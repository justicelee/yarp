# Copyright (C) 2006-2020 Istituto Italiano di Tecnologia (IIT)
# All rights reserved.
#
# This software may be modified and distributed under the terms of the
# BSD-3-Clause license. See the accompanying LICENSE file for details.

if(NOT DEFINED CMAKE_MINIMUM_REQUIRED_VERSION)
  cmake_minimum_required(VERSION 3.12)
  project(file_grabber)
  find_package(YARP REQUIRED COMPONENTS os sig dev)
endif()

add_executable(file_grabber)
target_sources(file_grabber PRIVATE file_grabber.cpp)
target_link_libraries(file_grabber PRIVATE YARP::YARP_os
                                           YARP::YARP_init
                                           YARP::YARP_sig
                                           YARP::YARP_dev)
set_property(TARGET file_grabber PROPERTY FOLDER "Examples/dev")


add_executable(file_grabber_net)
target_sources(file_grabber_net PRIVATE file_grabber_net.cpp)
target_link_libraries(file_grabber_net PRIVATE YARP::YARP_os
                                               YARP::YARP_init
                                               YARP::YARP_sig
                                               YARP::YARP_dev)
set_property(TARGET file_grabber_net PROPERTY FOLDER "Examples/dev")
