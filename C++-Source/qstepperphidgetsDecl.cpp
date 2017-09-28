#include "qstepperphidgetsDecl.h"
#include <unistd.h>
#include <stdlib.h>
#include "tsc_globaldata.h"
#include <stdlib.h>
#include <math.h>
#include <QDebug>

extern TSC_GlobalData *g_AllData;

//-----------------------------------------------------------------------------

QStepperPhidgetsDecl::QStepperPhidgetsDecl(double lacc, double lcurr){
    int sernum, version;
    double smax, amax, smin;

    this->SH = NULL;
    this->errorCreate = CPhidgetStepper_create(&SH);
    this->errorOpen = CPhidget_open((CPhidgetHandle)SH, -1);
    sleep(1);
    CPhidget_getSerialNumber((CPhidgetHandle)SH, &sernum);
    CPhidget_getDeviceVersion((CPhidgetHandle)SH, &version);
    sleep(1);
    this->snumifk=sernum;
    this->vifk=version;
    this->motorNum=1;
    CPhidgetStepper_getVelocityMax((CPhidgetStepperHandle)SH,0,&smax);
    CPhidgetStepper_getVelocityMin((CPhidgetStepperHandle)SH,0,&smin);
    CPhidgetStepper_getAccelerationMax((CPhidgetStepperHandle)SH,0,&amax);
    this->speedMin=smin;
    if (lacc > amax) {
        this->acc = amax;
    } else {
        this->acc = lacc;
    }
    this->currMax=lcurr;
    this->hBoxSlewEnded=false;
    CPhidgetStepper_setAcceleration((CPhidgetStepperHandle)SH,0,this->acc);
    CPhidgetStepper_setCurrentLimit((CPhidgetStepperHandle)SH,0,currMax);
    this->stopped=true;

    this->stepsPerSInDecl=round(0.0041780746*
            (g_AllData->getGearData(4))*
            (g_AllData->getGearData(5))*
            (g_AllData->getGearData(6))*
            (g_AllData->getGearData(8))/(g_AllData->getGearData(7)));
    this->speedMax=stepsPerSInDecl;
    CPhidgetStepper_setVelocityLimit((CPhidgetStepperHandle)SH,0,this->speedMax);
    // 360°/sidereal day in seconds*gear ratios*microsteps/steps
}

//-----------------------------------------------------------------------------

QStepperPhidgetsDecl::~QStepperPhidgetsDecl(void){
    this->stopped=PTRUE;
    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 0);
    sleep(1);
    CPhidget_close((CPhidgetHandle)SH);
    CPhidget_delete((CPhidgetHandle)SH);
}

//-----------------------------------------------------------------------------
bool QStepperPhidgetsDecl::travelForNSteps(long steps,short direction, int factor,bool isHBSlew) {

    this->hBoxSlewEnded = false;
    this->speedMax=factor*0.0041780746*
            (g_AllData->getGearData(4))*
            (g_AllData->getGearData(5))*
            (g_AllData->getGearData(6))*
            (g_AllData->getGearData(8))/(g_AllData->getGearData(7));
    if (direction < 0) {
        direction = -1;
    } else {
        direction = 1;
    }
    CPhidgetStepper_setVelocityLimit((CPhidgetStepperHandle)SH,0,this->speedMax);
    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 1);
    CPhidgetStepper_setCurrentPosition((CPhidgetStepperHandle)SH, 0, 0);
    CPhidgetStepper_setTargetPosition((CPhidgetStepperHandle)SH, 0, direction*steps);
    this->stopped = false;
    while (this->stopped == false) {
        CPhidgetStepper_getStopped((CPhidgetStepperHandle)SH, 0, &stopped);
    }
    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 0);
    this->speedMax=0.0041780746*
            (g_AllData->getGearData(4))*
            (g_AllData->getGearData(5))*
            (g_AllData->getGearData(6))*
            (g_AllData->getGearData(8))/(g_AllData->getGearData(7));
    CPhidgetStepper_setVelocityLimit((CPhidgetStepperHandle)SH,0,this->speedMax);
    if (isHBSlew == 1) {
        this->hBoxSlewEnded=true;
    }
    return true;
}

//-----------------------------------------------------------------------------

int QStepperPhidgetsDecl::retrievePhidgetStepperData(int what) {
    int retval;

    switch (what) {
    case 1:
        retval=this->snumifk;
        break;
    case 2:
        retval=this->vifk;
        break;
    case 3:
        retval=this->motorNum;
        break;
    default:
        retval=-1;
    }
    return retval;
}

//-----------------------------------------------------------------------------

double QStepperPhidgetsDecl::getKinetics(short whichOne) {
    double retval;

    switch (whichOne) {
    case 1:
        CPhidgetStepper_getCurrentLimit((CPhidgetStepperHandle)SH,0,&retval);
        break;
    case 2:
        CPhidgetStepper_getAcceleration((CPhidgetStepperHandle)SH,0,&retval);
        break;
    case 3:
        CPhidgetStepper_getVelocityLimit((CPhidgetStepperHandle)SH,0,&retval);
        break;
    case 4:
        retval = (this->speedMin);
        break;
    default:
        retval=-1;
    }
    return retval;
}

//-----------------------------------------------------------------------------

void QStepperPhidgetsDecl::setStepperParams(double val, short whichOne) {

    switch (whichOne) {
    case 1:
        this->acc=val;
        CPhidgetStepper_setAcceleration((CPhidgetStepperHandle)SH,0,this->acc);
        break;
    case 2:
        this->speedMax=val;
        CPhidgetStepper_setVelocityLimit((CPhidgetStepperHandle)SH,0,this->speedMax);
        break;
    case 3:
        this->currMax=val;
        CPhidgetStepper_setCurrentLimit((CPhidgetStepperHandle)SH,0,this->currMax);
    }
    return;
}


//-----------------------------------------------------------------------------

void QStepperPhidgetsDecl::shutDownDrive(void) {

    this->stopped=PTRUE;
    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 0);
    sleep(1);
}

//-----------------------------------------------------------------------------

bool QStepperPhidgetsDecl::getStopped(void) {
    return (this->stopped);
}

//------------------------------------------------------------------------------

void QStepperPhidgetsDecl::stopDrive(void) {

    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 0);
    CPhidgetStepper_setCurrentPosition((CPhidgetStepperHandle)SH, 0, 0);
    CPhidgetStepper_setTargetPosition((CPhidgetStepperHandle)SH, 0, 0);
    this->stopped=true;
    usleep(100);
}

//-------------------------------------------------------------------------------

void QStepperPhidgetsDecl::engageDrive(void) {
    this->stopped=false;
    CPhidgetStepper_setEngaged((CPhidgetStepperHandle)SH, 0, 1);
}

//-------------------------------------------------------------------------------

void QStepperPhidgetsDecl::changeSpeedForGearChange(void) {

    this->stepsPerSInDecl=round(0.0041780746*
            (g_AllData->getGearData(4))*
            (g_AllData->getGearData(5))*
            (g_AllData->getGearData(6))*
            (g_AllData->getGearData(8))/(g_AllData->getGearData(7)));
    this->speedMax=stepsPerSInDecl;
    CPhidgetStepper_setVelocityLimit((CPhidgetStepperHandle)SH,0,this->speedMax);
    // 360°/sidereal day in seconds*gear ratios*microsteps/steps
}

//-------------------------------------------------------------------------------

bool QStepperPhidgetsDecl::hasHBoxSlewEnded(void) {
    return this->hBoxSlewEnded;
}
