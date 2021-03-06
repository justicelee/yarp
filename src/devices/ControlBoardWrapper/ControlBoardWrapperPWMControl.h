/*
 * Copyright (C) 2006-2020 Istituto Italiano di Tecnologia (IIT)
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the
 * BSD-3-Clause license. See the accompanying LICENSE file for details.
 */

#ifndef YARP_DEV_CONTROLBOARDWRAPPER_CONTROLBOARDWRAPPERPWMCONTROL_H
#define YARP_DEV_CONTROLBOARDWRAPPER_CONTROLBOARDWRAPPERPWMCONTROL_H

#include <yarp/dev/IPWMControl.h>

#include "ControlBoardWrapperCommon.h"


class ControlBoardWrapperPWMControl :
        virtual public ControlBoardWrapperCommon,
        public yarp::dev::IPWMControl
{
public:
    inline bool getNumberOfMotors(int* num) override { return ControlBoardWrapperCommon::getNumberOfMotors(num); }
    bool setRefDutyCycle(int j, double v) override;
    bool setRefDutyCycles(const double* v) override;
    bool getRefDutyCycle(int j, double* v) override;
    bool getRefDutyCycles(double* v) override;
    bool getDutyCycle(int j, double* v) override;
    bool getDutyCycles(double* v) override;
};

#endif // YARP_DEV_CONTROLBOARDWRAPPER_CONTROLBOARDWRAPPERPWMCONTROL_H
