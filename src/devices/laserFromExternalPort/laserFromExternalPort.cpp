/*
 * Copyright (C) 2006-2020 Istituto Italiano di Tecnologia (IIT)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define _USE_MATH_DEFINES

#include "laserFromExternalPort.h"

#include <yarp/os/Time.h>
#include <yarp/os/Log.h>
#include <yarp/os/LogStream.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/math/Math.h>

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;

#ifndef DEG2RAD
#define DEG2RAD M_PI/180.0
#endif

#ifndef RAD2DEG
#define RAD2DEG 180/M_PI
#endif

YARP_LOG_COMPONENT(LASER_FROM_EXTERNAL_PORT, "yarp.devices.laserFromExternalPort")

/*
yarpdev --device Rangefinder2DWrapper --subdevice laserFromExternalPort \
--SENSOR::input_ports_name "(/port1 /port2)" \
--TRANSFORM_CLIENT::local /LaserFromExternalPort/tfClient \
--TRANSFORM_CLIENT::remote /transformServer \
--TRANSFORMS::src_frames "(/frame1 /frame2)" \
--TRANSFORMS::dst_frame /output_frame
--period 10 \
--name /outlaser:o

yarpdev --device Rangefinder2DWrapper --subdevice laserFromExternalPort \
--SENSOR::min_angle 0 
--SENSOR::max_angle 360
--SENSOR::resolution 0.5
--SENSOR::input_ports_name "(/port1 /port2)" \
--TRANSFORM_CLIENT::local /LaserFromExternalPort/tfClient \
--TRANSFORM_CLIENT::remote /transformServer \
--TRANSFORMS::src_frames "(/frame1 /frame2)" \
--TRANSFORMS::dst_frame /output_frame
--period 10 \
--name /outlaser:o
*/

InputPortProcessor::InputPortProcessor()
{
    m_contains_data=false;
}

void InputPortProcessor::onRead(yarp::dev::LaserScan2D& b)
{
    m_port_mutex.lock();
        m_lastScan = b;
        getEnvelope(m_lastStamp);
        m_contains_data=true;
    m_port_mutex.unlock();
}

inline void InputPortProcessor::getLast(yarp::dev::LaserScan2D& data, Stamp& stmp)
{
    //this blocks untils the first data is received;
    size_t counter =0;
    while (m_contains_data==false)
    {
        yarp::os::Time::delay(0.1);
        if (counter++ > 100) {yDebug() << "Waiting for incoming data..."; counter=0;}
    }

    m_port_mutex.lock();
        data = m_lastScan;
        stmp = m_lastStamp;
    m_port_mutex.unlock();
}

//-------------------------------------------------------------------------------------

bool LaserFromExternalPort::open(yarp::os::Searchable& config)
{
    Property subConfig;
    m_info = "LaserFromExternalPort device";

#ifdef LASER_DEBUG
    yCDebug(LASER_FROM_EXTERNAL_PORT) << "%s\n", config.toString().c_str());
#endif

    if (this->parseConfiguration(config) == false)
    {
        yCError(LASER_FROM_EXTERNAL_PORT) << "Error parsing parameters";
        return false;
    }
    m_empty_laser_data = m_laser_data;
    for (size_t i=0; i< m_empty_laser_data.size(); i++) m_empty_laser_data[i] = std::numeric_limits<double>::infinity();

    yarp::os::Searchable& general_config = config.findGroup("SENSOR");

    if (general_config.check("input_ports_name")) //this parameter is optional
    {
        yarp::os::Bottle* portlist = general_config.find("input_ports_name").asList();
        if (portlist)
        {
            for (size_t i = 0; i < portlist->size(); i++)
                m_port_names.push_back(portlist->get(i).asString());
        }
        else
        {
            m_port_names.push_back("/laserFromExternalPort:i");
        }

        for (size_t i = 0; i < m_port_names.size(); i++)
        {
            InputPortProcessor proc;
            m_input_ports.push_back(proc);
        }
        m_last_stamp.resize(m_port_names.size());
        m_last_scan_data.resize(m_port_names.size());
    }

    if (general_config.check("override")) //this parameter is optional
    {
        if (m_input_ports.size() != 1)
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "option override cannot be used when multiple ports are involved";
            return false;
        }
        else
        {
            m_option_override_limits = true;
        }
    }

    //open the tc client
    if (config.check("TRANSFORMS") && config.check("TRANSFORM_CLIENT"))
    {
        yarp::os::Searchable& transforms_config = config.findGroup("TRANSFORMS");
        yarp::os::Bottle* src_frames_list = transforms_config.find("src_frames").asList();
        if (src_frames_list)
        {
            if (src_frames_list->size() != m_input_ports.size())
            {
                yCError(LASER_FROM_EXTERNAL_PORT) << "src_frames invalid number";
                return false;
            }
            for (size_t i = 0; i < src_frames_list->size(); i++)
            {
                m_src_frame_id.push_back(src_frames_list->get(i).asString());
            }
        }
        else
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "src_frames invalid value or param not found";
            return false;
        }
        m_dst_frame_id = transforms_config.find("dst_frame").asString();
        if (m_dst_frame_id=="")
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "dst_frame param not found";
            return false;
        }


        std::string client_cfg_string = config.findGroup("TRANSFORM_CLIENT").toString();
        if (client_cfg_string=="")
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "TRANSFORM_CLIENT param not found";
            return false;
        }

        Property tcprop;
        tcprop.fromString(client_cfg_string);
        tcprop.put("device", "transformClient");

        m_tc_driver.open(tcprop);
        if (!m_tc_driver.isValid())
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "Error opening PolyDriver check parameters";
            return false;
        }
        m_tc_driver.view(m_iTc);
        if (!m_iTc)
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "Error opening iFrameTransform interface. Device not available";
            return false;
        }
        yarp::os::Time::delay(0.1);
    }

    for (auto i=0; i<m_input_ports.size(); i++)
    {
        m_input_ports[i].useCallback();
        if (m_input_ports[i].open(m_port_names[i]) == false)
        {
            yCError(LASER_FROM_EXTERNAL_PORT) << "Error opening port:" << m_port_names[i];
            return false;
        }
    }
    PeriodicThread::start();

    yInfo("LaserFromExternalPort: Sensor ready");
    return true;
}

bool LaserFromExternalPort::close()
{
    PeriodicThread::stop();

    for (auto it=m_input_ports.begin(); it!= m_input_ports.end(); it++)
    {
        it->close();
    }

    yCInfo(LASER_FROM_EXTERNAL_PORT) << "LaserFromExternalPort closed";
    return true;
}



bool LaserFromExternalPort::setDistanceRange(double min, double max)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_min_distance = min;
    m_max_distance = max;
    return true;
}

bool LaserFromExternalPort::setScanLimits(double min, double max)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    yCWarning(LASER_FROM_EXTERNAL_PORT) << "setScanLimits not yet implemented";
    return true;
}



bool LaserFromExternalPort::setHorizontalResolution(double step)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    yCWarning(LASER_FROM_EXTERNAL_PORT, "setHorizontalResolution not yet implemented");
    return true;
}

bool LaserFromExternalPort::setScanRate(double rate)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    yCWarning(LASER_FROM_EXTERNAL_PORT, "setScanRate not yet implemented");
    return false;
}



bool LaserFromExternalPort::threadInit()
{
#ifdef LASER_DEBUG
    yCDebug(LASER_FROM_EXTERNAL_PORT) <<"LaserFromExternalPort:: thread initialising...\n");
    yCDebug(LASER_FROM_EXTERNAL_PORT) <<"... done!\n");
#endif

    return true;
}

void LaserFromExternalPort::calculate(yarp::dev::LaserScan2D scan_data, yarp::sig::Matrix m)
{
    double t_off_deg = 0;
    double t_off_rad = t_off_deg * DEG2RAD;
    double x_off = m[3][0];
    double y_off = m[3][1];
#ifdef DO_NOTHING_DEBUG
    double x_off = 0;
    double y_off = 0;
    double t_off_deg = 0;
    double t_off_rad = 0;
#endif 

////////////////////////////
    double resolution = (scan_data.angle_max - scan_data.angle_min)/ scan_data.scans.size(); // deg/elem
    for (auto i = 0; i < scan_data.scans.size(); i++)
    {
        double distance = scan_data.scans[i];
        if (distance == std::numeric_limits<double>::infinity())
        {
            //skip infinity
        }
        if (std::isnan(distance))
        {
            //skip nan
        }
        else
        {
            //if we received a valid value, process it and put it in the right slot
            double angle_input_deg = (i * resolution) + scan_data.angle_min;
            double angle_input_rad = (angle_input_deg) * DEG2RAD;

            //calculate vertical and horizontal components of input angle
            double Ay = (sin(angle_input_rad + t_off_rad) * distance);
            double Ax = (cos(angle_input_rad + t_off_rad) * distance);

            //calculate vertical and horizontal components of new angle with offset.
            double By = Ay + y_off;
            double Bx = Ax + x_off;

            double angle_output_rad = atan2(By, Bx); //the output is (-pi +pi)
            double angle_output_deg = angle_output_rad * RAD2DEG; //the output is (-180 +180)
            angle_output_deg += 180; //the output is (0 360)
            if (angle_output_deg >=360) angle_output_deg=-360; //the output is (0 360(

            //check if angle is inside limits
            if (angle_output_deg > m_max_angle) { continue; }
            if (angle_output_deg < m_min_angle) { continue; }

            //compute the new slot
            int new_i = lrint ((angle_output_deg - m_min_angle)  / m_resolution);
           
            yAssert (new_i >= 0);
            yAssert (new_i < m_laser_data.size());

            //compute the distance
            double newdistance = std::sqrt((Bx * Bx) + (By * By));

            //assignment on empty (nan) slots or in valid slots if distance is shorter
            if (std::isnan(m_laser_data[new_i]))
            {
                m_laser_data[new_i] = newdistance;
            }
            else if (newdistance < m_laser_data[new_i])
            {
                m_laser_data[new_i] = newdistance;
            }
        }
    }
}

void LaserFromExternalPort::run()
{
#ifdef DEBUG_TIMING
    double t1 = yarp::os::Time::now();
#endif
    std::lock_guard<std::mutex> guard(m_mutex);
    m_laser_data = m_empty_laser_data;

    size_t nports= m_input_ports.size();
    if (nports == 1) //one single port, optimes version
    {
        m_input_ports[0].getLast(m_last_scan_data[0], m_last_stamp[0]);
        size_t received_scans = m_last_scan_data[0].scans.size();

        if (m_option_override_limits)
        {
            //this overrides user setting with parameters received from the port
            m_sensorsNum = received_scans;
            m_max_angle = m_last_scan_data[0].angle_max;
            m_min_angle = m_last_scan_data[0].angle_min;
            m_max_distance = m_last_scan_data[0].range_max;
            m_min_distance = m_last_scan_data[0].range_min;
            m_resolution = received_scans / (m_max_angle - m_min_angle);
            if (m_laser_data.size() != m_sensorsNum) m_laser_data.resize(m_sensorsNum);
        }

        if (m_iTc==nullptr)
        {
            for (size_t elem = 0; elem < m_sensorsNum; elem++)
            {
                m_laser_data[elem] = m_last_scan_data[0].scans[elem]; //m
            }
        }
        else
        {
            yarp::sig::Matrix m(4, 4); m.eye();
            bool frame_exists = m_iTc->getTransform(m_src_frame_id[0], m_dst_frame_id, m);
            if (frame_exists == false)
            {
                yCWarning(LASER_FROM_EXTERNAL_PORT) << "Unable to found m matrix" << "and" << m_dst_frame_id;
            }
            calculate(m_last_scan_data[0], m);
        }
    }
    else //multiple ports
    {
        for (auto i=0; i<nports;i++)
        {
            yarp::sig::Matrix m(4, 4); m.eye();
            bool frame_exists = m_iTc->getTransform(m_src_frame_id[i], m_dst_frame_id, m);
            if (frame_exists == false)
            {
                yCWarning(LASER_FROM_EXTERNAL_PORT) << "Unable to found m matrix between" << "and" << m_dst_frame_id;
            }
            m_input_ports[i].getLast(m_last_scan_data[i], m_last_stamp[i]);
            calculate(m_last_scan_data[i], m);
        }
    }

    applyLimitsOnLaserData();

    return;
}

void LaserFromExternalPort::threadRelease()
{
#ifdef LASER_DEBUG
    yCDebug(LASER_FROM_EXTERNAL_PORT) <<"Thread releasing...");
    yCDebug(LASER_FROM_EXTERNAL_PORT) <<"... done.");
#endif

    return;
}
