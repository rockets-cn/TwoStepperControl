﻿#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <qtimer.h>
#include <QtConcurrent/qtconcurrentrun.h>
#include <QDir>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkInterface>
#include <QTime>
#include <QDate>
#include <math.h>
#include <unistd.h>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <QMessageBox>
#include "QDisplay2D.h"
#include "tsc_globaldata.h"
#include "tsc_bt_serialcomm.h"

TSC_GlobalData *g_AllData; // a global class that holds system specific parameters on drive, current mount position, gears and so on ...

//------------------------------------------------------------------
// constructor of the GUI - takes care of everything....
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent), ui(new Ui::MainWindow) {
    int serNo; // serial number of the phidgets boards
    double val,draccRA, draccDecl, drcurrRA, drcurrDecl; // local values on drive acceleration and so on...
    QtContinuousStepper *dummyDrive; // a helper to determine the right order of drives
    QDir *catalogDir; // the directory holding the local .tsc catalogs
    QFileInfoList catFiles; // a list of available .tsc files
    QFileInfo catFileInfo; // a helper on the file list of catalogs
    QStringList filter; // needed for isolating the .tsc files
    QString *catfName; // name of a .tsc catalog
    QList<QHostAddress> ipAddressList;
    int listIter;

    ui->setupUi(this); // making the widget
    g_AllData =new TSC_GlobalData(); // instantiate the global class with parameters
    this->timer = new QTimer(); // start the event timer ... this is NOT the microtimer for the mount
    this->timer->start(100); // check all 100 ms for events
    elapsedGoToTime = new QElapsedTimer(); // timer for roughly measuring time taked during GoTo
    this->st4Timer = new QTimer();
    this->st4Timer->start(10); // if ST4 is active, the interface is read every 10 ms
    this->LX200Timer = new QTimer();
    this->LX200Timer->start(200);
    this->UTDate = new QDate(QDate::currentDate());
    this->julianDay = this->UTDate->toJulianDay();
    this->UTTime = new QTime(QTime::currentTime());
    this->UTTime->start();
    this->findOutAboutINDIServerPID(); // check running processes and store the ID of an INDIserver in a hidden file

    draccRA= g_AllData->getDriveParams(0,1);
    draccDecl= g_AllData->getDriveParams(1,1);
    drcurrRA= g_AllData->getDriveParams(0,2);
    drcurrDecl= g_AllData->getDriveParams(1,2); // retrieving acceleration and maximum current for the phidget boards

    // start searching for the right boards for the drives ...
    dummyDrive = new QtContinuousStepper(); // call the first phidget interface to the board of the stepper
    serNo = dummyDrive->retrieveKineticStepperData(1);
    delete dummyDrive;
    if ((g_AllData->getDriveID(0) != serNo) && (g_AllData->getDriveID(1) != serNo)) { //no driver boards are assigned to drives
        StepperDriveRA = new QtContinuousStepper(); // call the phidget interface to the board of the stepper
        serNo = StepperDriveRA->retrieveKineticStepperData(1);     // get the serial number of the phidget board
        g_AllData->setDriveData(0,serNo); // remember the ID of the RA-drive in the global class
        StepperDriveDecl = new QtKineticStepper(); // call the phidget interface to the board of the stepper
        serNo = StepperDriveDecl->retrieveKineticStepperData(1); // get the serial number of the phidget board
        g_AllData->setDriveData(1,serNo); // remember the ID of the Decl-drive in the global class
    } else { // IDs are written in the "TSC_Preferences.tsc" file
        dummyDrive = new QtContinuousStepper(); // call the first phidget interface to the board of the stepper
        serNo = dummyDrive->retrieveKineticStepperData(1);
        if (serNo != g_AllData->getDriveID(0)) { // dummy drive is NOT the designatedRA Drive
            StepperDriveRA = new QtContinuousStepper(); // call the phidget interface to the board of the stepper
            serNo =StepperDriveRA->retrieveKineticStepperData(1);
            g_AllData->setDriveData(0,serNo);
            delete dummyDrive; // set the other board to RA
            StepperDriveDecl = new QtKineticStepper(); // call the phidget interface to the board of the stepper
            serNo = StepperDriveDecl->retrieveKineticStepperData(1);
            g_AllData->setDriveData(1,serNo);
        } else {
            StepperDriveDecl = new QtKineticStepper(); // call the phidget interface to the board of the stepper
            serNo = StepperDriveDecl->retrieveKineticStepperData(1);
            g_AllData->setDriveData(1,serNo);
            delete dummyDrive; // set the other board to Decl
            StepperDriveRA = new QtContinuousStepper(); // call the phidget interface to the board of the stepper
            serNo =StepperDriveRA->retrieveKineticStepperData(1);
            g_AllData->setDriveData(0,serNo);
        }
    }
    this->StepperDriveRA->setGearRatioAndMicrosteps(g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2)/g_AllData->getGearData(3),g_AllData->getGearData(8));
    this->StepperDriveRA->setInitialParamsAndComputeBaseSpeed(draccRA,drcurrRA); // setting initial parameters for the ra drive
    this->StepperDriveDecl->setGearRatioAndMicrosteps(g_AllData->getGearData(4)*g_AllData->getGearData(5)*g_AllData->getGearData(6)/g_AllData->getGearData(7),g_AllData->getGearData(8));
    this->StepperDriveDecl->setInitialParamsAndComputeBaseSpeed(draccDecl,drcurrDecl); // setting initial parameters for the declination drive

        // set a bunch of flags and factors
    this->mountMotion.RATrackingIsOn=false;   // sidereal tracking is on if true
    this->mountMotion.RADriveIsMoving =false; // RA drive is moving faster than sideral tracking if true
    this->mountMotion.DeclDriveIsMoving = false; // Decl drive is moving if true
    this->mountMotion.GoToIsActiveInRA = false; // system is in a slew state, RA is moving. most system functionality is disabled
    this->mountMotion.GoToIsActiveInDecl = false; // system is in a slew state, Decl is moving. most system functionality is disabled
    this->mountMotion.emergencyStopTriggered = false; // system can be halted by brute force. true if this was triggered
    this->lx200IsOn = false; // true if a serial connection was opened vai RS232
    this->ccdCameraIsAcquiring=false; // true if images are coming in from INDI-server
    this->mountMotion.DeclDriveDirection = 1; // 1 for forward, -1 for backward
    this->mountMotion.RADriveDirection = 1; // 1 for forward, -1 for backward
    this->mountMotion.RASpeedFactor=1;
    this->mountMotion.DeclSpeedFactor=1; // speeds are multiples of sidereal compensation
    ui->rbCorrSpeed->setChecked(true); // activate radiobutton for correction speed ... this is sidereal speed
    this->guidingState.guideStarSelected=false;
    this->guidingState.guidingIsOn=false;
    g_AllData->setGuidingState(this->guidingState.guidingIsOn); // this has to be known in other classes, so every "guidingIsOn" state is copied
    this->guidingState.calibrationIsRunning=false;
    this->guidingState.systemIsCalibrated=false;
    this->guidingState.calibrationImageReceived=false;
    this->guidingState.travelTime_ms = 50;
    this->guidingState.declinationDriveDirection=+1; // for backlash compensation - remember direction of last declination travel during guiding
    this->guidingState.rotationAngle=0.0;
    this->guidingState.maxDevInArcSec=0.0;
    this->guidingState.backlashCompensationInMS = 0.0;
    this->guidingState.noOfGuidingSteps = 0;
    this->guidingState.st4IsActive = false;
    this->dslrStates.dslrExposureIsRunning = false;
    this->dslrStates.dslrSeriesRunning = false;
    ui->rbSiderealSpeed->setChecked(true); // make sure that sidereal speed is set...
    this->setTrackingRate();

        // now instantiate the GPIO - ports on the raspberry for ST4 guiding and DSLR control
    setenv("WIRINGPI_GPIOMEM", "1", 1); // otherwise, the program needs sudo - privileges
    wiringPiSetup();
    pinMode (2, INPUT);
    pinMode (3, INPUT);
    pinMode (4, INPUT);
    pinMode (5, INPUT); // setting up BCM-pins 22, 23, 24 and 27 as inputs for ST4
    pullUpDnControl(2,PUD_UP);
    pullUpDnControl(3,PUD_UP);
    pullUpDnControl(4,PUD_UP);
    pullUpDnControl(5,PUD_UP); // setting internal pull-ip resistors of the BCM
    ui->pbStopST4->setEnabled(false);
    pinMode (0, OUTPUT);
    pinMode (1, OUTPUT); // settin BCM-pins 17 and 18 to output mode for dslr-control

        // now setting all the parameters in the "Drive"-tab. settings are from pref-file, except for the stepper speed, which is
        // calculated from  gear parameters
    g_AllData->setDriveParams(0,0,this->StepperDriveRA->getKineticsFromController(3)); // velocity limit - this is set to sidereal speed for right ascension in the constructor of the stepper class ...
    g_AllData->setDriveParams(1,0,this->StepperDriveDecl->getKineticsFromController(3)); // velocity limit - this is set to sidereal speed for declination in the constructor of the stepper class ...
    this->StepperDriveRA->setStepperParams((g_AllData->getDriveParams(0,1)),1); // acceleration in RA
    this->StepperDriveRA->setStepperParams((g_AllData->getDriveParams(0,2)),3); // motor current in RA
    this->StepperDriveDecl->setStepperParams((g_AllData->getDriveParams(1,1)),1); // acceleration in Decl
    this->StepperDriveDecl->setStepperParams((g_AllData->getDriveParams(1,2)),3); // motor current in Decl
    textEntry = new QString();
    val=(this->StepperDriveRA->getKineticsFromController(3));
    ui->leVMaxRA->setText(textEntry->number(val,'f',2));
    textEntry->clear();
    ui->leAMaxRA->setText(textEntry->number(this->StepperDriveRA->getKineticsFromController(2)));
    textEntry->clear();
    ui->leCurrMaxRA->setText(textEntry->number(this->StepperDriveRA->getKineticsFromController(1)));
    textEntry->clear();
    val=(this->StepperDriveDecl->getKineticsFromController(3));
    ui->leVMaxDecl->setText(textEntry->number(val,'f',2));
    textEntry->clear();
    ui->leAMaxDecl->setText(textEntry->number(this->StepperDriveDecl->getKineticsFromController(2)));
    textEntry->clear();
    ui->leCurrMaxDecl->setText(textEntry->number(this->StepperDriveDecl->getKineticsFromController(1)));
    textEntry->clear();
    ui->leRAPlanetary->setText(textEntry->number(g_AllData->getGearData(0)));
    textEntry->clear();
    ui->leRAGear->setText(textEntry->number(g_AllData->getGearData(1)));
    textEntry->clear();
    ui->leRAWorm->setText(textEntry->number(g_AllData->getGearData(2)));
    textEntry->clear();
    ui->leRAStepsize->setText(textEntry->number(g_AllData->getGearData(3)));
    textEntry->clear();
    ui->leDeclPlanetary->setText(textEntry->number(g_AllData->getGearData(4)));
    textEntry->clear();
    ui->leDeclGear->setText(textEntry->number(g_AllData->getGearData(5)));
    textEntry->clear();
    ui->leDeclWorm->setText(textEntry->number(g_AllData->getGearData(6)));
    textEntry->clear();
    ui->leDeclStepSize->setText(textEntry->number(g_AllData->getGearData(7)));
    textEntry->clear();
    ui->leMicrosteps->setText(textEntry->number(g_AllData->getGearData(8)));
    textEntry->clear();
        // now setting all the parameters in the "Cam"-tab
    ui->lePixelSizeX->setText(textEntry->number(g_AllData->getCameraPixelSize(0)));
    textEntry->clear();
    ui->lePixelSizeY->setText(textEntry->number(g_AllData->getCameraPixelSize(1)));
    textEntry->clear();
    ui->leFrameSizeX->setText(textEntry->number(g_AllData->getCameraChipPixels(0)));
    textEntry->clear();
    ui->leFrameSizeY->setText(textEntry->number(g_AllData->getCameraChipPixels(1)));
    textEntry->clear();
        // now setting parameters in the "Settings"-tab
    ui->leControllerName->setText(g_AllData->getSiteName());
    ui->leLat->setText(textEntry->number(g_AllData->getSiteCoords(0)));
    textEntry->clear();
    ui->leLong->setText(textEntry->number(g_AllData->getSiteCoords(1)));
    textEntry->clear();
    ui->leUTCOffs->setText(textEntry->number(g_AllData->getSiteCoords(2)));
    textEntry->clear();

        // camera and guiding class are instantiated
    camera_client = new ccd_client(); // install a camera client for guiding via INDI
    guiding = new ocv_guiding();
    guideStarPrev = new QPixmap(); // a pixmap for showing the preview window
    guideStarPosition.centrX =0.0;
    guideStarPosition.centrY =0.0;
    this->guidingFOVFactor=1.0; // location of the crosshair and size of the preview window are set
    ui->sbFLGuideScope->setValue(g_AllData->getGuideScopeFocalLength()); // get stored focal length for the guidescope
    this->guiding->setFocalLengthOfGuidescope(g_AllData->getGuideScopeFocalLength());
    this->guidingLog=NULL;

        // now read all catalog files, ending in "*.tsc"
    catalogDir = new QDir("Catalogs/");
    filter << "*.tsc";
    catalogDir->setNameFilters(filter);
    catFiles = catalogDir->entryInfoList();
    foreach (catFileInfo, catFiles) {
        catfName = new QString((const QString)catFileInfo.fileName());
        catfName->remove(((catfName->length())-4),4);
        ui->listWidgetCatalog->addItem(catfName->toLatin1());
        delete catfName;
    }
    delete catalogDir;
        // filled the selection with all ".tsc" files in the home directory
    this->objCatalog=NULL; // the topical catalogue
    this->ra = 0.0;
    this->decl = 0.0; // the sync position - no sync for the mount was carried out - these are displayed in the GOTO textentry
    this->camView = new QDisplay2D(ui->camTab,550,400); // make the clickable scene view of 425 x 340 pixels
    this->camImg= new QPixmap(g_AllData->getCameraDisplaySize(0),g_AllData->getCameraDisplaySize(1)); // store the size of the scene view in the global parameter class
    this->camImageWasReceived=false; // no camera image came in yet ...
    RAdriveDirectionForNorthernHemisphere = 1; //switch this for the southern hemisphere to -1 ... RA is inverted
    g_AllData->storeGlobalData();
    g_AllData->setSyncPosition(0.0, 0.0); // deploy a fake sync to the mount so that the microtimer starts ...

    // read all available IP addresses and make them available for LX200
    ipAddressList = QNetworkInterface::allAddresses();
    for(listIter = 0; listIter < ipAddressList.count(); listIter++) {
        if ((ipAddressList[listIter].isLoopback() == false) && (ipAddressList[listIter].protocol() == QAbstractSocket::IPv4Protocol)) {
            ui->listWidgetIPAddresses->addItem(ipAddressList[listIter].toString());
        }
    }
    this->LXServer = new QTcpServer();
    this->LXSocket = new QTcpSocket(this);
    this->LXServerAddress = new QHostAddress(); // creating a server, a socket and a hostaddress for the LX 200 tcp/ip server
    this->tcpLXdata = new QByteArray(); // a byte array holding the data coming in from the TCP/IP socket

        // instantiate the class for serial communication via LX200
    this->LX200SerialPortIsUp = false;
    this->lx200SerialPort = new QSerialPort();
    this->lx200SerialPort->setPortName("/dev/ttyS0");
    this->lx200SerialPort->setBaudRate(QSerialPort::Baud9600);
    this->lx200SerialPort->setDataBits(QSerialPort::Data8);
    this->lx200SerialPort->setParity(QSerialPort::NoParity);
    this->lx200SerialPort->setStopBits(QSerialPort::OneStop);
    this->lx200SerialPort->setFlowControl(QSerialPort::NoFlowControl);
    this->lx200SerialData = new QByteArray();
    this->lx200port= new lx200_communication();

    this->LXSetNumberFormatToSimple(); // LX200 knows a simple and a complex number format for RA and Decl - set format to simple here ...

        // instantiate communications with handbox
    ui->leBTMACAddress->setText(*(g_AllData->getBTMACAddress()));
    this->bt_Handbox = new tsc_bt_serialcomm(*(g_AllData->getBTMACAddress()));
    this->bt_HandboxCommand=new QString(); // a string that holds the data from the bluetooth-handbox
    this->mountMotion.btMoveNorth=0;
    this->mountMotion.btMoveEast=0;
    this->mountMotion.btMoveSouth=0;
    this->mountMotion.btMoveWest=0;

        // connecting signals and slots
    connect(this->timer, SIGNAL(timeout()), this, SLOT(updateReadings())); // this is the event queue
    connect(this->LX200Timer, SIGNAL(timeout()), this, SLOT(readLX200Port())); // this is the event for reading LX200
    connect(this->st4Timer, SIGNAL(timeout()), this, SLOT(readST4Port())); // this is the event for reading LX200
    connect(ui->listWidgetCatalog,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(catalogChosen(QListWidgetItem*))); // choose an available .tsc catalog
    connect(ui->listWidgetObject,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(catalogObjectChosen())); // catalog selection
    connect(ui->listWidgetIPAddresses,SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(IPaddressChosen())); // selection of IP address for LX 200
    connect(ui->cbLXSimpleNumbers, SIGNAL(released()),this, SLOT(LXSetNumberFormatToSimple())); // switch between simple and complex LX 200 format
    connect(ui->cbIsOnNorthernHemisphere, SIGNAL(stateChanged(int)), this, SLOT(invertRADirection())); // switch direction of RA motion for the southern hemisphere
    connect(ui->cbStoreGuideCamImgs, SIGNAL(stateChanged(int)), this, SLOT(enableCamImageStorage())); // a checkbox that starts saving all camera images in the camera-class
    connect(ui->cbMedianFilter, SIGNAL(stateChanged(int)), this, SLOT(changePrevImgProc())); // apply a 3x3 median filter to the guidestar - image
    connect(ui->sbCCDGain, SIGNAL(valueChanged(int)), this, SLOT(changeCCDGain())); // change the gain of the guiding camera via INDI
    connect(ui->sbMoveSpeed, SIGNAL(valueChanged(int)),this,SLOT(changeMoveSpeed())); // set factor for faster manual motion
    connect(ui->sbFLGuideScope, SIGNAL(valueChanged(int)), this, SLOT(changeGuideScopeFL())); // spinbox for guidescope - focal length
    connect(ui->leAMaxRA, SIGNAL(textChanged(QString)), this, SLOT(setMaxStepperAccRA())); // process input on stepper parameters in gear-tab
    connect(ui->leCurrMaxRA, SIGNAL(textChanged(QString)), this, SLOT(setMaxStepperCurrentRA())); // process input on stepper parameters in gear-tab
    connect(ui->leAMaxDecl, SIGNAL(textChanged(QString)), this, SLOT(setMaxStepperAccDecl())); // process input on stepper parameters in gear-tab
    connect(ui->leCurrMaxDecl, SIGNAL(textChanged(QString)), this, SLOT(setMaxStepperCurrentDecl())); // process input on stepper parameters in gear-tab
    connect(ui->rbCorrSpeed,SIGNAL(released()), this, SLOT(setCorrectionSpeed())); // set speed for slow manual motion
    connect(ui->rbMoveSpeed,SIGNAL(released()), this, SLOT(setMoveSpeed())); // set speed for faster manual motion
    connect(ui->rbFOVStd, SIGNAL(released()), this, SLOT(setRegularFOV())); // guidestar window set to 180x180 pixels
    connect(ui->rbFOVHalf, SIGNAL(released()), this, SLOT(setHalfFOV())); // guidestar window set to 90x90 pixels
    connect(ui->rbFOVDbl, SIGNAL(released()), this, SLOT(setDoubleFOV())); // guidestar window set to 360x360 pixels
    connect(ui->rbSiderealSpeed, SIGNAL(released()), SLOT(setTrackingRate())); // set sidereal tracking rate
    connect(ui->rbLunarSpeed, SIGNAL(released()), SLOT(setTrackingRate())); // set lunar tracking rate
    connect(ui->rbSolarSpeed, SIGNAL(released()), SLOT(setTrackingRate())); // set solar tracking rate
    connect(ui->hsThreshold,SIGNAL(valueChanged(int)), this, SLOT(changePrevImgProc())); // change threshold for selecting a guidestar
    connect(ui->hsIContrast ,SIGNAL(valueChanged(int)), this, SLOT(changePrevImgProc())); // change contrast for selecting a guidestar
    connect(ui->hsIBrightness ,SIGNAL(valueChanged(int)), this, SLOT(changePrevImgProc())); // change brightness for selecting a guidestar
    connect(ui->pbExit,SIGNAL(clicked()), this, SLOT(shutDownProgram())); // this kills the program, including killing the drives
    connect(ui->pbConnectToServer,SIGNAL(clicked()),this, SLOT(setINDISAddrAndPort())); // connects to the INDI server at the given address ...
    connect(ui->pbKillINDIServer, SIGNAL(clicked()),this, SLOT(killRunningINDIServer())); // a button that kill running INDI servers ...
    connect(ui->pbDisconnectFromServer, SIGNAL(clicked()), this, SLOT(disconnectFromINDIServer())); // disconnects from INDI server
    connect(ui->pbStoreCCDParameters, SIGNAL(clicked()), this, SLOT(storeCCDData())); // store ccd parameters to preferences file manually
    connect(ui->pbExpose, SIGNAL(clicked()), this, SLOT(startCCDAcquisition())); // start acquiring images from the guidecam. a signal is emitted if an image arrived.
    connect(ui->pbStopExposure, SIGNAL(clicked()), this, SLOT(stopCCDAcquisition())); // just set the local flag on ccd-acquisition so that no new image is polled in "displayGuideCamImage".
    connect(ui->pbClearINDILog,SIGNAL(clicked()), this, SLOT(clearINDILog())); // clear the textbox for INDI server messages
    connect(ui->pbSync, SIGNAL(clicked()), this, SLOT(syncMount())); // reset the current position and global timer, and set the global mount position to the actual coordinates
    connect(ui->pbStoreGears, SIGNAL(clicked()), this, SLOT(storeGearData())); // well - take the data from the dialog and store them in the .tsp file and in g_AllData
    connect(ui->pbStoreSiteData, SIGNAL(clicked()), this, SLOT(storeSiteData())); // store information the observatory position and so on
    connect(ui->pbStartTracking, SIGNAL(clicked()),this,SLOT(startRATracking())); // start earth motion compensation in RA
    connect(ui->pbStopTracking, SIGNAL(clicked()),this,SLOT(stopRATracking())); // stop earth motion compensation in RA
    connect(ui->pbDeclUp, SIGNAL(clicked()),this,SLOT(declinationMoveHandboxUp())); // manual motion of the handbox - decl up
    connect(ui->pbDeclDown, SIGNAL(clicked()),this,SLOT(declinationMoveHandboxDown())); // manual motion of the handbox - decl down
    connect(ui->pbRAPlus, SIGNAL(clicked()),this,SLOT(RAMoveHandboxFwd())); // manual motion of the handbox - ra towards sunset
    connect(ui->pbRAMinus, SIGNAL(clicked()),this,SLOT(RAMoveHandboxBwd())); // manual motion of the handbox - ra towards dawn
    connect(ui->pbStoreDrive, SIGNAL(clicked()), this, SLOT(storeDriveData())); // store data to preferences
    connect(ui->pbGoTo, SIGNAL(clicked()),this, SLOT(startGoToObject())); // start the slew routine
    connect(ui->pbLX200Active, SIGNAL(clicked()), this, SLOT(switchToLX200())); // open the serial port for LX 200
    connect(ui->pbStartINDIServer, SIGNAL(clicked()), this, SLOT(deployINDICommand())); // call a system command to start an INDI server with given driver parameters
    connect(ui->pbStop1, SIGNAL(clicked()), this, SLOT(emergencyStop())); // kill all motion immediately
    connect(ui->pbStop2, SIGNAL(clicked()), this, SLOT(emergencyStop())); // kill all motion immediately
    connect(ui->pbStop5, SIGNAL(clicked()), this, SLOT(emergencyStop())); // kill all motion immediately
    connect(ui->pbEnableTCP, SIGNAL(clicked()), this, SLOT(connectToIPSocket())); // connect to a LX 200 socket
    connect(ui->pbDisableTCP, SIGNAL(clicked()), this, SLOT(disconnectFromIPSocket())); // disconnect from LX 200 socket
    connect(ui->pbPGDecPlus, SIGNAL(clicked()), this, SLOT(declPGPlus())); // pulse guide for a given amount of time defined in a spinbox
    connect(ui->pbPGDecMinus, SIGNAL(clicked()), this, SLOT(declPGMinus())); // pulse guide for a given amount of time defined in a spinbox
    connect(ui->pbPGRAPlus, SIGNAL(clicked()), this, SLOT(raPGFwd())); // pulse guide for a given amount of time defined in a spinbox
    connect(ui->pbPGRAMinus, SIGNAL(clicked()), this, SLOT(raPGBwd())); // pulse guide for a given amount of time defined in a spinbox
    connect(ui->pbClearLXLog, SIGNAL(clicked()), this, SLOT(clearLXLog())); // delete the log of LX200 commands
    connect(ui->pbSelectGuideStar, SIGNAL(clicked()), this, SLOT(selectGuideStar())); // select a guide star defined by crosshair in the QDisplay - widget
    connect(ui->pbGuiding,SIGNAL(clicked()), this, SLOT(doAutoGuiding())); // instantiate all variables for autoguiding and set a flag that takes care of correction in "displayGuideCamImage" and "correctGuideStarPosition"
    connect(ui->pbStoreFL, SIGNAL(clicked()), this, SLOT(storeGuideScopeFL())); // store focal length of guidescope to preferences
    connect(ui->pbTryBTRestart, SIGNAL(clicked()), this, SLOT(restartBTComm())); // try restarting RF comm connection for Bluetooth
    connect(ui->pbTrainAxes, SIGNAL(clicked()),this, SLOT(calibrateAutoGuider())); // find rotation and stepwidth for autoguiding
    connect(ui->pbResetGuiding, SIGNAL(clicked()), this, SLOT(resetGuidingCalibration())); // reset autoguider calibration
    connect(ui->pbResetGdErr, SIGNAL(clicked()), this, SLOT(resetGuidingError())); // reset autoguider guiding error
    connect(ui->pbConnectBT, SIGNAL(clicked()),this, SLOT(startBTComm())); // stop BT communication
    connect(ui->pbDisonnectBT, SIGNAL(clicked()),this, SLOT(stopBTComm())); // start BT communication
    connect(ui->pbStartST4, SIGNAL(clicked()),this, SLOT(startST4Guiding())); // start ST4 pulse guiding
    connect(ui->pbStopST4, SIGNAL(clicked()),this, SLOT(stopST4Guiding())); // stop ST4 pulse guiding
    connect(ui->pbMeridianFlip, SIGNAL(clicked()), this, SLOT(doMeridianFlip())); // carry out meridian flip
    connect(ui->pbDSLRSingleShot, SIGNAL(clicked()), this, SLOT(handleDSLRSingleExposure())); // start a dslr exposure
    connect(ui->pbDSLRStartSeries, SIGNAL(clicked()), this, SLOT(startDSLRSeries())); // start a series of DSLR exposures
    connect(ui->pbDSLRStopSeries, SIGNAL(clicked()), this, SLOT(terminateDSLRSeries())); // terminate a dslr series exposure early
    connect(ui->pbConveyCoordinates, SIGNAL(clicked()), this, SLOT(transferCoordinates())); // slot that transfers coordinates to the controller
    connect(ui->pbDSLRTerminateExposure, SIGNAL(clicked()), this, SLOT(terminateDSLRSingleShot())); // stop a single DSLR exposure
    connect(this, SIGNAL(dslrExposureDone()), this, SLOT(takeNextExposureInSeries())); // this is called when an exposure is done; if a series is taken, the next exposure is triggered ...
    connect(this->lx200port,SIGNAL(RS232moveEast()), this, SLOT(LXmoveEast()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232moveWest()), this, SLOT(LXmoveWest()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232moveNorth()), this, SLOT(LXmoveNorth()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232moveSouth()), this, SLOT(LXmoveSouth()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232stopMoveEast()), this, SLOT(LXstopMoveEast()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232stopMoveWest()), this, SLOT(LXstopMoveWest()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232stopMoveNorth()), this, SLOT(LXstopMoveNorth()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232stopMoveSouth()), this, SLOT(LXstopMoveSouth()),Qt::QueuedConnection); // LX 200 handbox commands
    connect(this->lx200port,SIGNAL(RS232stopMotion()), this, SLOT(LXstopMotion()),Qt::QueuedConnection); // total stop of all motion by LX 200
    connect(this->lx200port,SIGNAL(RS232guideSpeed()), this, SLOT(LXslowSpeed()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232centerSpeed()), this, SLOT(LXslowSpeed()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232findSpeed()), this, SLOT(LXhiSpeed()),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(RS232gotoSpeed()), this, SLOT(LXhiSpeed()),Qt::QueuedConnection); // LX 200 knows four speeds, we only know 2 - sidereal correction and fast motion
    connect(this->lx200port,SIGNAL(RS232sync()),this,SLOT(LXsyncMount()),Qt::QueuedConnection); // LX 200 sync
    connect(this->lx200port,SIGNAL(RS232slew()),this,SLOT(LXslewMount()),Qt::QueuedConnection); // LX 200 slew
    connect(this->lx200port,SIGNAL(RS232CommandReceived()),this, SLOT(logLX200IncomingCmds()),Qt::QueuedConnection); // write incoming command from LX 200 to log
    connect(this->lx200port,SIGNAL(logRASent()),this, SLOT(logLX200OutgoingCmdsRA()),Qt::QueuedConnection); // receive RA from LX 200 and log it
    connect(this->lx200port,SIGNAL(logDeclSent()),this, SLOT(logLX200OutgoingCmdsDecl()),Qt::QueuedConnection); // receive decl from LX 200 and log it
    connect(this->lx200port,SIGNAL(logCommandSent()),this, SLOT(logLX200OutgoingCmds()),Qt::QueuedConnection); // write outgoing command from LX 200 to log
    connect(this->lx200port,SIGNAL(polarAlignmentSignal()), this, SLOT(sendPolarAlignmentCommand()),Qt::QueuedConnection); // send a "P#" upon establishing conntact via classic LX200 over the TCP/IP socket ...
    connect(this->lx200port,SIGNAL(clientRASent(QString*)), this, SLOT(handleRAviaTCP(QString*)),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(clientDeclSent(QString*)), this, SLOT(handleDeclviaTCP(QString*)),Qt::QueuedConnection);
    connect(this->lx200port,SIGNAL(clientCommandSent(QString*)), this, SLOT(handleCommandviaTCP(QString*)),Qt::QueuedConnection);
    connect(this->camView,SIGNAL(currentViewStatusSignal(QPointF)),this->camView,SLOT(currentViewStatusSlot(QPointF)),Qt::QueuedConnection); // position the crosshair in the camera view by mouse...
    connect(this->guiding,SIGNAL(determinedGuideStarCentroid()), this->camView,SLOT(currentViewStatusSlot()),Qt::QueuedConnection); // an overload of the precious slot that allows for positioning the crosshair after a centroid was computed during guiding...
    connect(this->camera_client,SIGNAL(imageAvailable(QPixmap*)),this,SLOT(displayGuideCamImage(QPixmap*)),Qt::QueuedConnection); // display image from ccd if one was received from INDI; also takes care of autoguiding. triggered by signal
    connect(this->camera_client,SIGNAL(messageFromINDIAvailable()),this,SLOT(handleServerMessage()),Qt::QueuedConnection); // display messages from INDI if signal was received
    connect(this->guiding,SIGNAL(guideImagePreviewAvailable()),this,SLOT(displayGuideStarPreview()),Qt::QueuedConnection); // handle preview of the processed guidestar image
    connect(this->bt_Handbox,SIGNAL(btDataReceived()),this,SLOT(handleBTHandbox()),Qt::QueuedConnection); // handle data coming from the bluetooth handbox
    connect(this->LXServer,SIGNAL(newConnection()),this,SLOT(establishLX200IPLink()),Qt::QueuedConnection); // establish a link vian LAN/WLAN to a planetarium program via TCP/IP
    ui->rbV4L2INDI->setChecked(true); // set a default type of INDI server
    this->killRunningINDIServer(); // find out about running INDI servers and kill them
    this->StepperDriveRA->stopDrive();
    this->StepperDriveDecl->stopDrive(); // just to kill all jobs that may lurk in the muproc ...
}
//------------------------------------------------------------------
// destructor - hopefully kills all local and global instances
MainWindow::~MainWindow() {
    this->shutDownProgram();
    delete ui;
    exit(0);
}

//------------------------------------------------------------------
// the main event queue, triggered by this->timer
void MainWindow::updateReadings() {
    qint64 topicalTime; // g_AllData contains an monotonic global timer that is reset if a sync occcurs
    double relativeTravelRA, relativeTravelDecl,totalGearRatio, hourAngleForDisplay; // a few helpers

    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    this->updateTimeAndDate();
    if (this->bt_Handbox->getPortState() == true) { // check rfcomm0 for data from the handbox
        this->bt_Handbox->getDataFromSerialPort();
    }
    if (this->dslrStates.dslrExposureIsRunning == true) { // check a timer and update display of the remaining time ...
        this->updateDSLRGUIAndCountdown();
    }
    if (this->mountMotion.RATrackingIsOn == true) { // standard mode - mount compensates for earth motion
        topicalTime = g_AllData->getTimeSinceLastSync() - this->mountMotion.RAtrackingElapsedTimeInMS; // check the monotonic timer
        this->mountMotion.RAtrackingElapsedTimeInMS+=topicalTime; // total time elapsed in tracking mode
        totalGearRatio = g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2); // gear ratio in RA
        relativeTravelRA= this->StepperDriveRA->getKineticsFromController(3)*topicalTime*g_AllData->getGearData(3)/
                (1000.0*g_AllData->getGearData(8)*totalGearRatio); // compute travel in decimal degrees
        g_AllData->incrementActualScopePosition(relativeTravelRA, 0.0); // update position in global struct on RA
    }
    if (this->mountMotion.RADriveIsMoving == true) { // mount moves at non-sidereal rate - but not in GOTO
        topicalTime = g_AllData->getTimeSinceLastSync() - this->mountMotion.RAMoveElapsedTimeInMS;
        this->mountMotion.RAMoveElapsedTimeInMS+=topicalTime;
        totalGearRatio = g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2);
        relativeTravelRA=this->mountMotion.RADriveDirection*
                this->StepperDriveRA->getKineticsFromController(3)*topicalTime*g_AllData->getGearData(3)/
                (1000.0*g_AllData->getGearData(8)*totalGearRatio);
        g_AllData->incrementActualScopePosition(relativeTravelRA, 0.0);  // same as above - compute travel in decimal degrees and update it
        if (this->StepperDriveRA->hasHBoxSlewEnded() == true) {
            // this is a little bit strange; handboxslew is 180 degrees maximum. if this occurs,
            // it has to be handled like pressing the stop button
            this->mountMotion.RADriveIsMoving = false;
            while (!futureStepperBehaviourRA.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            if (this->mountMotion.RATrackingIsOn == false) {
                this->setControlsForRATravel(true);
            }
            this->startRATracking();
            ui->pbRAMinus->setEnabled(1);
            ui->pbRAPlus->setEnabled(1);
            ui->rbCorrSpeed->setEnabled(true);
            ui->rbMoveSpeed->setEnabled(true);
            ui->pbPGRAMinus->setEnabled(true);
            ui->pbPGRAPlus->setEnabled(true);
            ui->pbPGDecMinus->setEnabled(true);
            ui->pbPGDecPlus->setEnabled(true);
            if (ui->rbMoveSpeed->isChecked()==false) {
                ui->sbMoveSpeed->setEnabled(true);
            }
        } // a lot of code for an unlikely situation. after 180 degrees, the mount simply resumes tracking
    }

    if (this->mountMotion.DeclDriveIsMoving == true) { // now, the declination drive is also active; it does not track, therefore we have a copy of the above section, more or less
        topicalTime = g_AllData->getTimeSinceLastSync() - this->mountMotion.DeclMoveElapsedTimeInMS;
        this->mountMotion.DeclMoveElapsedTimeInMS+=topicalTime;
        totalGearRatio = g_AllData->getGearData(4)*g_AllData->getGearData(5)*g_AllData->getGearData(6);
        relativeTravelDecl= this->mountMotion.DeclDriveDirection*
                this->StepperDriveDecl->getKineticsFromController(3)*topicalTime*g_AllData->getGearData(7)/
                (1000.0*g_AllData->getGearData(8)*totalGearRatio);
        g_AllData->incrementActualScopePosition(0.0, relativeTravelDecl); // update the declination position
        if (this->StepperDriveDecl->hasHBoxSlewEnded() == true) {
            // same as above; end of handbox slew of 180 degrees has to be handled like pressing a stop button
            this->mountMotion.DeclDriveIsMoving = false;
            while (!futureStepperBehaviourDecl.isFinished()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            ui->pbPGDecMinus->setEnabled(true);
            ui->pbPGDecPlus->setEnabled(true);
            ui->pbPGRAMinus->setEnabled(true);
            ui->pbPGRAPlus->setEnabled(true);
            ui->pbDeclUp->setEnabled(true);
            ui->pbDeclDown->setEnabled(true);
            this->setControlsForDeclTravel(true);
            if (ui->rbMoveSpeed->isChecked()==false) {
                ui->sbMoveSpeed->setEnabled(true);
            } else {
                ui->sbMoveSpeed->setEnabled(false);
            }
        } // after 180 degrees, the declination travel simply stops
    }

    if ((this->mountMotion.GoToIsActiveInRA==true) || (this->mountMotion.GoToIsActiveInDecl==true)) { // the mount is slewing. slew is not complete as either RA or decl are in slew mode - or both
        ui->lcdGotoTime->display(round((this->gotoETA-this->elapsedGoToTime->elapsed())*0.001));
        if (this->mountMotion.GoToIsActiveInRA==true) {
            topicalTime = g_AllData->getTimeSinceLastSync() - this->mountMotion.RAGoToElapsedTimeInMS;
            this->mountMotion.RAGoToElapsedTimeInMS+=topicalTime;
            totalGearRatio = g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2);
            relativeTravelRA=this->mountMotion.RADriveDirection*
                    this->approximateGOTOSpeedRA*topicalTime*g_AllData->getGearData(3)/
                    (1000.0*g_AllData->getGearData(8)*totalGearRatio);
            g_AllData->incrementActualScopePosition(relativeTravelRA, 0.0);
        }
        if (this->mountMotion.GoToIsActiveInDecl==true) {
            topicalTime = g_AllData->getTimeSinceLastSync() - this->mountMotion.DeclGoToElapsedTimeInMS;
            this->mountMotion.DeclGoToElapsedTimeInMS+=topicalTime;
            totalGearRatio = g_AllData->getGearData(4)*g_AllData->getGearData(5)*g_AllData->getGearData(6);
            relativeTravelDecl= this->mountMotion.DeclDriveDirection*
                    this->approximateGOTOSpeedDecl*topicalTime*g_AllData->getGearData(7)/
                    (1000.0*g_AllData->getGearData(8)*totalGearRatio);
            g_AllData->incrementActualScopePosition(0.0, relativeTravelDecl);
        }
    } else {
        if ((this->mountMotion.RATrackingIsOn == false) &&
                (this->mountMotion.RADriveIsMoving == false) &&
                (this->mountMotion.DeclDriveIsMoving == false)) {
                    g_AllData->incrementActualScopePosition(0.0,0.0);
                    // the scope is at rest, but the hour angle still
                    // needs to be updated ...
        }
    } // slew has ended ...

//    ui->leHourAngle->setText(textEntry->number(g_AllData->getActualScopePosition(0),'f',5));
    hourAngleForDisplay=(g_AllData->getLocalSTime()*15 - g_AllData->getActualScopePosition(2));
    while (hourAngleForDisplay < 0) {
        hourAngleForDisplay += 360;
    }
    while (hourAngleForDisplay > 360) {
        hourAngleForDisplay -= 360;
    }
    ui->leHourAngle->setText(textEntry->number(hourAngleForDisplay,'f',5));
    ui->leDecl->setText(textEntry->number(g_AllData->getActualScopePosition(1),'f',5));
    // finally, the actual scope position is updated in the GUI
}

//------------------------------------------------------------------------
// routine for handling date and time; also computes julian day and local sidereal
// time
void MainWindow::updateTimeAndDate(void) {
    double secSinceMidnight, lstX, lst;

    ui->leTime->setText(this->UTTime->currentTime().toString());
    ui->leDate->setText(this->UTDate->currentDate().toString("dd/MM/yyyy"));
    this->julianDay = this->UTDate->toJulianDay()-0.5;
    ui->teJulianDate->setText(QString::number(long(this->julianDay)));
    secSinceMidnight=UTTime->currentTime().hour()*3600.0+UTTime->currentTime().minute()*60.0+UTTime->currentTime().second()+UTTime->currentTime().msec()/1000.0;
    lstX=(this->julianDay-2451545.0)/36525.0;
    lst=6.697374558 + 2400.051336*lstX + 0.000025862*lstX*lstX + 1.00273791*(secSinceMidnight/3600.0) + g_AllData->getSiteCoords(1)/15.0;
    lst=lst-(int(lst/24))*24.0;
    ui->teLSTime->setText(QString::number(lst,'f',5));
    g_AllData->setLocalSTime(lst); // store the sidereal time also in the global data class ...
}

//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
// routines for basic stepper operation
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
// the most important routine - is compensates for earth motion, sets all flags and disables all GUI elements that can interefere
void MainWindow::startRATracking(void) {
    ui->rbCorrSpeed->setEnabled(true);
    ui->rbMoveSpeed->setEnabled(true);
    if (ui->rbMoveSpeed->isChecked()==false) {
        ui->sbMoveSpeed->setEnabled(true);
    }
    this->StepperDriveRA->stopDrive();
    this->mountMotion.RATrackingIsOn = true;
    ui->pbStartTracking->setEnabled(0);
    ui->pbStopTracking->setEnabled(1);
    this->mountMotion.RAtrackingElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
    this->futureStepperBehaviourRATracking=QtConcurrent::run(this->StepperDriveRA, &QtContinuousStepper::startTracking);
    this->setControlsForRATracking(false);
    g_AllData->setTrackingMode(true);
}

//------------------------------------------------------------------
// stops earthtracking. has to be called prior to all other commands issued to the RA-stepper
void MainWindow::stopRATracking(void) {
    this->setControlsForRATracking(true);
    ui->pbStartTracking->setEnabled(1);
    ui->pbStopTracking->setEnabled(0);
    this->StepperDriveRA->stopDrive();
    while (!this->futureStepperBehaviourRATracking.isFinished()) {
    } // wait till the RA-tracking thread has died ...
    this->mountMotion.RATrackingIsOn = false;
    g_AllData->setTrackingMode(false);
}

//------------------------------------------------------------------
// synchronizes the mount to given coordinates and sets the monotonic timer to zero
void MainWindow::syncMount(void) {
    if (this->StepperDriveRA->getStopped() == false) { // stop tracking
        this->stopRATracking();
    }
    if (this->mountMotion.DeclDriveIsMoving == true) {
        this->mountMotion.DeclDriveIsMoving=false;
        this->StepperDriveDecl->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    } // stop the declination drive as well ...
    g_AllData->setSyncPosition(this->ra, this->decl);
    // convey right ascension and declination to the global parameters;
    // a microtimer starts ...
    this->startRATracking(); // start tracking again
    ui->pbGoTo->setEnabled(true); // enable GOTO as we now have a reference position
}
//---------------------------------------------------------------------
// that one handles GOTO-commands. it leaves when the destination is reached ...
void MainWindow::startGoToObject(void) {
    double travelRA, travelDecl, absShortRATravel, speedFactorRA, speedFactorDecl,TRamp, SRamp,
           SAtFullSpeed, TAtFullSpeed, earthTravelDuringGOTOinMSteps,
           convertDegreesToMicrostepsDecl,convertDegreesToMicrostepsRA; // variables for assessing travel time and so on
    float targetRA, targetDecl; // desired ra and decl in decimal degrees
    qint64 timestampGOTOStarted, timeDifference, timeTaken; // various time stamps
    qint64 timeEstimatedInRAInMS = 0; // estimate for travel time in RA [ms]
    qint64 timeEstimatedInDeclInMS = 0; // estimate for travel time in Decl [ms]
    long int RASteps, DeclSteps,corrsteps; // microsteps for travel plus a correction measure
    int timeForProcessingEventQueue = 100; // should be the same as the time for the event queue given in this->timer
    bool RAtakesLonger, shortSlew; // two flags. RAtakes longer = true: RAtravel longer than Decl travel. short sles of a few seconds are carried out one after another to avaid timing problems
    bool RARideIsDone; // a flag set to true when slew in RA is done
    QElapsedTimer *waitState;

    waitState = new QElapsedTimer(); // a timer to give the muproc a little time to breathe ...
    ui->pbGoTo->setEnabled(false); // disable pushbutton for GOTO
    this->setControlsForGoto(false); // set some controls on disabled
    ui->pbStartTracking->setEnabled(false); // tracking button is disabled
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    shortSlew=false; // we do not know how long the slew takes, so this flag is false
    timeDifference=0; // difference between estimated travel and real travel in RA - needed for correction after slew
        // determine the travel to be taken based on steps, acceleration and end velocity
    travelRA=((g_AllData->getActualScopePosition(0))+g_AllData->getCelestialSpeed()*g_AllData->getTimeSinceLastSync()/1000.0)-this->ra;
    if (fabs(travelRA) > 180) {
        absShortRATravel = 360.0 - fabs(travelRA);
        if (travelRA > 0) {
            travelRA = -absShortRATravel;
        } else {
            travelRA = absShortRATravel;
        }
    } // determine the shorter travel path
    travelDecl=this->decl-g_AllData->getActualScopePosition(1); // travel in both axes based on current position
    targetRA = this->ra;
    targetDecl = this->decl; // destination as given by LX200 or the menu of TSC
    if (travelRA < 0) {
        this->mountMotion.RADriveDirection = -1;
    } else {
        this->mountMotion.RADriveDirection = 1;
    } // determine direction in RA
    if (travelDecl < 0) {
        this->mountMotion.DeclDriveDirection = -1;
    } else {
        this->mountMotion.DeclDriveDirection = 1;
    } // determine direction in declination
    speedFactorDecl=ui->sbGoToSpeed->value();
    speedFactorRA=ui->sbGoToSpeed->value(); // set the drive speed to GOTO speed according to spinbox in GUI
    convertDegreesToMicrostepsDecl=1.0/g_AllData->getGearData(7)*g_AllData->getGearData(8)*
            g_AllData->getGearData(4)*g_AllData->getGearData(5)*g_AllData->getGearData(6);
    DeclSteps=round(fabs(travelDecl)*convertDegreesToMicrostepsDecl); // determine the number of microsteps necessary to reach target. direction is already given and unimportant here ...
    convertDegreesToMicrostepsRA=1.0/g_AllData->getGearData(3)*g_AllData->getGearData(8)*
            g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2);
    RASteps=round(fabs(travelRA)*convertDegreesToMicrostepsRA); // determine the number of microsteps necessary to reach target. direction is already given and unimportant here ...
    TRamp = (this->StepperDriveDecl->getKineticsFromController(3)*(speedFactorDecl))/this->StepperDriveDecl->getKineticsFromController(2);// time needed until drive reaches full speed - vel/acc ...
    SRamp = 0.5*this->StepperDriveDecl->getKineticsFromController(2)*TRamp*TRamp; // travel in microsteps until full speed is reached
    SAtFullSpeed = DeclSteps-2.0*SRamp; // travel after acceleration and before de-acceleration
    if (SAtFullSpeed < 0) {
        TAtFullSpeed=sqrt(DeclSteps/this->StepperDriveDecl->getKineticsFromController(2));// if the travel is so short that full speed cannot be reached: consider a ramp that stops at the end of travel
        timeEstimatedInDeclInMS = (TAtFullSpeed)*1000+timeForProcessingEventQueue; // time in microseconds estimated for Declination-Travel
    } else {
        TAtFullSpeed = SAtFullSpeed/(this->StepperDriveDecl->getKineticsFromController(3)*speedFactorDecl);
        timeEstimatedInDeclInMS = (TAtFullSpeed+2.0*TRamp)*1000+timeForProcessingEventQueue; // time in microseconds estimated for Declination-Travel
    }

        // Now repeat that computation for the RA drive
    TRamp = (this->StepperDriveRA->getKineticsFromController(3)*(speedFactorRA))/this->StepperDriveRA->getKineticsFromController(2);
    SRamp = 0.5*this->StepperDriveRA->getKineticsFromController(2)*TRamp*TRamp;
    SAtFullSpeed = RASteps-2.0*SRamp;
    if (SAtFullSpeed < 0) {
        TAtFullSpeed=sqrt(RASteps/this->StepperDriveRA->getKineticsFromController(2));
        timeEstimatedInRAInMS = (TAtFullSpeed)*1000+timeForProcessingEventQueue;
    } else {
        TAtFullSpeed = SAtFullSpeed/(this->StepperDriveRA->getKineticsFromController(3)*speedFactorRA);
        timeEstimatedInRAInMS = (TAtFullSpeed+2.0*TRamp)*1000+timeForProcessingEventQueue;
    }

    earthTravelDuringGOTOinMSteps=(g_AllData->getCelestialSpeed()*((double)timeEstimatedInRAInMS)/1000.0)*
            convertDegreesToMicrostepsRA; // determine the addition of earth travel in sideral time into account

        // compensate for earth travel in fwd or bwd direction
    if (this->mountMotion.RADriveDirection == 1) {
        RASteps=RASteps+earthTravelDuringGOTOinMSteps;
    } else {
        RASteps=RASteps-earthTravelDuringGOTOinMSteps;
    }
    timeEstimatedInRAInMS = RASteps/((double)this->StepperDriveRA->getKineticsFromController(3)*(speedFactorRA))*1000; // correct RA travel time for this additional motion

        // find out which drive is longer in GOTO mode
    if (timeEstimatedInDeclInMS > timeEstimatedInRAInMS) {
        gotoETA = timeEstimatedInDeclInMS; // time for total slew
        RAtakesLonger=false;
    } else {
        gotoETA = timeEstimatedInRAInMS; // time for total slew
        RAtakesLonger=true;
    }
        // if one of the travels is less than 5 seconds - carry the travel out one after another to avoid timing issues
    if ((timeEstimatedInDeclInMS < 5000) || (timeEstimatedInRAInMS < 5000)) {
        gotoETA = timeEstimatedInDeclInMS+timeEstimatedInRAInMS; // time for total slew
        shortSlew=true; // in this case,
    }

    this->approximateGOTOSpeedRA=RASteps/(timeEstimatedInRAInMS/1000.0)*0.95; // for LX 200 display, a mean speed during GOTO not taking ramps into account is computed; it is shortened to avoid overshooting (in graphical display)
    this->approximateGOTOSpeedDecl=DeclSteps/(timeEstimatedInDeclInMS/1000.0)*0.95; // same as above
    gotoETA *= 1.05; // just overerestimate the time in order to avoid negative times ...
    ui->lcdGotoTime->display(round(gotoETA/1000.0)); // determined the estimated duration of the GoTo - Process and display it in the GUI. it is reduced in the event queue
    QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue); // just make sure that events are processed ...

        // let the games begin ... GOTO is ready to start ...
    RARideIsDone = false; // RA slew not finished yet ...
    this->terminateAllMotion(); // stop the drives
    this->elapsedGoToTime->start(); // a second timer in the class to measure the time elapsed during goto - needed for updates in the event queue
    if (shortSlew == true) { // is the target is nearby, carry out the slews one after another ...
        this->startRATracking(); // RA still has to track while decl slews here ...
        ui->pbStopTracking->setEnabled(false);
        if (DeclSteps > 50) {
            futureStepperBehaviourDecl_GOTO =QtConcurrent::run(this->StepperDriveDecl,&QtKineticStepper::travelForNSteps,
                                                            DeclSteps,this->mountMotion.DeclDriveDirection,speedFactorDecl,0);
            while (!futureStepperBehaviourDecl.isStarted()) { // wait for thread to start
            }
        }
        this->mountMotion.GoToIsActiveInDecl=true;
        timestampGOTOStarted = g_AllData->getTimeSinceLastSync(); // set a global timestamp
        this->mountMotion.DeclGoToElapsedTimeInMS=g_AllData->getTimeSinceLastSync();
        waitState->start();
        do {
        } while (waitState->elapsed() < 100);
        while (!futureStepperBehaviourDecl_GOTO.isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
            if (this->mountMotion.emergencyStopTriggered==true) { // if the emergency button is pressed, terminate routine immediately
                this->mountMotion.emergencyStopTriggered=false;
                this->mountMotion.GoToIsActiveInRA=false;
                this->mountMotion.GoToIsActiveInDecl=false;
                return;
            }
        }
        this->mountMotion.GoToIsActiveInDecl=false; // goto in Decl is now done, so start travel in RA. set also the flag for decl-goto ...
        waitState->start();
        do {
        } while (waitState->elapsed() < 100);
        this->stopRATracking(); // now, the decl slew is done and slewing in RA starts - therefore tracking is stopped
        ui->pbStartTracking->setEnabled(false);
        if (RASteps > 50) {
            futureStepperBehaviourRA_GOTO =QtConcurrent::run(this->StepperDriveRA,&QtContinuousStepper::travelForNSteps,RASteps,this->mountMotion.RADriveDirection,speedFactorRA,false);
            while (!futureStepperBehaviourRA.isStarted()) { // wait for thread to start
            }
        }
        this->mountMotion.GoToIsActiveInRA=true;
        this->mountMotion.RAGoToElapsedTimeInMS=g_AllData->getTimeSinceLastSync(); // set a global timestamp
        waitState->start();
        do {
        } while (waitState->elapsed() < 100);
        while (!futureStepperBehaviourRA_GOTO.isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
            if (this->mountMotion.emergencyStopTriggered==true) { // if the emergency button is pressed, terminate routine immediately
                this->mountMotion.emergencyStopTriggered=false;
                this->mountMotion.GoToIsActiveInRA=false; // flag on RA-GOTO set to false - important for event queue
                this->mountMotion.GoToIsActiveInDecl=false;
                return;
            }
        } // RA travel is done here
        timeTaken = g_AllData->getTimeSinceLastSync()-timestampGOTOStarted; // determine real time of GOTO according to monotonic timer
        timeDifference = timeTaken-timeEstimatedInRAInMS; // determine the difference between estimated and real travel
        this->startRATracking(); // start earth motion compensation
        ui->pbStopTracking->setDisabled(true); // set GUI to tracking operation
        this->mountMotion.GoToIsActiveInRA=false; // flag on RA-GOTO set to false - important for event queue
    } else { // carry out the slews parallel
        futureStepperBehaviourRA_GOTO =QtConcurrent::run(this->StepperDriveRA,&QtContinuousStepper::travelForNSteps,RASteps,this->mountMotion.RADriveDirection,speedFactorRA,false);
        while (!futureStepperBehaviourRA.isStarted()) {
        }
        this->mountMotion.GoToIsActiveInRA=true;
        this->mountMotion.RAGoToElapsedTimeInMS=g_AllData->getTimeSinceLastSync();
        timestampGOTOStarted = g_AllData->getTimeSinceLastSync();
        ui->pbStartTracking->setEnabled(false);
        waitState->start();
        do {
        } while (waitState->elapsed() < 100);
        futureStepperBehaviourDecl_GOTO =QtConcurrent::run(this->StepperDriveDecl,&QtKineticStepper::travelForNSteps,DeclSteps,this->mountMotion.DeclDriveDirection,speedFactorDecl,0);
        while (!futureStepperBehaviourDecl.isStarted()) {
        }
        this->mountMotion.GoToIsActiveInDecl=true;
        this->mountMotion.DeclGoToElapsedTimeInMS=g_AllData->getTimeSinceLastSync(); // now, all drives are started and timestamps were taken
        waitState->start();
        do {
        } while (waitState->elapsed() < 100);
        if (RAtakesLonger == true) { // then decl will finish earlier
            while (!futureStepperBehaviourRA_GOTO.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
                if (futureStepperBehaviourDecl_GOTO.isFinished()) {
                    this->mountMotion.GoToIsActiveInDecl=false;
                } // declination has finished here, flag is set to false
                if (this->mountMotion.emergencyStopTriggered==true) { // if the emergency button is pressed, terminate routine immediately
                    this->mountMotion.emergencyStopTriggered=false;
                    this->mountMotion.GoToIsActiveInRA=false;
                    this->mountMotion.GoToIsActiveInDecl=false;
                    return;
                }
            } // now, RA is done as well
            waitState->start();
            do {
            } while (waitState->elapsed() < 100);
            timeTaken = g_AllData->getTimeSinceLastSync()-timestampGOTOStarted;
            timeDifference = timeTaken-timeEstimatedInRAInMS; // compute difference between real and estimated travel
            this->startRATracking();
            ui->pbStopTracking->setDisabled(true);
            this->mountMotion.GoToIsActiveInRA=false; // resume standard tracking
        } else { // RA will finish earlier
            while (!futureStepperBehaviourDecl_GOTO.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
                if (futureStepperBehaviourRA_GOTO.isFinished()) { // ra is finished, decl still active ...
                    this->mountMotion.GoToIsActiveInRA=false;
                    if (RARideIsDone==false) { // ok - if RA is finished FOR THE FIRST TIME ...
                        waitState->start();
                        do {
                        } while (waitState->elapsed() < 100);
                        RARideIsDone=true;
                        timeTaken = g_AllData->getTimeSinceLastSync()-timestampGOTOStarted;
                        timeDifference = timeTaken-timeEstimatedInRAInMS;
                        this->startRATracking();
                        ui->pbStopTracking->setDisabled(true); // ... take care of time stamps and tracking while decl is further active
                    }
                    if (this->mountMotion.emergencyStopTriggered==true) { // emergency stop handling
                        this->mountMotion.emergencyStopTriggered=false;
                        this->mountMotion.GoToIsActiveInRA=false;
                        this->mountMotion.GoToIsActiveInDecl=false;
                        return;
                    }
                }
            }
            if (this->mountMotion.emergencyStopTriggered==true) { // emergency stop handling
                this->mountMotion.emergencyStopTriggered=false;
                this->mountMotion.GoToIsActiveInRA=false;
                this->mountMotion.GoToIsActiveInDecl=false;
                return;
            }
            this->mountMotion.GoToIsActiveInDecl=false; // declination is now done
        }
    }
    waitState->start();
    do {
    } while (waitState->elapsed() < 100); // just a little rest
    this->stopRATracking(); // stop tracking, either for correction of for sync
    if (abs(timeDifference)>100) { // if the error in goto time estimation is bigger than 0.1 s, correcct in RA
        corrsteps=(g_AllData->getCelestialSpeed()*((double)(timeDifference))/1000.0)*convertDegreesToMicrostepsRA; // number of correction steps
        futureStepperBehaviourRA_Corr = QtConcurrent::run(this->StepperDriveRA,
                &QtContinuousStepper::travelForNSteps,corrsteps, 1,10,false);
        while (!futureStepperBehaviourRA_Corr.isFinished()) {
           QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
        }
        if (this->mountMotion.emergencyStopTriggered==true) { // emergency stop handling
            this->mountMotion.emergencyStopTriggered=false;
            this->mountMotion.GoToIsActiveInRA=false;
            this->mountMotion.GoToIsActiveInDecl=false;
            return;
        }
    } // correction is now done as well
    this->ra=targetRA;
    this->decl=targetDecl;
    this->syncMount(); // sync the mount
    while (!futureStepperBehaviourRATracking.isStarted()) {
       QCoreApplication::processEvents(QEventLoop::AllEvents, timeForProcessingEventQueue);
    }
    ui->lcdGotoTime->display(0); // set the LCD counter to zero again
    ui->pbStopTracking->setDisabled(false);
    this->setControlsForGoto(true);
    this->setControlsForRATravel(true); // set GUI back in base state
    this->setControlsForRATracking(false);
    this->mountMotion.GoToIsActiveInRA=false;
    this->mountMotion.GoToIsActiveInDecl=false; // just to make sure - slew has ENDED here ...
    delete waitState;
    return;
}

//------------------------------------------------------------------
// handles shutdown of the program
void MainWindow::shutDownProgram() {
    this->ccdCameraIsAcquiring=false;
    sleep(ui->sbExposureTime->value());
    camera_client->sayGoodbyeToINDIServer();
    delete camera_client;
    this->bt_Handbox->shutDownPort();
    this->StepperDriveRA->stopDrive();
    delete StepperDriveRA;
    this->StepperDriveDecl->stopDrive();
    delete StepperDriveDecl;
    delete timer;
    delete textEntry;
    delete bt_HandboxCommand;
    delete lx200port;
    delete g_AllData;
    delete camImg;
    delete guideStarPrev;
    delete bt_Handbox;
    delete LXServer;
    delete LXSocket;
    delete LXServerAddress;
    delete tcpLXdata;
    delete UTDate;
    delete UTTime;
    delete lx200SerialPort;
    delete lx200SerialData;
    exit(0);
}

//---------------------------------------------------------------------
// soft stop for drives used in GOTO
void MainWindow::terminateAllMotion(void) {
    if (this->mountMotion.RADriveIsMoving == true) {
        this->mountMotion.RADriveIsMoving=false;
        this->StepperDriveRA->stopDrive();
        while (!futureStepperBehaviourRA.isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    }
    if (this->mountMotion.DeclDriveIsMoving == true) {
        this->mountMotion.DeclDriveIsMoving=false;
        this->StepperDriveDecl->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    }
    if (this->mountMotion.RATrackingIsOn == true) {
        this->stopRATracking();
    }
}

//---------------------------------------------------------------------
// emergency stop of all motion
void MainWindow::emergencyStop(void) {
    this->mountMotion.emergencyStopTriggered=true;
    this->StepperDriveRA->stopDrive();
    this->StepperDriveDecl->stopDrive();
    this->mountMotion.RATrackingIsOn = false;
    this->mountMotion.RADriveIsMoving = false;
    this->mountMotion.DeclDriveIsMoving = false;
    this->mountMotion.GoToIsActiveInRA = false;
    this->mountMotion.GoToIsActiveInDecl = false;
    while (!futureStepperBehaviourRATracking.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourRA.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourDecl.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourRA_GOTO.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourDecl_GOTO.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourRA_Corr.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    while (!futureStepperBehaviourDecl_Corr.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    ui->pbStartTracking->setEnabled(true);
    ui->pbStopTracking->setEnabled(false);
    this->setControlsForGoto(true);
    ui->lcdGotoTime->display(0);
    ui->pbGoTo->setEnabled(true);
    ui->ctrlTab->setEnabled(true);
}
//---------------------------------------------------------------------
// slot for changing drive speeds
void MainWindow::setCorrectionSpeed(void) {
    this->mountMotion.RASpeedFactor = 1;
    this->mountMotion.DeclSpeedFactor = 1;
    ui->sbMoveSpeed->setEnabled(true);
}

//---------------------------------------------------------------------
// slot for changing drive speeds
void MainWindow::setMoveSpeed(void) {
    this->mountMotion.RASpeedFactor = ui->sbMoveSpeed->value();
    this->mountMotion.DeclSpeedFactor = ui->sbMoveSpeed->value();
    ui->sbMoveSpeed->setEnabled(false);
}
//---------------------------------------------------------------------
// slot for changing drive speeds
void MainWindow::changeMoveSpeed(void) {
    this->mountMotion.RASpeedFactor = ui->sbMoveSpeed->value();
    this->mountMotion.DeclSpeedFactor = ui->sbMoveSpeed->value();
    ui->rbMoveSpeed->setChecked(true);
}

//---------------------------------------------------------------------
// invert direction for RA if on souther hermisphere - is a slot
void MainWindow::invertRADirection(void) {
    if (ui->cbIsOnNorthernHemisphere->isChecked() == true) {
        this->RAdriveDirectionForNorthernHemisphere = 1;
    } else {
        this->RAdriveDirectionForNorthernHemisphere = -1;
    }
    this->StepperDriveRA->setRADirection(this->RAdriveDirectionForNorthernHemisphere);
}

//------------------------------------------------------------------
// convey acceleration to the stepper class from the GUI
void MainWindow::setMaxStepperAccRA(void) {
    double val;
    QString *leEntry;
    bool trackingWasOn;

    trackingWasOn = this->mountMotion.RATrackingIsOn;
    if (this->mountMotion.RATrackingIsOn == true) {
        this->stopRATracking();
    }
    leEntry = new QString(ui->leAMaxRA->text());
    val = (double)(leEntry->toFloat());
    this->StepperDriveRA->setStepperParams(val, 1);
    if (trackingWasOn == true) {
        this->startRATracking();
    }
    g_AllData->setDriveParams(0,1,val);
    delete leEntry;
}

//------------------------------------------------------------------
// convey acceleration to the stepper class from the GUI
void MainWindow::setMaxStepperAccDecl(void) {
    double val;
    QString *leEntry;

    leEntry = new QString(ui->leAMaxDecl->text());
    val = (double)(leEntry->toFloat());
    this->StepperDriveDecl->setStepperParams(val, 1);
    g_AllData->setDriveParams(1,1,val);
    delete leEntry;
}

//------------------------------------------------------------------
// convey current to the stepper class from the GUI
void MainWindow::setMaxStepperCurrentRA(void) {
    double val;
    QString *leEntry;
    bool trackingWasOn;

    trackingWasOn = this->mountMotion.RATrackingIsOn;
    if (this->mountMotion.RATrackingIsOn == true) {
        this->stopRATracking();
    }
    leEntry = new QString(ui->leCurrMaxRA->text());
    val = (double)(leEntry->toFloat());
    this->StepperDriveRA->setStepperParams(val, 3);
    if (trackingWasOn == true) {
        this->startRATracking();
    }
    g_AllData->setDriveParams(0,2,val);
    delete leEntry;
}
//------------------------------------------------------------------
// convey current to the stepper class from the GUI
void MainWindow::setMaxStepperCurrentDecl(void) {
    double val;
    QString *leEntry;

    leEntry = new QString(ui->leCurrMaxDecl->text());
    val = (double)(leEntry->toFloat());
    this->StepperDriveDecl->setStepperParams(val, 3);
    g_AllData->setDriveParams(1,2,val);
    delete leEntry;
}

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// routines for communication with the INDI server
//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// read the address and port from the GUI and start connect the camera to the INDI server
void MainWindow::setINDISAddrAndPort(void) {
    QString saddr;
    int sport, gainVal;
    bool isServerUp = 0;

    saddr=ui->leINDIServer->text();
    sport=ui->sbINDIPort->value();
    isServerUp = camera_client->setINDIServer(saddr,sport);
    g_AllData->setINDIState(isServerUp);
    // set a global flag on the server state
    if (isServerUp==true) {
        ui->gbStartINDI->setEnabled(false);
        ui->pbExpose->setEnabled(true);
        ui->cbIndiIsUp->setChecked(true);
        ui->cbStoreGuideCamImgs->setEnabled(true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);   // process events before sleeping for a second
        sleep(1);
        this->getCCDParameters();
        storeCCDData();
        gainVal=ui->sbCCDGain->value();
        camera_client->sendGain(gainVal);
        ui->pbConnectToServer->setEnabled(false);
        ui->pbDisconnectFromServer->setEnabled(true);
    }
}

//------------------------------------------------------------------
// disconnect from INDI server
void MainWindow::disconnectFromINDIServer(void) {
    if (this->ccdCameraIsAcquiring==false) {
        this->camera_client->disconnectFromServer();
        ui->pbConnectToServer->setEnabled(true);
        ui->pbDisconnectFromServer->setEnabled(false);
        ui->cbIndiIsUp->setChecked(false);
        ui->pbExpose->setEnabled(false);
        ui->pbStopExposure->setEnabled(false);
        ui->gbStartINDI->setEnabled(true);
    }
}

//------------------------------------------------------------------
// a private little function that tries to find out about the PID of the
// INDI-server; the system call logs the process ID in a file .INDIpid.tsl
void MainWindow::findOutAboutINDIServerPID(void) {
    system("pidof indiserver > .INDIPID.tsl &");
    sleep(1);
}

//------------------------------------------------------------------
// this one kills a running INDI-server
void MainWindow::killRunningINDIServer(void) {
    QString *myCommand;

    std::ifstream infile(".INDIPID.tsl");
    std::string line;   // define a line that is read
    std::getline(infile, line);     // read that line
    if (line.length() > 0) {
        myCommand=new QString("kill -9 ");
        myCommand->append(line.data());
        myCommand->append(" &");
        system(myCommand->toLatin1());
        delete myCommand;
        system("rm -rf .INDIPID.tsl");
    }
    infile.close(); // close the file
    ui->pbKillINDIServer->setEnabled(false);
    ui->pbStartINDIServer->setEnabled(true);
    ui->gbStartINDI->setEnabled(true);
    ui->pbConnectToServer->setEnabled(false);
    this->setINDIrbuttons(true);
}

//------------------------------------------------------------------
// if a message from INDI is received by the camera class, process the signal and display it
void MainWindow::handleServerMessage(void) {
    QString *indiMesg;

    indiMesg=new QString(camera_client->getINDIServerMessage()->toLatin1());
    if (indiMesg->isEmpty()==false) {
        ui->textEditINDIMsgs->appendPlainText(indiMesg->toLatin1());
    }
    delete indiMesg;
}
//------------------------------------------------------------------
// deploy a system command to start an INDI server locally with standard parameters.
// type of server is defined by radiobuttons ...
void MainWindow::deployINDICommand(void) {
    int retval = 0;

    ui->sbCCDGain->setEnabled(true);
    ui->sbExposureTime->setEnabled(true);
    if (ui->rbQHYINDI->isChecked()== true) {
        retval = system("indiserver -v indi_qhy_ccd &");
    }
    if (ui->rbZWOINDI->isChecked()== true) {
        retval = system("indiserver -v indi_asi_ccd &");
    }
    if (ui->rbV4L2INDI->isChecked()== true) {
        retval = system("indiserver -v indi_v4l2_ccd &");
        ui->sbCCDGain->setEnabled(false);
        ui->sbExposureTime->setEnabled(false);
    }
    if (ui->rbMoravian->isChecked()== true) {
        retval = system("indiserver -v indi_mi_ccd &");
    }
    if (ui->rbSLXPress->isChecked() == true) {
        retval = system("indiserver -v indi_sx_ccd &");
    }
    if (retval == 0) {
        ui->pbStartINDIServer->setEnabled(false);
        ui->pbKillINDIServer->setEnabled(true);
        ui->pbConnectToServer->setEnabled(true);
        this->setINDIrbuttons(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);   // process events before sleeping for a second
    }
    sleep(1);
    this->findOutAboutINDIServerPID(); // store the PID in a file
}

//------------------------------------------------------------------
// slot that erases the log window for INDI
void MainWindow::clearINDILog(void) {
    ui->textEditINDIMsgs->clear();
}

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// routines for handling the ccd - camera
//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// send an exposure time to the camera and tell INDI to take an image
// an event is triggered once these data were received
void MainWindow::takeSingleCamShot(void) {
   int exptime;

   exptime = ui->sbExposureTime->value();
   camera_client->takeExposure(exptime);

}
//------------------------------------------------------------------
// prepare GUI for taking images; once an image was received, a
// new one is requested in "displayGuideCamImage"
void MainWindow::startCCDAcquisition(void) {
    this->ccdCameraIsAcquiring=true;
    ui->pbExpose->setEnabled(false);
    ui->pbStopExposure->setEnabled(true);
    takeSingleCamShot();
    ui->pbSelectGuideStar->setEnabled(true);
    ui->pbDisconnectFromServer->setEnabled(false);
}

//------------------------------------------------------------------
// set a flag so that no new image is requested in "displayGuideCamImage"
void MainWindow::stopCCDAcquisition(void) {
    this->ccdCameraIsAcquiring=false;
    ui->pbStopExposure->setEnabled(false);
    ui->pbDisconnectFromServer->setEnabled(true);
    this->camImageWasReceived=false;
}

//------------------------------------------------------------------
// same as "stopCCDAcquisition", but it also waits for the
// image to be received. this is indicated when "camImageWasReceived"
// is set to "true" as when a signal "imageAvailable" is emitted by
// the camera-class. this one is connected to "displayGuideCamImage",
// where images are further handled and "camImageWasReceived" is also
// set to "true". return "true" if a final image was acquired and
// "false" if a timeout ocurred ...
bool MainWindow::abortCCDAcquisition(void) {
    QElapsedTimer *timeElapsedLocal;
    long maxTime;

    this->ccdCameraIsAcquiring=false;
    ui->pbStopExposure->setEnabled(false);
    ui->pbDisconnectFromServer->setEnabled(true);
    this->camImageWasReceived=false;
    timeElapsedLocal = new QElapsedTimer();
    timeElapsedLocal->start();
    maxTime = ui->sbExposureTime->value()*5000; // wait for a maximum of 5* the exposure time for a last image
    while (this->camImageWasReceived==false) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);   // process events while waiting for the last image
        if (timeElapsedLocal->elapsed() > maxTime) {
            delete timeElapsedLocal;
            return false;
        }
    }
    delete timeElapsedLocal;
    return true;
}

//------------------------------------------------------------------
// this one tells the camera class to store the ccd images
void MainWindow::enableCamImageStorage(void) {
    if (ui->cbStoreGuideCamImgs->isChecked()==true) {
        camera_client->setStoreImageFlag(true);
    } else {
        camera_client->setStoreImageFlag(false);
    }
}

//------------------------------------------------------------------
// slot for changing the gain settings of the guidecamera
void MainWindow::changeCCDGain(void) {
    int gainSet;

    gainSet = ui->sbCCDGain->value();
    camera_client->sendGain(gainSet);
}

//------------------------------------------------------------------
// does a lot - stores the camera image in the mainwindow class,
// displays the bigger image, but if a guidestar is selected, it also
// takes care of processing the preview image. also polls new images
// if the appropriate flag is set
void MainWindow::displayGuideCamImage(QPixmap *camPixmap) {
    int thrshld,beta;
    float newX, newY,alpha;
    bool medianOn;

    this->camImageWasReceived= true;
    thrshld = ui->hsThreshold->value();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    medianOn=ui->cbMedianFilter->isChecked(); // get parameters for guidestar-processing from GUI
       // now cope with different states and images needed ...

    if (g_AllData->getINDIState() == true) { // ... if the camera class is connected to the INDI server ...
        delete camImg;
        this->camImg = new QPixmap(*camPixmap);
        this->camView->addBgImage(*camImg); // receive the pixmap from the camera ...
        if (this->guidingState.calibrationIsRunning == true) { // autoguider is calibrating
            this->guidingState.calibrationImageReceived=true; // in calibration, this camera image is to be used
        } // we only take a single shot here
        if ((this->ccdCameraIsAcquiring==true) && (this->guidingState.guidingIsOn==false)) { // if the flag for taking another one is true ...
            this->takeSingleCamShot(); // ... request another one from INDI
        } else {
            ui->pbExpose->setEnabled(true); // if acquisition is disabled, set the GUI so that it can be enabled
        }
        if ((this->guidingState.guidingIsOn==true) && (this->guidingState.systemIsCalibrated==true)) { // if autoguiding is active and system is calibrated
            this->guidingState.noOfGuidingSteps++; // every odd one, corrections are applied ...
            this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            newX = g_AllData->getInitialStarPosition(2);
            newY = g_AllData->getInitialStarPosition(3); // the star centroid found in "doGuideStarImgProcessing" was stored in the global struct ...
            correctGuideStarPosition(newX,newY); // ... and is used to correct the position
        }
    }
}

//------------------------------------------------------------------
// retrieve parameters for the CCD from the camera class
 bool MainWindow::getCCDParameters(void) {
    bool retrievalSuccess;
    QString letxt;
    float psx,psy;
    int fsx,fsy;

    retrievalSuccess = camera_client->getCCDParameters();
    if (retrievalSuccess==1) {
        psx=g_AllData->getCameraPixelSize(0);
        psy=g_AllData->getCameraPixelSize(1);
        fsx=(int)g_AllData->getCameraChipPixels(0);
        fsy=(int)g_AllData->getCameraChipPixels(1);
        letxt=QString::number((double)psx,'g',2);
        ui->lePixelSizeX->setText(letxt);
        letxt=QString::number((double)psy,'g',2);
        ui->lePixelSizeY->setText(letxt);
        letxt=QString::number(fsx);
        ui->leFrameSizeX->setText(letxt);
        letxt=QString::number(fsy);
        ui->leFrameSizeY->setText(letxt);
    }
    return retrievalSuccess;
}

 //------------------------------------------------------------------
 // store data on the ccd from the GUI to the global data and to the .tsp file ...
 void MainWindow::storeCCDData(void)  {
     float psx,psy;
     int ccdw, ccdh;
     QString *leEntry;

     leEntry = new QString(ui->lePixelSizeX->text());
     psx=leEntry->toFloat();
     leEntry->clear();
     leEntry->append(ui->lePixelSizeY->text());
     psy=leEntry->toFloat();
     leEntry->clear();
     leEntry->append(ui->leFrameSizeX->text());
     ccdw=leEntry->toInt();
     leEntry->clear();
     leEntry->append(ui->leFrameSizeY->text());
     ccdh=leEntry->toInt();
     delete leEntry;
     g_AllData->setCameraParameters(psx,psy,ccdw,ccdh);
     g_AllData->storeGlobalData();
 }

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// routines for auto guiding
//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// correct guide star position here. called from "displayGuideCamImage".
double MainWindow::correctGuideStarPosition(float cx, float cy) {
    float devVector[2], devVectorRotated[2],errx,erry,err;
    long pgduration;
    QString logString;

    if (this->guidingState.noOfGuidingSteps == 1) {
        this->guideStarPosition.centrX = cx;
        this->guideStarPosition.centrY = cy;
        if (ui->cbLogGuidingData->isChecked()==true) {
            qDebug() << "Initial guidestar-position is " << cx << cy;
            logString.append("Travel time per Pixel in ms:\t");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append(QString::number((double)this->guidingState.travelTime_ms,'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append("Rotation matrix:\t");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append(QString::number((double)this->rotMatrixGuidingXToRA[0][0],'g',3));
            logString.append("\t");
            logString.append(QString::number((double)this->rotMatrixGuidingXToRA[0][1],'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append("\t");
            logString.append(QString::number((double)this->rotMatrixGuidingXToRA[1][0],'g',3));
            logString.append("\t");
            logString.append(QString::number((double)this->rotMatrixGuidingXToRA[1][1],'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append("Decl backlash in ms:\t");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append(QString::number((double)this->guidingState.backlashCompensationInMS,'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
            logString.append("Guidestar-position:\t");
            logString.append(QString::number((double)cx,'g',3));
            logString.append("\t");
            logString.append(QString::number((double)cy,'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
        }
        this->takeSingleCamShot(); // poll a new image
        return 0.0;
    } // when called for the first time, make the current centroid the reference ...
    if (ui->cbLogGuidingData->isChecked()==true) {
        qDebug() << "Centroid:" << cx << cy;
        logString.append("Measured centroid:\t");
        logString.append(QString::number((double)cx,'g',3));
        logString.append("\t");
        logString.append(QString::number((double)cy,'g',3));
        logString.append("\n");
        this->guidingLog->write(logString.toLatin1(),logString.length());
        logString.clear();
    }

    // now compute the deviation
    devVector[0]=-(this->guideStarPosition.centrX - cx);
    devVector[1]=-(this->guideStarPosition.centrY - cy); // this is the deviation in pixel from the last position
    errx=devVector[0]*this->guiding->getArcSecsPerPix(0);
    erry=devVector[1]*this->guiding->getArcSecsPerPix(1);
    err=sqrt(errx*errx+erry*erry);
    if (err > this->guidingState.maxDevInArcSec) {
        this->guidingState.maxDevInArcSec = err;
        ui->leMaxGuideErr->setText(textEntry->number(err));
    }
    if (ui->cbLogGuidingData->isChecked()==true) {
            logString.append("Current deviation:\t");
            logString.append(QString::number((double)devVector[0],'g',3));
            logString.append("\t");
            logString.append(QString::number((double)devVector[1],'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
        }
    devVectorRotated[0]=(this->rotMatrixGuidingXToRA[0][0]*devVector[0]+this->rotMatrixGuidingXToRA[0][1]*devVector[1]);
    devVectorRotated[1]=(this->rotMatrixGuidingXToRA[1][0]*devVector[0]+this->rotMatrixGuidingXToRA[1][1]*devVector[1]);
    // the deviation vector is rotated to the ra/decl coordinate system and inverted as we want to move in the other direction
    if (ui->cbLogGuidingData->isChecked()==true) {
            logString.append("Transformed position:\t");
            logString.append(QString::number((double)devVectorRotated[0],'g',3));
            logString.append("\t");
            logString.append(QString::number((double)devVectorRotated[1],'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
    }

    // carry out the correction in RA
    if (fabs(devVectorRotated[0]) > ui->sbDevRA->value()) {
        pgduration=this->guidingState.travelTime_ms*fabs(devVectorRotated[0]); // pulse guide duration in ra
        if (ui->cbLogGuidingData->isChecked()==true) {
            logString.append("RA correction duration:\t");
            logString.append(QString::number((double)pgduration,'g',3));
            logString.append("\n");
            this->guidingLog->write(logString.toLatin1(),logString.length());
            logString.clear();
        }
        ui->sbPulseGuideDuration->setValue(pgduration); // set the duration for the slew in RA - this value is used in the pulseguideroutine
        ui->lePulseRAMS->setText(textEntry->number(pgduration));
        if (devVectorRotated[0]>0) {
            ui->leDevRaPix->setText(textEntry->number(-devVectorRotated[0]));
            this->raPGBwdGd(pgduration);
            if (ui->cbLogGuidingData->isChecked()==true) {
                logString.append("RA correction direction:\t RA-\n");
                this->guidingLog->write(logString.toLatin1(),logString.length());
                logString.clear();
            }
        } else {
            ui->leDevRaPix->setText(textEntry->number(devVectorRotated[0]));
            this->raPGFwdGd(pgduration);
            if (ui->cbLogGuidingData->isChecked()==true) {
                logString.append("RA correction direction:\t RA+\n");
                this->guidingLog->write(logString.toLatin1(),logString.length());
                logString.clear();
            }
        }
    }
    this->waitForDriveStop(true,false); // just to make sure that drive has stopped moving, should not be an issue as guiding is unthreaded

    // carry out the correction in decl
    if (fabs(devVectorRotated[1]) > ui->sbDevDecl->value()) {
        pgduration=this->guidingState.travelTime_ms*fabs(devVectorRotated[1]); // pulse guide duration in decl
        if (ui->cbLogGuidingData->isChecked()==true) {
                logString.append("Decl correction duration:\t");
                logString.append(QString::number((double)pgduration,'g',3));
                logString.append("\n");
                this->guidingLog->write(logString.toLatin1(),logString.length());
                logString.clear();
            }
        if (devVectorRotated[1]>0) {
            ui->cbDeclinationInverted->setChecked(false); // indicate that decl-direction indicator to false
            if (this->guidingState.declinationDriveDirection < 0) {
                this->guidingState.declinationDriveDirection = +1; // switch state to positive travel
                ui->cbDeclinationInverted->setChecked(true); // indicate that decl-direction is switched
                if (ui->cbDeclBacklashComp->isChecked()==true) { // carry out compensation if checkbox is activated
                    this->compensateDeclBacklashPG(-this->guidingState.declinationDriveDirection); // trying to invert the correction direction ... hopefully correct
                    if (ui->cbLogGuidingData->isChecked()==true) {
                            logString.append("Decl backlash activated.\n");
                            this->guidingLog->write(logString.toLatin1(),logString.length());
                            logString.clear();
                        }
                }
                ui->sbPulseGuideDuration->setValue(pgduration); // set the duration for the slew in Decl - this value is used in the pulseguideroutine
            }
            ui->lePulseDeclMS->setText(textEntry->number(pgduration));
            ui->leDevDeclPix->setText(textEntry->number(-devVectorRotated[1]));
            this->declPGMinusGd(pgduration);
            if (ui->cbLogGuidingData->isChecked()==true) {
                logString.append("Decl correction direction:\t Decl-\n");
                this->guidingLog->write(logString.toLatin1(),logString.length());
                logString.clear();
            }
        } else {
            ui->cbDeclinationInverted->setChecked(false);
            if (this->guidingState.declinationDriveDirection > 0) {
                this->guidingState.declinationDriveDirection = -1; // switch state to negative travel
                ui->cbDeclinationInverted->setChecked(true);
                if (ui->cbDeclBacklashComp->isChecked()==true) {
                    compensateDeclBacklashPG(-this->guidingState.declinationDriveDirection); // trying to invert the correction direction ... hopefully correct
                if (ui->cbLogGuidingData->isChecked()==true) {
                            logString.append("Decl backlash activated.\n");
                            this->guidingLog->write(logString.toLatin1(),logString.length());
                            logString.clear();
                        }
                }
                ui->sbPulseGuideDuration->setValue(pgduration); // set the duration for the slew in Decl - this value is used in the pulseguideroutine
            }
            ui->lePulseDeclMS->setText(textEntry->number(pgduration));
            ui->leDevDeclPix->setText(textEntry->number(devVectorRotated[1]));
            this->declPGPlusGd(pgduration);
            if (ui->cbLogGuidingData->isChecked()==true) {
                logString.append("Decl correction direction:\t Decl+\n");
                this->guidingLog->write(logString.toLatin1(),logString.length());
                logString.clear();
            }
        }
    }
    this->waitForDriveStop(false,false);
    this->takeSingleCamShot(); // poll a new image
    return 0.0;
}

//------------------------------------------------------------------
// a rotuine that resets the maximum guiding error
void MainWindow::resetGuidingError(void) {

    if (this->guidingState.guidingIsOn==true) {
        this->guidingState.maxDevInArcSec=0.0;
        ui->leMaxGuideErr->setText(textEntry->number(this->guidingState.maxDevInArcSec));
    }
}

//------------------------------------------------------------------
// calibrate the system. the selected star is located and three
// pulse guide commands in each direction and back are carried out.
// the pixel/ms is then evaluated for each direction. 8 slews from the
// center of the search image are carried out, and the relative angle between
// a coordinate system defined by ra/decl movement and by the x/y frame
// coordinate system is defined. a log displays status messages ...
void MainWindow::calibrateAutoGuider(void) {
    int pulseDuration;
    double currentCentroid[2],initialCentroid[2],slewVector[2],arcsecPPix[2],
        travelPerMSInRACorr,travelTimeInMSForOnePix,tTimeOnePix[4],lengthOfTravel,
        lengthOfTravelWithDeclBacklash,relativeAngle[4],avrgAngle, sdevAngle, avrgDeclBacklashInPixel,
        sdevBacklashPix, declBacklashInPixel[4];
    float alpha;
    int thrshld,beta,imgProcWindowSize;
    bool medianOn;
    short slewCounter;

    this->displayCalibrationStatus("Entering calibration...");
    if (this->abortCCDAcquisition() == true) {
        this->displayCalibrationStatus("CCD acquisition stopped...");
    } else {
        this->displayCalibrationStatus("CCD acquisition timeout...");
    } // stopping the stream of images from the ccd ...
    setControlsForAutoguiderCalibration(false);
    thrshld = ui->hsThreshold->value();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    medianOn=ui->cbMedianFilter->isChecked(); // get parameters for guidestar-processing from GUI
    arcsecPPix[0] = this->guiding->getArcSecsPerPix(0);
    arcsecPPix[1] = this->guiding->getArcSecsPerPix(1); // get the ratio "/pixel from the guiding class
    travelPerMSInRACorr=0.001*(3600.0)*g_AllData->getDriveParams(0,0)*(g_AllData->getGearData(3)/g_AllData->getGearData(8))/
        (g_AllData->getGearData(0)*g_AllData->getGearData(1)*g_AllData->getGearData(2));
    travelTimeInMSForOnePix=arcsecPPix[0]/travelPerMSInRACorr; // travel time for one pix in ra direction in milliseconds
    this->displayCalibrationStatus("Time for 1 pix: ",travelTimeInMSForOnePix," ms");

    // now determine the direction of RA+ Travel as a unit vector; travel for "imgProcWindowSize" pix and
    // determine the relative angle between ccd x/y and ra/decl. first, a run is carried out with the
    // pulse guide duration as computed from guide scope fl and camera pixels; the time and travel in pixels
    // is used to update "travelTimeInMSForOnePix"
    this->displayCalibrationStatus("Calibration run:");
    this->displayCalibrationStatus("0/4 ...");
    imgProcWindowSize=round(90*this->guidingFOVFactor*0.5); // 1/4 size of the image processing window is the travel in RA+ ...
    pulseDuration = imgProcWindowSize*travelTimeInMSForOnePix; // that gives the pulse duration
    this->waitForCalibrationImage(); // small subroutine - waits for 2 images
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    initialCentroid[0] = g_AllData->getInitialStarPosition(2);
    initialCentroid[1] = g_AllData->getInitialStarPosition(3); // first centroid before slew
    ui->sbPulseGuideDuration->setValue(pulseDuration); // set the duration for the slew
    this->displayCalibrationStatus("RA+ slew (pix): ",(float)imgProcWindowSize, "");
    this->raPGFwd(); // carry out travel
    this->waitForDriveStop(true, true);
    ui->pbPGRAMinus->setEnabled(false);
    ui->pbPGRAPlus->setEnabled(false);
    this->waitForCalibrationImage();
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100); // try to force screen updates as "doGuideStarImageProcesing" emits a signal ...
    currentCentroid[0] = g_AllData->getInitialStarPosition(2);
    currentCentroid[1] = g_AllData->getInitialStarPosition(3); // centroid after slew
    slewVector[0] = currentCentroid[0]-initialCentroid[0];
    slewVector[1] = currentCentroid[1]-initialCentroid[1];  // direction vector of slew
    lengthOfTravel=sqrt(slewVector[0]*slewVector[0]+slewVector[1]*slewVector[1]); // length of vector
    travelTimeInMSForOnePix=pulseDuration/lengthOfTravel; // replace estimated "travelTimeInMSForOnePix" by a measured value
    pulseDuration = imgProcWindowSize*travelTimeInMSForOnePix; // that gives the pulse duration
    ui->sbPulseGuideDuration->setValue(pulseDuration); // set the duration for the slew
    this->displayCalibrationStatus("Slewing back...");
    this->raPGBwd(); // going back to initial position
    this->waitForDriveStop(true, true);
    ui->pbPGRAMinus->setEnabled(false);
    ui->pbPGRAPlus->setEnabled(false);

    // now repeat this procedure 4 times to get a better estimate of "travelTimeInMSForOnePix" and the relative angle of the ccd and telescope coordinate system
    for (slewCounter = 0; slewCounter < 4; slewCounter++) {
        this->displayCalibrationStatus("Calibration run: ", (float)(slewCounter+1), "/4 ...");
        imgProcWindowSize=round(90*this->guidingFOVFactor*0.5); // 1/4 size of the image processing window is the travel in RA+ ...
        this->waitForCalibrationImage(); // small subroutine - waits for 1 new image
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        initialCentroid[0] = g_AllData->getInitialStarPosition(2);
        initialCentroid[1] = g_AllData->getInitialStarPosition(3); // first centroid before slew
        this->displayCalibrationStatus("RA+ slew (pix): ",(float)imgProcWindowSize,"");
        this->raPGFwd(); // carry out travel
        this->waitForDriveStop(true,true);
        ui->pbPGRAMinus->setEnabled(false);
        ui->pbPGRAPlus->setEnabled(false);
        this->waitForCalibrationImage();
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        currentCentroid[0] = g_AllData->getInitialStarPosition(2);
        currentCentroid[1] = g_AllData->getInitialStarPosition(3); // centroid after slew
        slewVector[0] = currentCentroid[0]-initialCentroid[0];
        slewVector[1] = currentCentroid[1]-initialCentroid[1];  // direction vector of slew
        lengthOfTravel=sqrt(slewVector[0]*slewVector[0]+slewVector[1]*slewVector[1]); // length of vector
        relativeAngle[slewCounter]=acos((slewVector[0])/(lengthOfTravel)); // the angle between the RA/Decl coordinate system and the x/y coordinate system of the cam is given by the inner product ...
        this->displayCalibrationStatus("Relative angle: ",(float)relativeAngle[slewCounter]*(180.0/3.14159),"°.");
        tTimeOnePix[slewCounter]=pulseDuration/lengthOfTravel;
        this->displayCalibrationStatus("Slewing back...");
        this->raPGBwd(); // going back to initial position
        this->waitForDriveStop(true,true);
        ui->pbPGRAMinus->setEnabled(false);
        ui->pbPGRAPlus->setEnabled(false);
    }
    travelTimeInMSForOnePix+=tTimeOnePix[0]+tTimeOnePix[1]+tTimeOnePix[2]+tTimeOnePix[3];
    travelTimeInMSForOnePix/=5.0; // compute the average travel time
    pulseDuration = imgProcWindowSize*travelTimeInMSForOnePix; // that gives the pulse duration
    ui->sbPulseGuideDuration->setValue(pulseDuration); // set the duration for the slew
    this->displayCalibrationStatus("Travel time for 1 pix: ", travelTimeInMSForOnePix,"ms.");
    avrgAngle=(relativeAngle[0]+relativeAngle[1]+relativeAngle[2]+relativeAngle[3])/4.0;
    sdevAngle=sqrt(1/3.0*((relativeAngle[0]-avrgAngle)*(relativeAngle[0]-avrgAngle)+
                          (relativeAngle[1]-avrgAngle)*(relativeAngle[1]-avrgAngle)+
                          (relativeAngle[2]-avrgAngle)*(relativeAngle[2]-avrgAngle)+
                          (relativeAngle[3]-avrgAngle)*(relativeAngle[3]-avrgAngle))); // compute the standard deviation
    this->displayCalibrationStatus("Rotation Angle: ", (avrgAngle*(180.0/3.14159)),"°.");
    this->displayCalibrationStatus("Standard deviation: ", (sdevAngle*(180.0/3.14159)),"°.");
        // rotation angle determined

    //-----------------------------------------
    // debugging code
    //  avrgAngle=2.13;
    //  travelTimeInMSForOnePix=71.5;
    //-----------------------------------------

    // now determine the rotation matrix from ccd x/y to ra/decl
    this->rotMatrixGuidingXToRA[0][0]=cos(avrgAngle);
    this->rotMatrixGuidingXToRA[0][1]=sin(avrgAngle);
    this->rotMatrixGuidingXToRA[1][0]=-sin(avrgAngle);
    this->rotMatrixGuidingXToRA[1][1]=cos(avrgAngle);

    // mirror check in decl is missing here ...

    // now do declination travel to compensate for backlash when reversing declination travel direction
    this->displayCalibrationStatus("Determine declination backlash ...");
    this->declPGMinus();
    this->waitForDriveStop(false,true);
    this->declPGPlus(); // carry out a slew in + direction to apply tension to the worm prior to another "+" -slew
    this->waitForDriveStop(false,true);

    for (slewCounter = 0; slewCounter < 4; slewCounter++) {
        this->displayCalibrationStatus("Backlash calibration run: ", (float)(slewCounter+1), "/4 ...");
        ui->pbPGDecMinus->setEnabled(false);
        ui->pbPGDecPlus->setEnabled(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        this->waitForCalibrationImage(); // small subroutine - waits for image
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            // now get a position
        initialCentroid[0] = g_AllData->getInitialStarPosition(2);
        initialCentroid[1] = g_AllData->getInitialStarPosition(3); // centroid before slew
        this->displayCalibrationStatus("Decl+ slew (pix): ",(float)imgProcWindowSize,"");
        if (slewCounter%2==0) {
            this->guidingState.declinationDriveDirection=1; // remember that we travel forward in decl
            this->declPGPlus(); // carry out travel
        } else {
            this->guidingState.declinationDriveDirection=-1; // remember that we travel backward in decl
            this->declPGMinus(); // carry out travel
        } // alternate declination drive direction in order to maintain tension on the worm wheel
        this->waitForDriveStop(false,true);
        ui->pbPGDecMinus->setEnabled(false);
        ui->pbPGDecPlus->setEnabled(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        this->waitForCalibrationImage();
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        currentCentroid[0] = g_AllData->getInitialStarPosition(2);
        currentCentroid[1] = g_AllData->getInitialStarPosition(3); // centroid after slew
        slewVector[0] = currentCentroid[0]-initialCentroid[0];
        slewVector[1] = currentCentroid[1]-initialCentroid[1];  // direction vector of slew
        lengthOfTravel=sqrt(slewVector[0]*slewVector[0]+slewVector[1]*slewVector[1]); // length of vector
        initialCentroid[0]=currentCentroid[0];
        initialCentroid[1]=currentCentroid[1]; // now go back, therefore the endpoint is the new beginning ...
        this->displayCalibrationStatus("Travel back ...");
        if (slewCounter%2==0) {
            this->guidingState.declinationDriveDirection=-1;
            this->declPGMinus(); // carry out travel
        } else {
            this->guidingState.declinationDriveDirection=1;
            this->declPGPlus(); // carry out travel
        } // again - alternate declination travel to maintain tension on the worm wheel
        this->waitForDriveStop(false,true);
        ui->pbPGDecMinus->setEnabled(false);
        ui->pbPGDecPlus->setEnabled(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        this->waitForCalibrationImage();
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected); // ... process the guide star subimage
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        currentCentroid[0] = g_AllData->getInitialStarPosition(2);
        currentCentroid[1] = g_AllData->getInitialStarPosition(3); // centroid after slew
        slewVector[0] = currentCentroid[0]-initialCentroid[0];
        slewVector[1] = currentCentroid[1]-initialCentroid[1];  // direction vector of slew
        lengthOfTravelWithDeclBacklash=sqrt(slewVector[0]*slewVector[0]+slewVector[1]*slewVector[1]); // travel incl. decl backlash
        declBacklashInPixel[slewCounter]=-lengthOfTravelWithDeclBacklash+lengthOfTravel; // determine backlash in pixel
        this->displayCalibrationStatus("Decl backlash (pix): ",(float)declBacklashInPixel[slewCounter],"");
    }
    avrgDeclBacklashInPixel=(declBacklashInPixel[0]+declBacklashInPixel[1]+declBacklashInPixel[2]+declBacklashInPixel[3])/4.0;
    sdevBacklashPix=sqrt(1/3.0*((declBacklashInPixel[0]-avrgDeclBacklashInPixel)*(declBacklashInPixel[0]-avrgDeclBacklashInPixel)+
                                (declBacklashInPixel[1]-avrgDeclBacklashInPixel)*(declBacklashInPixel[1]-avrgDeclBacklashInPixel)+
                                (declBacklashInPixel[2]-avrgDeclBacklashInPixel)*(declBacklashInPixel[2]-avrgDeclBacklashInPixel)+
                                (declBacklashInPixel[3]-avrgDeclBacklashInPixel)*(declBacklashInPixel[3]-avrgDeclBacklashInPixel)));
    this->guidingState.backlashCompensationInMS=avrgDeclBacklashInPixel*travelTimeInMSForOnePix; // determine length of travel for backlash compensation
    this->displayCalibrationStatus("Backlash compensation: ", (float)this->guidingState.backlashCompensationInMS,"[ms]");
    this->displayCalibrationStatus("Standard deviation: ", sdevBacklashPix,"[ms]");

    //  debug code follows
    // this->guidingState.backlashCompensationInMS=800;

    this->guidingState.calibrationIsRunning=false; // "calibrationIsRunning" - flag set to false
    this->guidingState.systemIsCalibrated=true; // "systemIsCalibrated" - flag set to true
    setControlsForAutoguiderCalibration(true);
    this->guidingState.travelTime_ms=travelTimeInMSForOnePix;
    this->guidingState.rotationAngle=avrgAngle;
    this->displayCalibrationStatus("Calibration is finished...");
    this->startCCDAcquisition(); // starting ccd acquisition again in a permanent mode ...
}

//------------------------------------------------------------------
// wait until a drive has stopped. helper application for
// "calibrateAutoGuider" and "correctGuideStarPosition"
void MainWindow::waitForDriveStop(bool isRA, bool isVerbose) {

    if (isRA) {
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        } while (this->mountMotion.RADriveIsMoving == true);
        if (isVerbose) {
            this->displayCalibrationStatus("RA motion stopped..."); // just make sure that the program continues once the drive is down
        }
    } else {
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        } while (this->mountMotion.DeclDriveIsMoving == true);
        if (isVerbose) {
            this->displayCalibrationStatus("Decl motion stopped..."); // just make sure that the program continues once the drive is down
        }
    }
}

//------------------------------------------------------------------
// a subroutine that acquires a single image during calibration. needed in
// "calibrateAutoGuider"
void MainWindow::waitForCalibrationImage(void) {
    QElapsedTimer *lTim;

    this->displayCalibrationStatus("Waiting for image...");
    lTim = new QElapsedTimer();
    lTim->start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    } while (lTim->elapsed() < 250); // wait for 250 ms
    delete lTim;
    this->guidingState.calibrationIsRunning=true;
    this->guidingState.calibrationImageReceived=false;
    this->takeSingleCamShot();
    while (this->guidingState.calibrationImageReceived == false) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

//------------------------------------------------------------------
// a subroutine that displays status messages during calibration;
// only needed to make the code more compact. this one displays
// an assembled string from values and strings; an override just gives a string.
void MainWindow::displayCalibrationStatus(QString str1, float number, QString str2) {
    QString lMesg;

    lMesg.append(str1);
    lMesg.append(QString::number((double)number,'g',3));
    lMesg.append(str2);
    ui->teCalibrationStatus->appendPlainText(lMesg);
    lMesg.clear();
}

//------------------------------------------------------------------
// an override just giving a single string message
void MainWindow::displayCalibrationStatus(QString str1) {
    ui->teCalibrationStatus->appendPlainText(str1);
}

//------------------------------------------------------------------
// resets all calibration parameters
void MainWindow::resetGuidingCalibration(void) {
    if ((this->guidingState.systemIsCalibrated==true) &&
        (this->guidingState.guidingIsOn==false)) {
        this->abortCCDAcquisition();
        ui->teCalibrationStatus->clear();
        this->guidingState.guideStarSelected=false;
        this->guidingState.guidingIsOn=false;
        this->guidingState.calibrationIsRunning=false;
        this->guidingState.systemIsCalibrated=false;
        this->guidingState.calibrationImageReceived=false;
        this->guidingState.declinationDriveDirection=1;
        this->guidingState.travelTime_ms=0.0;
        this->guidingState.rotationAngle=0.0;
        this->guidingState.maxDevInArcSec=0.0;
        this->guidingState.backlashCompensationInMS=0.0;
        this->guidingState.noOfGuidingSteps = 0;
        this->guidingState.st4IsActive=false;
    }
}

//------------------------------------------------------------------
// prepare the GUI and the flags for autoguiding; the actual work is done
// in "displayGuideCamImage" and "correctGuideStarPosition" ...
void MainWindow::doAutoGuiding(void) {

    if (this->guidingState.guidingIsOn ==false) {
        ui->rbSiderealSpeed->setChecked(true); // make sure that sidereal speed is set...
        this->setTrackingRate();
        this->guidingState.maxDevInArcSec=0.0;
        this->guidingState.guidingIsOn = true;
        if (ui->cbLogGuidingData->isChecked()==true) {
            this->guidingLog = new QFile("GuidingLog.tsl");
            this->guidingLog->open((QIODevice::ReadWrite | QIODevice::Text));
        }
        g_AllData->setGuidingState(this->guidingState.guidingIsOn); // this has to be known in other classes, so every "guidingIsOn" state is copied
        this->setControlsForGuiding(false);
        // take care of disabling the gui here ...
        ui->pbGuiding->setText("Stop");
    } else {
        this->guidingState.guidingIsOn = false;
        if (ui->cbLogGuidingData->isChecked()==true) {
            if (this->guidingLog != NULL) {
                this->guidingLog->close();
            }
        }
        g_AllData->setGuidingState(this->guidingState.guidingIsOn); // this has to be known in other classes, so every "guidingIsOn" state is copied
        ui->pbGuiding->setText("Guide");
        this->setControlsForGuiding(true);
        // enable the GUI here again ...
    }
}

//------------------------------------------------------------------
// slot for finding a guide star. the position is initially taken
// from the crosshair position stored in g_AllData.
void MainWindow::selectGuideStar(void) {
    int thrshld,beta;
    bool medianOn;
    float alpha;

    if ((this->ccdCameraIsAcquiring==true) && (this->camImageWasReceived==true)) {
        if (this->mountMotion.RATrackingIsOn==false) {
            this->startRATracking();
        } // turn on tracking if it is not running when a guide star is selected
        ui->pbGuiding->setEnabled(true);
        ui->hsThreshold->setEnabled(true);
        ui->hsIContrast->setEnabled(true);
        ui->hsIBrightness->setEnabled(true); // enable image processing controls
        medianOn=ui->cbMedianFilter->isChecked();
        thrshld = ui->hsThreshold->value();
        alpha = ui->hsIContrast->value()/100.0;
        beta = ui->hsIBrightness->value(); // get image processing parameters
        this->guidingState.guideStarSelected=true;
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        guideStarPosition.centrX = g_AllData->getInitialStarPosition(2);
        guideStarPosition.centrY = g_AllData->getInitialStarPosition(3); // "doGuideStarImgProcessing" stores a position in g_AllData
        this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        ui->pbTrainAxes->setEnabled(true);
    }
}

//------------------------------------------------------------------
// slot for changing the image processing controls
void MainWindow::changePrevImgProc(void) {
    int thrshld,beta;
    bool medianOn;
    float alpha;

    thrshld = ui->hsThreshold->value();
    medianOn=ui->cbMedianFilter->isChecked();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta, this->guidingFOVFactor,this->guidingState.guideStarSelected);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// slot for handling a signal from the camera class that a new
// processed guide star image is available
void MainWindow::displayGuideStarPreview(void) {
    guideStarPrev = this->guiding->getGuideStarPreview();
    ui->lPreview->setPixmap(*guideStarPrev);
}

//------------------------------------------------------------------
// slot for storing the input from a spinbox on guide scope focal length
void MainWindow::changeGuideScopeFL(void) {
    int focalLength;

    focalLength=ui->sbFLGuideScope->value();
    this->guiding->setFocalLengthOfGuidescope(focalLength);
    g_AllData->setGuideScopeFocalLength(focalLength);
}

//------------------------------------------------------------------
// store guide scope focal length to .tsp preference file
void MainWindow::storeGuideScopeFL(void) {
    this->changeGuideScopeFL();
    g_AllData->storeGlobalData();
}

//------------------------------------------------------------------
// reduce the guide star window to 90x90 pixels
void MainWindow::setHalfFOV(void) {
    int thrshld,beta;
    float alpha;
    bool medianOn;

    this->guidingFOVFactor=0.5;
    thrshld = ui->hsThreshold->value();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    medianOn=ui->cbMedianFilter->isChecked();
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// reduce the guide star window to 360x360 pixels
void MainWindow::setDoubleFOV(void) {
    int thrshld,beta;
    float alpha;
    bool medianOn;

    this->guidingFOVFactor=2.0;
    thrshld = ui->hsThreshold->value();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    medianOn=ui->cbMedianFilter->isChecked();
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// reduce the guide star window to 180x180 pixels
void MainWindow::setRegularFOV(void) {
    int thrshld,beta;
    float alpha;
    bool medianOn;

    this->guidingFOVFactor=1.0;
    thrshld = ui->hsThreshold->value();
    alpha = ui->hsIContrast->value()/100.0;
    beta = ui->hsIBrightness->value();
    medianOn=ui->cbMedianFilter->isChecked();
    this->guiding->doGuideStarImgProcessing(thrshld,medianOn,alpha,beta,this->guidingFOVFactor,this->guidingState.guideStarSelected);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// LX 200 related stuff
//------------------------------------------------------------------
//------------------------------------------------------------------
// a slot that handles things when an IP address for LX200 was chosen
void MainWindow::IPaddressChosen(void) {
    QString *ipaddress;

    ipaddress = new QString(ui->listWidgetIPAddresses->currentItem()->text());
    g_AllData->setLX200IPAddress(*ipaddress);
    delete ipaddress;
    ui->pbEnableTCP->setEnabled(true);
}

//------------------------------------------------------------------
// a slot that establishes LX200 TCP/IP communication
void MainWindow::connectToIPSocket(void) {
    QString *ipaddress;
    qint16 lxport;

    ipaddress = new QString(g_AllData->getLX200IPAddress()->toLatin1());
    this->LXServerAddress->setAddress(*ipaddress);
    delete ipaddress;
    lxport = (qint16)(ui->sbLX200Port->value());
    if (this->LXServer->listen(*LXServerAddress,lxport) != true) {
        qDebug() << "Could not open TCPServer";
    } else {
        ui->pbEnableTCP->setEnabled(false);
        ui->pbDisableTCP->setEnabled(true);
        ui->listWidgetIPAddresses->setEnabled(false);
        ui->pbLX200Active->setEnabled(false);
    }
}

//------------------------------------------------------------------
// a slot that stops LX200 TCP/IP communication
void MainWindow::disconnectFromIPSocket(void) {
    this->LXServer->close();
    this->LXSocket->close();
    ui->pbEnableTCP->setEnabled(true);
    ui->pbDisableTCP->setEnabled(false);
    ui->listWidgetIPAddresses->setEnabled(true);
    ui->cbTCPConnected->setChecked(false);
    this->lx200IsOn=false;
    ui->pbLX200Active->setEnabled(true);
}

//------------------------------------------------------------------
// a slot that connects a TCP/IP linked planetarium program to a socket;
// the server listens and establishes a socket when a connection comes in
void MainWindow::establishLX200IPLink(void) {
    if (this->LX200SerialPortIsUp == 0) { // if serial connection is down, then allow connecting
        ui->cbTCPConnected->setChecked(true);
        this->LXSocket = this->LXServer->nextPendingConnection();
        this->lx200IsOn=true;
    }
}

//------------------------------------------------------------------
// handle the serial port - called after timeout of the LX200Timer
void MainWindow::readLX200Port(void) {
    qint64 charsToBeRead;
    QString *command;
    QElapsedTimer *waitTimer;

    if (this->lx200IsOn) {
        command = new QString();
        if (this->LX200SerialPortIsUp == 1) {
            charsToBeRead=this->lx200SerialPort->bytesAvailable();
            if (charsToBeRead > 0) {
                waitTimer = new QElapsedTimer();
                waitTimer->start();
                do {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                } while (waitTimer->elapsed() < 25);
                delete waitTimer; // just wait for 25 ms if more data come in ...
                this->lx200SerialData->clear();
                this->lx200SerialData->append(this->lx200SerialPort->readAll());
                command->append(this->lx200SerialData->data());
            }
        } else {
            charsToBeRead = this->LXSocket->bytesAvailable();
            if (charsToBeRead > 0) {
                waitTimer = new QElapsedTimer();
                waitTimer->start();
                do {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                } while (waitTimer->elapsed() < 25);
                delete waitTimer; // just wait for 25 ms if more data come in ... and to process the queued events :/
                this->tcpLXdata->clear();
                this->tcpLXdata->append(this->LXSocket->readAll());
                command->append(this->tcpLXdata->data());
            }
        }
        lx200port->handleDataFromClient(*command);
        delete command;
    }
}

//------------------------------------------------------------------
// responding to a single <ACK> in classic LX 200 requires a statement on the
// alignment

void MainWindow::sendPolarAlignmentCommand(void) {
    if (this->LX200SerialPortIsUp == true) {
        this->lx200SerialPort->write("P#");
        this->lx200SerialPort->flush();
    } else {
        this->LXSocket->write("P#");
    }
}

//------------------------------------------------------------------
// if LX send RA, this slot handles it ...
void MainWindow::handleRAviaTCP(QString* racmd) {
    if (this->LX200SerialPortIsUp == true) {
        this->lx200SerialPort->write(racmd->toLatin1());
        this->lx200SerialPort->flush();
    } else {
        this->LXSocket->write(racmd->toLatin1());
    }
}

//------------------------------------------------------------------
//  if LX send decl, this slot handles it ...
void MainWindow::handleDeclviaTCP(QString* declcmd) {
    if (this->LX200SerialPortIsUp == true) {
        this->lx200SerialPort->write(declcmd->toLatin1());
        this->lx200SerialPort->flush();
    } else {
        this->LXSocket->write(declcmd->toLatin1());
    }
}

//------------------------------------------------------------------
// handles other replies by LX ...
void MainWindow::handleCommandviaTCP(QString* msgcmd) {
    if (this->LX200SerialPortIsUp == true) {
        this->lx200SerialPort->write(msgcmd->toLatin1());
        this->lx200SerialPort->flush();
    } else {
        this->LXSocket->write(msgcmd->toLatin1());
    }
}

//------------------------------------------------------------------
// log incoming requests from LX 200
void MainWindow::logLX200IncomingCmds(void) {
    QString* lx200msg;

    if ((this->lx200IsOn==true) && (ui->cbLX200Logs->isChecked()==true)) {
        lx200msg = new QString("Incoming: ");
        lx200msg->append(this->lx200port->getLX200Command());
        ui->teLX200Data->appendPlainText(lx200msg->toLatin1());
        delete lx200msg;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// log RA commands from LX 200
void MainWindow::logLX200OutgoingCmdsRA(void) {
    QString* lx200msg;

    if ((this->lx200IsOn==true) && (ui->cbLX200Logs->isChecked()==true)) {
        lx200msg = new QString("Outgoing RA: ");
        lx200msg->append(this->lx200port->getLX200ResponseRA());
        ui->teLX200Data->appendPlainText(lx200msg->toLatin1());
        delete lx200msg;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// log declination commands in LX 200
void MainWindow::logLX200OutgoingCmdsDecl(void) {
    QString* lx200msg;

    if ((this->lx200IsOn==true) && (ui->cbLX200Logs->isChecked()==true)) {
        lx200msg = new QString("Outgoing Decl: ");
        lx200msg->append(this->lx200port->getLX200ResponseDecl());
        ui->teLX200Data->appendPlainText(lx200msg->toLatin1());
        delete lx200msg;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}
//------------------------------------------------------------------
// log outgoing commands from LX 200
void MainWindow::logLX200OutgoingCmds(void) {
    QString* lx200msg;

    if ((this->lx200IsOn==true) && (ui->cbLX200Logs->isChecked()==true)) {
        lx200msg = new QString("Outgoing: ");
        lx200msg->append(this->lx200port->getLX200Response());
        ui->teLX200Data->appendPlainText(lx200msg->toLatin1());
        delete lx200msg;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//------------------------------------------------------------------
// erase the LX 200 log
void MainWindow::clearLXLog(void) {
    ui->teLX200Data->clear();
}

//------------------------------------------------------------------
// sync the mount via LX 200
void MainWindow::LXsyncMount(void) {
    QString lestr;

    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if (this->StepperDriveRA->getStopped() == false) {
            this->stopRATracking();
        }
        if (this->mountMotion.DeclDriveIsMoving == true) {
            this->mountMotion.DeclDriveIsMoving=false;
            this->StepperDriveDecl->stopDrive();
            while (!futureStepperBehaviourDecl.isFinished()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            }
        }
        this->ra = (float)(this->lx200port->getReceivedCoordinates(0));
        this->decl = (float)(this->lx200port->getReceivedCoordinates(1));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        g_AllData->setSyncPosition(this->ra, this->decl);
        // convey right ascension and declination to the global parameters;
        // a microtimer starts ...
        this->startRATracking();
        lestr = QString::number(this->ra, 'g', 8);
        ui->lineEditRA->setText(lestr);
        ui->leLX200RA->setText(lestr);
        lestr = QString::number(this->decl, 'g', 8);
        ui->lineEditDecl->setText(lestr);
        ui->leLX200Decl->setText(lestr);
    }
}

//---------------------------------------------------------------------
// trigger a stop via LX 200; this is not an emergency halt, it just
// terminates motion and goes into tracking state. some ASCOM drivers
// spit out :Q# like hell. I am insecure of the meaning of this command;
// in my opinion, it should stop all motion like an emergency stop, but
// the documentation says only "stop slewing motion" ...
void MainWindow::LXstopMotion(void) {

    if ((this->guidingState.guidingIsOn == false) && (this->guidingState.calibrationIsRunning == false)) {
        if ((this->mountMotion.GoToIsActiveInRA) || (this->mountMotion.GoToIsActiveInDecl) ||
            (this->mountMotion.RADriveIsMoving) || (this->mountMotion.DeclDriveIsMoving)) {
            this->emergencyStop();
            this->startRATracking();
        }
    }
}

//---------------------------------------------------------------------
// slew via LX 200
void MainWindow::LXslewMount(void) {
    QString lestr;
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) || (mountMotion.GoToIsActiveInDecl== false)) {
            if (g_AllData->wasMountSynced() == true) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                this->ra = (float)(this->lx200port->getReceivedCoordinates(0));
                this->decl = (float)(this->lx200port->getReceivedCoordinates(1));
                lestr = QString::number(this->ra, 'g', 8);
                ui->lineEditRA->setText(lestr);
                ui->leLX200RA->setText(lestr);
                lestr = QString::number(this->decl, 'g', 8);
                ui->lineEditDecl->setText(lestr);
                ui->leLX200Decl->setText(lestr);
                this->startGoToObject();
            }
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXmoveEast(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
            && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) {
            if (this->mountMotion.RADriveIsMoving == true) {
                return;
            } else {
                this->RAMoveHandboxBwd();
            }
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXmoveWest(void) {

    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
         && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) {
            if (this->mountMotion.RADriveIsMoving == true) {
                return;
            } else {
                this->RAMoveHandboxFwd();
            }
        }
    }
}
//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXmoveNorth(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) {
            if (this->mountMotion.DeclDriveIsMoving == true) {
                return;
            } else {
                this->declinationMoveHandboxUp();
            }
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXmoveSouth(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) {
            if (this->mountMotion.DeclDriveIsMoving == true) {
                return;
            } else {
                this->declinationMoveHandboxDown();
            }
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXstopMoveEast(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if (((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) &&
                (mountMotion.RADriveIsMoving == true))  {
            this->RAMoveHandboxBwd();
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXstopMoveWest(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if (((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) &&
                (mountMotion.RADriveIsMoving == true))  {
            this->RAMoveHandboxFwd();
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXstopMoveNorth(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if (((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) &&
                (mountMotion.DeclDriveIsMoving == true))  {
            this->declinationMoveHandboxUp();
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXstopMoveSouth(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if (((mountMotion.GoToIsActiveInRA==false) ||
                (mountMotion.GoToIsActiveInDecl==false)) &&
                (mountMotion.DeclDriveIsMoving == true))  {
            this->declinationMoveHandboxDown();
        }
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXslowSpeed(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        ui->rbCorrSpeed->setChecked(true);
        this->setCorrectionSpeed();
    }
}

//---------------------------------------------------------------------
// motion via LX 200
void MainWindow::LXhiSpeed(void) {
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        ui->rbMoveSpeed->setChecked(true);
        this->setMoveSpeed();
    }
}

//---------------------------------------------------------------------
// change LX 200 number format
void MainWindow::LXSetNumberFormatToSimple(void) {

    if (ui->cbLXSimpleNumbers->isChecked() == true) {
        this->lx200port->setNumberFormat(true);
    } else {
        this->lx200port->setNumberFormat(false);
    }
}

//---------------------------------------------------------------------
// open the serial port for LX200 communication
void MainWindow::openPort(void) {
      this->LX200SerialPortIsUp = 1;
      if (!lx200SerialPort->open(QIODevice::ReadWrite)) {
          this->LX200SerialPortIsUp = 0;
    } else {
        this->lx200SerialPort->setBreakEnabled(false);
        this->LX200SerialPortIsUp = 1;
        this->lx200SerialPort->clear(QSerialPort::AllDirections);
    }
      this->lx200port->clearReplyString();
}

//---------------------------------------------------------------------
// close the serial port for LX 200 commuinication
void MainWindow::shutDownPort(void) {
    this->lx200SerialPort->setBreakEnabled(true);
    this->LX200SerialPortIsUp = 0;
    this->lx200SerialPort->clear(QSerialPort::AllDirections);
    this->lx200SerialPort->close();
    this->lx200port->clearReplyString();
}


//---------------------------------------------------------------------
// enable or disable the serial port
void MainWindow::switchToLX200(void) {

    if ((this->lx200IsOn==false) && (this->LXSocket->isOpen() == false)) {
        this->openPort();
        if (this->LX200SerialPortIsUp == true) {
            this->lx200IsOn=true;
            ui->pbLX200Active->setText("Deactivate LX200");
            ui->cbRS232Open->setChecked(true);
            ui->tabLXTCP->setEnabled(false);
        }
    } else {
        this->shutDownPort();
        this->lx200IsOn=false;
        ui->pbLX200Active->setText("Activate LX200");
        ui->cbRS232Open->setChecked(false);
        ui->tabLXTCP->setEnabled(true);
    }
}

//--------------------------------------------------------------
//--------------------------------------------------------------
//--------------------------------------------------------------
// routines for handling the handbox
//--------------------------------------------------------------
//--------------------------------------------------------------
//--------------------------------------------------------------
void MainWindow::declinationMoveHandboxUp(void) {
    long maxDeclSteps;

    if (this->mountMotion.DeclDriveIsMoving==false){
        this->mountMotion.DeclMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
        ui->pbPGDecMinus->setEnabled(false);
        ui->pbPGDecPlus->setEnabled(false);
        ui->pbPGRAMinus->setEnabled(false);
        ui->pbPGRAPlus->setEnabled(false);
        ui->pbDeclDown->setEnabled(0);
        this->setControlsForDeclTravel(false);
        this->mountMotion.DeclDriveIsMoving=true;
        maxDeclSteps=180.0/g_AllData->getGearData(7)*g_AllData->getGearData(8)*
                g_AllData->getGearData(4)*g_AllData->getGearData(5)*
                g_AllData->getGearData(6); // travel 180° at most
        this->mountMotion.DeclDriveDirection=1;
        futureStepperBehaviourDecl = QtConcurrent::run(this->StepperDriveDecl,
                &QtKineticStepper::travelForNSteps,maxDeclSteps,
                this->mountMotion.DeclDriveDirection, this->mountMotion.DeclSpeedFactor,1);
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    } else {
        this->mountMotion.DeclDriveIsMoving=false;
        this->StepperDriveDecl->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        ui->pbDeclDown->setEnabled(1);
        this->setControlsForDeclTravel(true);
        ui->pbPGDecMinus->setEnabled(true);
        ui->pbPGDecPlus->setEnabled(true);
        ui->pbPGRAMinus->setEnabled(true);
        ui->pbPGRAPlus->setEnabled(true);
        if (ui->rbMoveSpeed->isChecked()==false) {
            ui->sbMoveSpeed->setEnabled(true);
        } else {
            ui->sbMoveSpeed->setEnabled(false);
        }
    }
}

//--------------------------------------------------------------
void MainWindow::declinationMoveHandboxDown(void) {
    long maxDeclSteps;

    if (this->mountMotion.DeclDriveIsMoving==false){
        ui->pbPGDecMinus->setEnabled(false);
        ui->pbPGDecPlus->setEnabled(false);
        ui->pbPGRAMinus->setEnabled(false);
        ui->pbPGRAPlus->setEnabled(false);
        this->mountMotion.DeclMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
        ui->pbDeclUp->setEnabled(0);
        this->setControlsForDeclTravel(false);
        this->mountMotion.DeclDriveIsMoving=true;
        this->mountMotion.DeclDriveDirection = -1;
        maxDeclSteps=180.0/g_AllData->getGearData(7)*g_AllData->getGearData(8)*
                g_AllData->getGearData(4)*g_AllData->getGearData(5)*
                g_AllData->getGearData(6); // travel 180° at most
        futureStepperBehaviourDecl =
                QtConcurrent::run(this->StepperDriveDecl,
                &QtKineticStepper::travelForNSteps,maxDeclSteps,
                                  this->mountMotion.DeclDriveDirection,
                                  this->mountMotion.DeclSpeedFactor,1);
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    } else {
        this->mountMotion.DeclDriveIsMoving=false;
        this->StepperDriveDecl->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        ui->pbPGDecMinus->setEnabled(true);
        ui->pbPGDecPlus->setEnabled(true);
        ui->pbPGRAMinus->setEnabled(true);
        ui->pbPGRAPlus->setEnabled(true);
        ui->pbDeclUp->setEnabled(1);
        this->setControlsForDeclTravel(true);
        if (ui->rbMoveSpeed->isChecked()==false) {
            ui->sbMoveSpeed->setEnabled(true);
        } else {
            ui->sbMoveSpeed->setEnabled(false);
        }
    }
}
//--------------------------------------------------------------
void MainWindow::RAMoveHandboxFwd(void) {
    long maxRASteps;
    long fwdFactor;

    if (this->mountMotion.RATrackingIsOn == true) {
        this->stopRATracking();
    }
    ui->rbCorrSpeed->setEnabled(false);
    ui->rbMoveSpeed->setEnabled(false);
    ui->sbMoveSpeed->setEnabled(false);
    ui->pbPGRAMinus->setEnabled(false);
    ui->pbPGRAPlus->setEnabled(false);
    ui->pbPGDecMinus->setEnabled(false);
    ui->pbPGDecPlus->setEnabled(false);
    if (this->mountMotion.RADriveIsMoving ==false){
        this->mountMotion.RAMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
        ui->pbRAMinus->setEnabled(0);
        ui->pbStartTracking->setEnabled(0);
        ui->pbStopTracking->setEnabled(0);
        this->setControlsForRATravel(false);
        this->mountMotion.RADriveIsMoving=true;
        maxRASteps=180/g_AllData->getGearData(3)*g_AllData->getGearData(8)*
                g_AllData->getGearData(0)*g_AllData->getGearData(1)*
                g_AllData->getGearData(2); // travel 180° at most

        this->mountMotion.RADriveDirection=1;
        fwdFactor = this->mountMotion.RASpeedFactor+1; // forward motion means increase the speed
        futureStepperBehaviourRA =
                QtConcurrent::run(this->StepperDriveRA,
                &QtContinuousStepper::travelForNSteps,maxRASteps,
                                  this->mountMotion.RADriveDirection,
                                  fwdFactor,true);
        while (!futureStepperBehaviourRA.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    } else {
        this->mountMotion.RADriveIsMoving=false;
        this->StepperDriveRA->stopDrive();
        while (!futureStepperBehaviourRA.isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        if (this->mountMotion.RATrackingIsOn == false) {
            this->setControlsForRATravel(true);
        }
        this->startRATracking();
        ui->pbRAMinus->setEnabled(1);
        ui->rbCorrSpeed->setEnabled(true);
        ui->rbMoveSpeed->setEnabled(true);
        ui->pbPGRAMinus->setEnabled(true);
        ui->pbPGRAPlus->setEnabled(true);
        ui->pbPGDecMinus->setEnabled(true);
        ui->pbPGDecPlus->setEnabled(true);
        if (ui->rbMoveSpeed->isChecked()==false) {
            ui->sbMoveSpeed->setEnabled(true);
        }
    }
}

//---------------------------------------------------------------------
void MainWindow::RAMoveHandboxBwd(void) {
    long maxRASteps;
    double bwdFactor;

    if (this->mountMotion.RATrackingIsOn == true) {
        this->stopRATracking();
    }
    ui->rbCorrSpeed->setEnabled(false);
    ui->rbMoveSpeed->setEnabled(false);
    ui->sbMoveSpeed->setEnabled(false);
    ui->pbPGRAMinus->setEnabled(false);
    ui->pbPGRAPlus->setEnabled(false);
    ui->pbPGDecMinus->setEnabled(false);
    ui->pbPGDecPlus->setEnabled(false);
    if (this->mountMotion.RADriveIsMoving ==false){
        this->mountMotion.RAMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
        ui->pbRAPlus->setEnabled(0);
        setControlsForRATravel(false);
        ui->pbStartTracking->setEnabled(0);
        ui->pbStopTracking->setEnabled(0);
        this->mountMotion.RADriveIsMoving=true;
        maxRASteps=180/g_AllData->getGearData(3)*g_AllData->getGearData(8)*
                g_AllData->getGearData(0)*g_AllData->getGearData(1)*
                g_AllData->getGearData(2); // travel 180° at most

        this->mountMotion.RADriveDirection=-1;
        bwdFactor=this->mountMotion.RASpeedFactor-1; // backward motion means stop at tracking speeds
        futureStepperBehaviourRA = QtConcurrent::run(this->StepperDriveRA,
                &QtContinuousStepper::travelForNSteps,maxRASteps, this->mountMotion.RADriveDirection,
                bwdFactor,true);
        while (!futureStepperBehaviourRA.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    } else {
        this->mountMotion.RADriveIsMoving=false;
        this->StepperDriveRA->stopDrive();
        while (!futureStepperBehaviourRA.isFinished()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        if (this->mountMotion.RATrackingIsOn == false) {
            this->setControlsForRATravel(true);
        }
        this->startRATracking();
        ui->pbRAPlus->setEnabled(1);
        ui->rbCorrSpeed->setEnabled(true);
        ui->rbMoveSpeed->setEnabled(true);
        ui->pbPGRAMinus->setEnabled(true);
        ui->pbPGRAPlus->setEnabled(true);
        ui->pbPGDecMinus->setEnabled(true);
        ui->pbPGDecPlus->setEnabled(true);
        if (ui->rbMoveSpeed->isChecked()==false) {
            ui->sbMoveSpeed->setEnabled(true);
        }
    }
}

//--------------------------------------------------------------
//--------------------------------------------------------------
//--------------------------------------------------------------
// pulse guide routines and ST 4
//--------------------------------------------------------------
//--------------------------------------------------------------
//--------------------------------------------------------------
// slot called by timeout of ST4Timer
void MainWindow::readST4Port(void) {
    if (this->guidingState.st4IsActive== true) {
        this->handleST4State();
    }
}

//--------------------------------------------------------------
// instantiates all variables for ST4
void MainWindow::startST4Guiding(void) {
    if (this->mountMotion.RATrackingIsOn==false) {
        this->startRATracking();
    } // if tracking is not active - start it ...
    this->guidingState.st4IsActive=true;
    this->guidingState.guidingIsOn=true;
    ui->ctrlTab->setEnabled(false);
    ui->catTab->setEnabled(false);
    ui->camTab->setEnabled(false);
    ui->gearTab->setEnabled(false);
    ui->tabLX200->setEnabled(false);
    ui->gbBluetooth->setEnabled(false);
    ui->gbINDI->setEnabled(false);
    ui->pbStartST4->setEnabled(false);
    ui->pbStopST4->setEnabled(true);
    this->ST4stateDurations.declTimeMeasurementActive=false;
    this->ST4stateDurations.RATimeMeasurementActive=false;
    this->ST4stateDurations.dpDuration=0;
    this->ST4stateDurations.dmDuration=0;
    this->ST4stateDurations.rpDuration=0;
    this->ST4stateDurations.rmDuration=0;
}

//--------------------------------------------------------------
void MainWindow::stopST4Guiding(void) {
    this->guidingState.st4IsActive=false;
    this->guidingState.guidingIsOn=false;
    ui->ctrlTab->setEnabled(true);
    ui->catTab->setEnabled(true);
    ui->camTab->setEnabled(true);
    ui->gearTab->setEnabled(true);
    ui->tabLX200->setEnabled(true);
    ui->gbBluetooth->setEnabled(true);
    ui->gbINDI->setEnabled(true);
    ui->pbStartST4->setEnabled(true);
    ui->pbStopST4->setEnabled(false);
}

//--------------------------------------------------------------
// reads the GPIO pins with the ST4 signals; it measures the
// duration of the signal uptime, the triggers a drive pulse for the
// given amount of time ... kind of a workaround, but honestly speaking,
// monitoring the gpio pins while running the drives is quite a hassle
// while running into the same sampling problem ...

void MainWindow::handleST4State(void) {
    short dp, rm, dm, rp;

    if (this->guidingState.st4IsActive==true) {
        dp=abs(1-digitalRead(2));
        rm=abs(1-digitalRead(3));
        dm=abs(1-digitalRead(4));
        rp=abs(1-digitalRead(5)); // reading the GPIO pins
        if (dp > 0) {
            if (ui->cbST4North->isChecked()==false) { // if pin goes UP ...
                ui->cbST4North->setChecked(true); // ... check the checkbox ...
                this->ST4stateDurations.declTimeMeasurementActive = true; // ... set a flag that time is measured to TRUE ...
                this->ST4stateDurations.dElapsed.start(); // ... and start a timer for declination
            }
        } else {
            if (ui->cbST4North->isChecked()==true) { // if pin goes DOWN ...
                ui->cbST4North->setChecked(false);
                this->ST4stateDurations.declTimeMeasurementActive = false; // ... set the time measurement flag to FALSE ...
                this->ST4stateDurations.dpDuration=this->ST4stateDurations.dElapsed.elapsed(); // ... and determine the time it was up.
            }
        }
        if (rp > 0) {
            if (ui->cbST4West->isChecked()==false) {
                ui->cbST4West->setChecked(true);
                this->ST4stateDurations.RATimeMeasurementActive = true;
                this->ST4stateDurations.rElapsed.start();
            }
        } else {
            if (ui->cbST4West->isChecked()==true) {
                ui->cbST4West->setChecked(false);
                this->ST4stateDurations.RATimeMeasurementActive = false;
                this->ST4stateDurations.rpDuration=this->ST4stateDurations.rElapsed.elapsed();
            }
        }
        if (dm > 0) {
            if (ui->cbST4South->isChecked()==false) {
                ui->cbST4South->setChecked(true);
                this->ST4stateDurations.declTimeMeasurementActive = true;
                this->ST4stateDurations.dElapsed.start();
            }
        } else {
            if (ui->cbST4South->isChecked()==true) {
                ui->cbST4South->setChecked(false);
                this->ST4stateDurations.declTimeMeasurementActive = false;
                this->ST4stateDurations.dmDuration=this->ST4stateDurations.dElapsed.elapsed();
            }
        }
        if (rm > 0) {
            if (ui->cbST4East->isChecked()==false) {
                ui->cbST4East->setChecked(true);
                this->ST4stateDurations.RATimeMeasurementActive = true;
                this->ST4stateDurations.rElapsed.start();
            }
        } else {
            if (ui->cbST4East->isChecked()==true) {
                ui->cbST4East->setChecked(false);
                this->ST4stateDurations.RATimeMeasurementActive = false;
                this->ST4stateDurations.rmDuration=this->ST4stateDurations.rElapsed.elapsed();
            }
        }
        if (this->ST4stateDurations.declTimeMeasurementActive == false) {
            if (this->ST4stateDurations.dpDuration > 1) {
                ui->lcdPulseDecl->display((int)this->ST4stateDurations.dpDuration);
                ui->cbST4North->setChecked(true);
                this->declPGPlusGd(this->ST4stateDurations.dpDuration);          
                ui->cbST4North->setChecked(false);
                this->ST4stateDurations.dpDuration = 0;
            }
            if (this->ST4stateDurations.dmDuration > 1) {
                ui->lcdPulseDecl->display((int)this->ST4stateDurations.dmDuration);
                ui->cbST4South->setChecked(true);
                this->declPGMinusGd(this->ST4stateDurations.dmDuration);
                ui->cbST4South->setChecked(false);
                this->ST4stateDurations.dmDuration = 0;
            }
        }
        if (this->ST4stateDurations.RATimeMeasurementActive == false) {
            if (this->ST4stateDurations.rpDuration > 1) {
                ui->lcdPulseDecl->display((int)this->ST4stateDurations.rpDuration);
                ui->cbST4West->setChecked(true);
                this->raPGFwdGd(this->ST4stateDurations.rpDuration);
                ui->cbST4West->setChecked(false);
                this->ST4stateDurations.rpDuration = 0;
            }
            if (this->ST4stateDurations.rmDuration > 1) {
                ui->lcdPulseDecl->display((int)this->ST4stateDurations.rmDuration);
                ui->cbST4East->setChecked(true);
                this->raPGBwdGd(this->ST4stateDurations.rmDuration);
                ui->cbST4East->setChecked(false);
                this->ST4stateDurations.rmDuration = 0;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

//--------------------------------------------------------------
void MainWindow::declPGPlus(void) {
    long duration;

    duration = ui->sbPulseGuideDuration->value();
    declinationPulseGuide(duration, 1,true);
}

//--------------------------------------------------------------
void MainWindow::declPGMinusGd(long duration) {

    declinationPulseGuide(duration, -1,false);
    qDebug() << "Duration" << duration;
}

//--------------------------------------------------------------
void MainWindow::declPGPlusGd(long duration) {

    declinationPulseGuide(duration, 1,false);
    qDebug() << "Duration" << duration;
}

//--------------------------------------------------------------
void MainWindow::declPGMinus(void) {
    long duration;

    duration = ui->sbPulseGuideDuration->value();
    declinationPulseGuide(duration, -1,true);
}
//--------------------------------------------------------------
void MainWindow::declinationPulseGuide(long pulseDurationInMS, short direction, bool isThreaded) {
    long steps;
    double declSpeed;
    short ldir;

    this->setControlsForDeclTravel(false);
    ui->pbDeclDown->setEnabled(false);
    ui->pbDeclUp->setEnabled(false);
    ui->pbRAMinus->setEnabled(false);
    ui->pbRAPlus->setEnabled(false);
    if (this->mountMotion.DeclDriveIsMoving==true){
        this->mountMotion.DeclDriveIsMoving=false;
        this->StepperDriveDecl->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    } // if the decl drive was moving, it is now set to stop
    this->setCorrectionSpeed();
    ui->rbCorrSpeed->setChecked(true); // switch to correction speed
    declSpeed = g_AllData->getCelestialSpeed()*
            (g_AllData->getGearData(4))*
            (g_AllData->getGearData(5))*
            (g_AllData->getGearData(6))*
            (g_AllData->getGearData(8))/(g_AllData->getGearData(7));
    if (direction < 0) {
        ldir = -1;
    } else {
        ldir = 1;
    }
    steps = declSpeed*(pulseDurationInMS/1000.0);
    if (steps>1) { // controller cannot do only one microstep
        this->mountMotion.DeclDriveDirection=ldir;
        this->mountMotion.DeclMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
        this->mountMotion.DeclDriveIsMoving=true;
        if (isThreaded == true) {
            futureStepperBehaviourDecl = QtConcurrent::run(this->StepperDriveDecl,
                &QtKineticStepper::travelForNSteps,steps,
                this->mountMotion.DeclDriveDirection,this->mountMotion.DeclSpeedFactor,0);
            while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
        } else {
            if (pulseDurationInMS < 2000) { // don't do corrections > 2 s in guiding
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                this->StepperDriveDecl->travelForNSteps(steps,this->mountMotion.DeclDriveDirection,
                    this->mountMotion.DeclSpeedFactor,0);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
        }
    }
    this->mountMotion.DeclDriveIsMoving=false;
    this->setControlsForDeclTravel(true);
    ui->pbDeclDown->setEnabled(true);
    ui->pbDeclUp->setEnabled(true);
    ui->pbRAMinus->setEnabled(true);
    ui->pbRAPlus->setEnabled(true);
}

//---------------------------------------------------------------------
void MainWindow::raPGFwd(void) {
    long duration;

    duration = ui->sbPulseGuideDuration->value();
    raPulseGuide(duration,1,true);
}

//---------------------------------------------------------------------
void MainWindow::raPGBwd(void) {
    long duration;

    duration = ui->sbPulseGuideDuration->value();
    raPulseGuide(duration,-1,true);
}

//---------------------------------------------------------------------
void MainWindow::raPGFwdGd(long duration) {

    raPulseGuide(duration,1,false);
    qDebug() << "Duration" << duration;
}

//---------------------------------------------------------------------
void MainWindow::raPGBwdGd(long duration) {

    raPulseGuide(duration,-1,false);
    qDebug() << "Duration" << duration;
}
//---------------------------------------------------------------------
void MainWindow::raPulseGuide(long pulseDurationInMS, short direction, bool isThreaded) {
    long steps;
    double raSpeed,pgFactor;
    QElapsedTimer *localTimer;

    if (this->mountMotion.RATrackingIsOn) {
        this->stopRATracking();
    }
    if (this->mountMotion.RADriveIsMoving==true){
        this->mountMotion.RADriveIsMoving=false;
        this->StepperDriveRA->stopDrive();
        while (!futureStepperBehaviourDecl.isFinished()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }
    this->setControlsForRATravel(false);
    ui->pbStartTracking->setEnabled(0);
    ui->pbStopTracking->setEnabled(0);
    ui->pbRAMinus->setEnabled(0);
    ui->pbRAPlus->setEnabled(0); // if the RA drive was moving, it is now set to stop
    ui->pbDeclDown->setEnabled(0);
    ui->pbDeclUp->setEnabled(0);
    this->setCorrectionSpeed();
    ui->rbCorrSpeed->setChecked(true); // switch to correction speed
    if (direction < 0) {
        direction = -1;
         pgFactor=this->mountMotion.RASpeedFactor-1;
    } else {
        direction = 1;
        pgFactor=this->mountMotion.RASpeedFactor+1;
    }
    if (direction == 1) {
        this->mountMotion.RADriveDirection=direction;
        raSpeed=g_AllData->getCelestialSpeed()*
                (g_AllData->getGearData(0))*
                (g_AllData->getGearData(1))*
                (g_AllData->getGearData(2))*
                (g_AllData->getGearData(8))/(g_AllData->getGearData(3));
        steps = direction*pgFactor*raSpeed*(pulseDurationInMS/1000.0);
        if (steps>1) { // controller cannot do only one microstep
            this->mountMotion.RAMoveElapsedTimeInMS = g_AllData->getTimeSinceLastSync();
            this->mountMotion.RADriveIsMoving=true;
            if (isThreaded == true) {
                this->futureStepperBehaviourRA =
                    QtConcurrent::run(this->StepperDriveRA, &QtContinuousStepper::travelForNSteps,steps,
                        this->mountMotion.RADriveDirection,pgFactor,false);
                while (!futureStepperBehaviourRA.isFinished()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
            } else {
                if (pulseDurationInMS < 2000) { // in guiding, do less that 2 s corrections
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    this->StepperDriveRA->travelForNSteps(steps,this->mountMotion.RADriveDirection,
                        pgFactor,false);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
            }
        }
    } else {
        localTimer = new QElapsedTimer();
        localTimer->start();
        while (localTimer->elapsed() < pulseDurationInMS) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            // that one is easy - just stop the drive for a given amount of time
        }
        delete localTimer;
    }
    this->mountMotion.RADriveIsMoving=false;
    ui->pbRAMinus->setEnabled(1);
    ui->pbRAPlus->setEnabled(1);
    ui->pbDeclDown->setEnabled(1);
    ui->pbDeclUp->setEnabled(1);
    this->setControlsForRATravel(true);
    this->startRATracking();
}

//-----------------------------------------------------------------------
// this one is called during guiding. it compensates for declination backlash if
// the direction changes.
void MainWindow::compensateDeclBacklashPG(short ddir) {
    long compSteps;

    if (this->guidingState.systemIsCalibrated == true) { // compensate backlash only if system was calibrated ...
        compSteps=round((g_AllData->getCelestialSpeed()*(g_AllData->getGearData(4))*
                (g_AllData->getGearData(5))*(g_AllData->getGearData(6))*
                (g_AllData->getGearData(8))/(g_AllData->getGearData(7)))*
                (this->guidingState.backlashCompensationInMS/1000.0)); // sidereal speed in declination*compensation time in s == # of steps for compensation
        this->StepperDriveDecl->travelForNSteps(compSteps, ddir, 1,false);
    }
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// routines for GUI enabling and disabling
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
void MainWindow::setControlsForGuiding(bool isEnabled) {
    ui->pbTrainAxes->setEnabled(isEnabled);
    ui->sbPulseGuideDuration->setEnabled(isEnabled);
    ui->pbPGDecMinus->setEnabled(isEnabled);
    ui->pbPGDecPlus->setEnabled(isEnabled);
    ui->pbPGRAMinus->setEnabled(isEnabled);
    ui->pbPGRAPlus->setEnabled(isEnabled);
    ui->pbSelectGuideStar->setEnabled(isEnabled);
    ui->sbExposureTime->setEnabled(isEnabled);
    ui->tabCCDAcq->setEnabled(isEnabled);
    ui->pbSelectGuideStar->setEnabled(isEnabled);
    ui->hsThreshold->setEnabled(isEnabled);
    ui->hsIContrast->setEnabled(isEnabled);
    ui->hsIBrightness->setEnabled(isEnabled);
    ui->cbMedianFilter->setEnabled(isEnabled);
    ui->rbFOVDbl->setEnabled(isEnabled);
    ui->rbFOVHalf->setEnabled(isEnabled);
    ui->rbFOVStd->setEnabled(isEnabled);
    ui->gearTab->setEnabled(isEnabled);
    ui->LX200Tab->setEnabled(isEnabled);
    ui->catTab->setEnabled(isEnabled);
    ui->ctrlTab->setEnabled(isEnabled);
    ui->hsThreshold->setEnabled(isEnabled);
    if (isEnabled == true) {
        if ((this->lx200IsOn) && (this->LXSocket->isOpen())) {
            ui->pbLX200Active->setEnabled(false);
        }
    }
}

//---------------------------------------------------------------------
void MainWindow::setControlsForGoto(bool isEnabled) {
    ui->sbGoToSpeed->setEnabled(isEnabled);
    ui->listWidgetObject->setEnabled(isEnabled);
    ui->listWidgetCatalog->setEnabled(isEnabled);
    ui->pbSync->setEnabled(isEnabled);
    ui->pbDeclDown->setEnabled(isEnabled);
    ui->pbDeclUp->setEnabled(isEnabled);
    ui->pbRAPlus->setEnabled(isEnabled);
    ui->pbRAMinus->setEnabled(isEnabled);
    this->setControlsForDeclTravel(isEnabled);
    this->setControlsForRATravel(isEnabled);
    ui->rbCorrSpeed->setEnabled(isEnabled);
    ui->rbMoveSpeed->setEnabled(isEnabled);
    ui->sbMoveSpeed->setEnabled(isEnabled);
    ui->pbStoreDrive->setEnabled(isEnabled);
    ui->pbStoreGears->setEnabled(isEnabled);
    ui->pbLX200Active->setEnabled(isEnabled);
    ui->pbPGDecMinus->setEnabled(isEnabled);
    ui->pbPGDecPlus->setEnabled(isEnabled);
    ui->pbPGRAMinus->setEnabled(isEnabled);
    ui->pbPGRAPlus->setEnabled(isEnabled);
    ui->LX200Tab->setEnabled(isEnabled);
    ui->INDITab->setEnabled(isEnabled);
    ui->gearTab->setEnabled(isEnabled);
    ui->camTab->setEnabled(isEnabled);
    ui->gbCoordinates->setEnabled(isEnabled);
    ui->gbGeneralSettings->setEnabled(true);
    ui->rbSiderealSpeed->setEnabled(isEnabled);
    ui->rbLunarSpeed->setEnabled(isEnabled);
    ui->rbSolarSpeed->setEnabled(isEnabled);
    ui->pbMeridianFlip->setEnabled(isEnabled);
    ui->cbIsOnNorthernHemisphere->setEnabled(isEnabled);
    ui->pbConveyCoordinates->setEnabled(isEnabled);
    ui->sbRAhours->setEnabled(isEnabled);
    ui->sbRAmins->setEnabled(isEnabled);
    ui->sbRASecs->setEnabled(isEnabled);
    ui->sbDeclDegrees->setEnabled(isEnabled);
    ui->sbDeclMin->setEnabled(isEnabled);
    ui->sbDeclSec->setEnabled(isEnabled);
    if (isEnabled == true) {
        if ((this->lx200IsOn) && (this->LXSocket->isOpen())) {
            ui->pbLX200Active->setEnabled(false);
        }
    }
}
//---------------------------------------------------------------------
void MainWindow::setControlsForRATracking(bool isEnabled) {
    ui->leAMaxRA->setEnabled(isEnabled);
    ui->leCurrMaxRA->setEnabled(isEnabled);
    ui->leRAPlanetary->setEnabled(isEnabled);
    ui->leRAGear->setEnabled(isEnabled);
    ui->leRAWorm->setEnabled(isEnabled);
    ui->leRAStepsize->setEnabled(isEnabled);
    ui->leMicrosteps->setEnabled(isEnabled);
    ui->cbIsOnNorthernHemisphere->setEnabled(isEnabled);
}

//---------------------------------------------------------------------
void MainWindow::setControlsForRATravel(bool isEnabled) {
    ui->leAMaxRA->setEnabled(isEnabled);
    ui->leCurrMaxRA->setEnabled(isEnabled);
    ui->leRAPlanetary->setEnabled(isEnabled);
    ui->leRAGear->setEnabled(isEnabled);
    ui->leRAWorm->setEnabled(isEnabled);
    ui->leRAStepsize->setEnabled(isEnabled);
    ui->leMicrosteps->setEnabled(isEnabled);
    ui->cbIsOnNorthernHemisphere->setEnabled(isEnabled);
    ui->pbPGRAMinus->setEnabled(isEnabled);
    ui->pbPGRAPlus->setEnabled(isEnabled);
    ui->listWidgetCatalog->setEnabled(isEnabled);
    ui->listWidgetObject->setEnabled(isEnabled);
    ui->pbSync->setEnabled(isEnabled);
    ui->pbGoTo->setEnabled(isEnabled);
    ui->pbStop2->setEnabled(true);
}

//---------------------------------------------------------------------
void MainWindow::setControlsForDeclTravel(bool isEnabled) {
    ui->leAMaxDecl->setEnabled(isEnabled);
    ui->leCurrMaxDecl->setEnabled(isEnabled);
    ui->rbCorrSpeed->setEnabled(isEnabled);
    ui->rbMoveSpeed->setEnabled(isEnabled);
    ui->sbMoveSpeed->setEnabled(isEnabled);
    ui->leDeclPlanetary->setEnabled(isEnabled);
    ui->leDeclGear->setEnabled(isEnabled);
    ui->leDeclWorm->setEnabled(isEnabled);
    ui->leDeclStepSize->setEnabled(isEnabled);
    ui->leMicrosteps->setEnabled(isEnabled);
    ui->pbPGDecMinus->setEnabled(isEnabled);
    ui->pbPGDecPlus->setEnabled(isEnabled);
    ui->listWidgetCatalog->setEnabled(isEnabled);
    ui->listWidgetObject->setEnabled(isEnabled);
    ui->pbSync->setEnabled(isEnabled);
    ui->pbGoTo->setEnabled(isEnabled);
    ui->pbStop2->setEnabled(true);
}

//---------------------------------------------------------------------
void MainWindow::setControlsForAutoguiderCalibration(bool isEnabled) {
    ui->ctrlTab->setEnabled(isEnabled);
    ui->catTab->setEnabled(isEnabled);
    ui->LX200Tab->setEnabled(isEnabled);
    this->camView->setEnabled(isEnabled);
    ui->tabCCDAcq->setEnabled(isEnabled);
    ui->tabGuideParams->setEnabled(isEnabled);
    ui->tabImageProc->setEnabled(isEnabled);
    ui->pbTrainAxes->setEnabled(isEnabled);
    ui->sbPulseGuideDuration->setEnabled(isEnabled);
    ui->pbPGDecPlus->setEnabled(isEnabled);
    ui->pbPGDecMinus->setEnabled(isEnabled);
    ui->pbPGRAMinus->setEnabled(isEnabled);
    ui->pbPGRAPlus->setEnabled(isEnabled);
    ui->gearTab->setEnabled(isEnabled);
    ui->teCalibrationStatus->setEnabled(true);
    if (isEnabled == true) {
        if ((this->lx200IsOn) && (this->LXSocket->isOpen())) {
            ui->pbLX200Active->setEnabled(false);
        }
    }
}

//------------------------------------------------------------------
void MainWindow::setINDIrbuttons(bool isEnabled) {
    ui->rbMoravian->setEnabled(isEnabled);
    ui->rbQHYINDI->setEnabled(isEnabled);
    ui->rbV4L2INDI->setEnabled(isEnabled);
    ui->rbZWOINDI->setEnabled(isEnabled);
    ui->rbSLXPress->setEnabled(isEnabled);
}

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// routines for handling the .tsc catalogs
//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// this one loads a catalog according to the catalog name selected
// in the list view. catalogs have to be in a subdirectory "Catalogs"
// in the working directory.
void MainWindow::catalogChosen(QListWidgetItem* catalogName) {
    QString *catalogPath,*catName;
    long counterForObjects, maxObj;
    std::string objectName;

    ui->listWidgetCatalog->blockSignals(true);
    if (this->objCatalog != NULL) {
        delete this->objCatalog;
    }
    if (g_AllData->wasMountSynced() == true) {
        ui->pbGoTo->setEnabled(true);
    }
    ui->pbSync->setEnabled(false);
    catalogPath = new QString("Catalogs/");
    catName = new QString(catalogName->text());
    catalogPath->append(catName);
    catalogPath->append(QString(".tsc"));
    this->objCatalog = new currentObjectCatalog(*catalogPath);
    maxObj = this->objCatalog->getNumberOfObjects();
    ui->listWidgetObject->clear();
    for (counterForObjects = 0; counterForObjects < maxObj; counterForObjects++) {
        objectName=this->objCatalog->getNamesOfObjects(counterForObjects);
        ui->listWidgetObject->addItem(QString(objectName.data()));
    }
    ui->lcdCatEpoch->display(QString::number(this->objCatalog->getEpoch()));
    ui->listWidgetCatalog->blockSignals(false);
    delete catalogPath;
    delete catName;
}
//------------------------------------------------------------------
// load the catalog itself with coordinates and convert them to the current epoch
// if needed
void MainWindow::catalogObjectChosen(void) {
    QString lestr;
    long indexInList;
    double epRA, epDecl, meeusM, meeusN, deltaRA, deltaDecl,raRadians, declRadians;

    indexInList = ui->listWidgetObject->currentRow();
    if (this->objCatalog != NULL) {
        epRA=this->objCatalog->getRADec(indexInList);
        epDecl=this->objCatalog->getDeclDec(indexInList);
        if (ui->cbConvertToCurrentEpoch->isChecked()==false) {
            this->ra=epRA;
            this->decl=epDecl;
        } else {
            meeusM=(3.07234+0.00186*((ui->sbEpoch->value()-1900)/100.0))*0.00416667; // factor m, J. Meeus, 3. ed, p.63, given in degrees
            meeusN=(20.0468-0.0085*((ui->sbEpoch->value()-1900)/100.0))/(3600.0);  // factor n, in degrees
            raRadians=epRA/180.0*3.141592653589793;
            declRadians=epDecl/180.0*3.141592653589793;
            deltaRA = meeusM+meeusN*sin(raRadians)*tan(declRadians);
            deltaDecl = meeusN*cos(raRadians);
            this->ra=epRA+deltaRA*((double)(ui->sbEpoch->value()-ui->lcdCatEpoch->value()));
            if (this->ra > 360) {
                this->ra-=360;
            } // that one is clear,right - avoid more than 24h RA ...
            this->decl=epDecl+deltaDecl*((double)(ui->sbEpoch->value()-ui->lcdCatEpoch->value()));
            if (this->decl>90) {
                this->decl = 90; // don't know what else to do here
            }
            if (this->decl<-90) {
                this->decl = -90;
            }
        }
        lestr = QString::number(this->ra, 'g', 8);
        ui->lineEditRA->setText(lestr);
        lestr = QString::number(this->decl, 'g', 8);
        ui->lineEditDecl->setText(lestr);
        ui->pbSync->setEnabled(true);
    }
}

//------------------------------------------------------------------
// slot that conveys manual coordinates to the controller
void MainWindow::transferCoordinates(void) {
    short rah, ram, declDeg, declMin;
    float ras, declSec;
    double lRA, lDecl, lSubVal;
    QString lestr;

    ui->pbGoTo->setEnabled(false);
    rah = ui->sbRAhours->value();
    ram = ui->sbRAmins->value();
    ras = ui->sbRASecs->value();
    declDeg = ui->sbDeclDegrees->value();
    declMin = ui->sbDeclMin->value();
    declSec = ui->sbDeclSec->value();
    lRA = ras/3600.0+ram/60.0+rah*15.0;
    lSubVal = declSec/3600.0+declMin/60.0;
    if (declDeg < 0) {
        lDecl = declDeg-lSubVal;
    } else {
        lDecl = declDeg+lSubVal;
    }
    this->ra=lRA;
    this->decl=lDecl;
    lestr = QString::number(this->ra, 'g', 8);
    ui->lineEditRA->setText(lestr);
    lestr = QString::number(this->decl, 'g', 8);
    ui->lineEditDecl->setText(lestr);
    ui->pbSync->setEnabled(true);
    ui->pbGoTo->setEnabled(true);
}

//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
// store drive and gear data to g_AllData and preference file
//------------------------------------------------------------------
//------------------------------------------------------------------
//------------------------------------------------------------------
void MainWindow::storeGearData(void) {
    float pgra,ogra,wormra,ssra,pgdec,ogdec,wormdec,ssdec,microsteps,vra,vdecl;
    QString *leEntry,leSpeeds;

    leEntry = new QString(ui->leRAPlanetary->text());
    pgra=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leRAGear->text());
    ogra=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leRAWorm->text());
    wormra=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leRAStepsize->text());
    ssra=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leDeclPlanetary->text());
    pgdec=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leDeclGear->text());
    ogdec=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leDeclWorm->text());
    wormdec=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leDeclStepSize->text());
    ssdec=leEntry->toFloat();
    leEntry->clear();
    leEntry->append(ui->leMicrosteps->text());
    microsteps=leEntry->toFloat();
    g_AllData->setGearData(pgra,ogra,wormra,ssra,pgdec,ogdec,wormdec,ssdec,microsteps);
    // store all gear data in global struct
    g_AllData->storeGlobalData();

    if (this->StepperDriveRA->getStopped() == false) {
        this->stopRATracking();
        StepperDriveRA->changeSpeedForGearChange();
        this->startRATracking();
    } else {
        StepperDriveRA->changeSpeedForGearChange();
    }
    StepperDriveDecl->setGearRatioAndMicrosteps(g_AllData->getGearData(4)*g_AllData->getGearData(5)*g_AllData->getGearData(6)/g_AllData->getGearData(7),g_AllData->getGearData(8));
    StepperDriveDecl->changeSpeedForGearChange();

    vra=StepperDriveRA->getKineticsFromController(3);
    leSpeeds= QString::number(vra, 'g', 2);
    ui->leVMaxRA->setText(leSpeeds);
    vdecl=StepperDriveDecl->getKineticsFromController(3);
    leSpeeds= QString::number(vdecl, 'g', 2);
    ui->leVMaxDecl->setText(leSpeeds);
    delete leEntry;
}

//---------------------------------------------------------------------
// store the drive data and convey this also to the drives
void MainWindow::storeDriveData(void) {
    g_AllData->storeGlobalData();
    this->StepperDriveRA->setStepperParams(g_AllData->getDriveParams(0,1),1);//acc
    this->StepperDriveRA->setStepperParams(g_AllData->getDriveParams(0,2),3);//current
    this->StepperDriveDecl->setStepperParams(g_AllData->getDriveParams(1,1),1);//acc
    this->StepperDriveDecl->setStepperParams(g_AllData->getDriveParams(1,2),3);//current
}

//------------------------------------------------------------------
// store data site from the GUI to the global data and to the .tsp file ...
void MainWindow::storeSiteData(void)  {
    double guilat, guilong, guiUTCOffs;
    QString *leEntry;

    leEntry = new QString(ui->leLat->text());
    guilat=leEntry->toDouble();
    leEntry->clear();
    leEntry->append(ui->leLong->text());
    guilong=leEntry->toDouble();
    leEntry->clear();
    leEntry->append(ui->leUTCOffs->text());
    guiUTCOffs=leEntry->toDouble();
    delete leEntry;
    g_AllData->setSiteParams(guilat,guilong,guiUTCOffs);
    g_AllData->setSiteParams(ui->leControllerName->text());
    g_AllData->storeGlobalData();
}

//----------------------------------------------------------------------
// slot that takes care of changing the tracking rates
void MainWindow::setTrackingRate(void) {
    double dracc, drcurr;

    if (ui->rbSiderealSpeed->isChecked()) {
        g_AllData->setCelestialSpeed(0);
    }
    if (ui->rbLunarSpeed->isChecked()) {
        g_AllData->setCelestialSpeed(1);
    }
    if (ui->rbSolarSpeed->isChecked()) {
        g_AllData->setCelestialSpeed(2);
    }
    dracc=this->StepperDriveRA->getKineticsFromController(2);
    drcurr=this->StepperDriveRA->getKineticsFromController(1);
    this->StepperDriveRA->setInitialParamsAndComputeBaseSpeed(dracc,drcurr);
    dracc=this->StepperDriveDecl->getKineticsFromController(2);
    drcurr=this->StepperDriveDecl->getKineticsFromController(1);
    this->StepperDriveDecl->setInitialParamsAndComputeBaseSpeed(dracc,drcurr);
}

//----------------------------------------------------------------------
// carry out a meridian flip for german mounts
void MainWindow::doMeridianFlip(void) {
    QString lestr;
    double targetRA, targetDecl;

    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false)
             && (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA == false)) {
        if ((mountMotion.GoToIsActiveInRA==false) || (mountMotion.GoToIsActiveInDecl== false)) {
            if (g_AllData->wasMountSynced() == true) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                targetRA=g_AllData->getActualScopePosition(0)-180.0;
                if (targetRA < 0.0) {
                    targetRA+=360.0;
                }
                this->ra = targetRA;
                targetDecl = g_AllData->getActualScopePosition(1)+180.0;
                this->decl = targetDecl;
                lestr = QString::number(this->ra, 'g', 8);
                ui->lineEditRA->setText(lestr);
                ui->leLX200RA->setText(lestr);
                lestr = QString::number(this->decl, 'g', 8);
                ui->lineEditDecl->setText(lestr);
                ui->leLX200Decl->setText(lestr);
                this->startGoToObject();
            }
        }
    }
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
// routines for handling bluetooth communications with the handbox
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
void MainWindow::startBTComm(void) { // start BT communications
    this->bt_Handbox->openPort();
    if (this->bt_Handbox->getPortState()==true) {
        ui->cbBTIsUp->setChecked(true);
    }
}

//----------------------------------------------------------------------
void MainWindow::stopBTComm(void) {  // stop BT communications
    this->bt_Handbox->shutDownPort();
    ui->cbBTIsUp->setChecked(false);
}

//----------------------------------------------------------------------
void MainWindow::restartBTComm(void) {  // try to open up the rfcommport if it failed in initialisation
    ui->leBTMACAddress->setText(*(g_AllData->getBTMACAddress()));
    this->bt_Handbox->bt_serialcommTryRestart(*(g_AllData->getBTMACAddress()));
    this->mountMotion.btMoveNorth=0;
    this->mountMotion.btMoveEast=0;
    this->mountMotion.btMoveSouth=0;
    this->mountMotion.btMoveWest=0;
}
//----------------------------------------------------------------------
// slot that responds to the strings received from the handbox via bluetooth.
// the arduino sends a string consisting of 5 characters. "1000" is north,
// "0100" is east, "0010" is south and "0001" is west. the fifth value is 0
// if the speed is single, and 1 if the speed is the "move" speed.
void MainWindow::handleBTHandbox(void) {
    QString *localBTCommand; // make a deep copy of the command string
    short speedSwitchState; // set to 1 or 0 concerning the motion speed
    QElapsedTimer *wait;

    wait = new QElapsedTimer();
    this->bt_HandboxCommand=this->bt_Handbox->getTSCcommand(); // store the command from the arduino
    localBTCommand=new QString(*bt_HandboxCommand); // make a copy of the command
    if ((this->guidingState.guidingIsOn==false) && (this->guidingState.calibrationIsRunning==false) &&
            (mountMotion.GoToIsActiveInDecl==false) && (mountMotion.GoToIsActiveInRA==false)) {
        // ignore this if system is in guiding or autoguider calibration

        speedSwitchState=(localBTCommand->right(1)).toInt(); // the last digit is the motion state
        localBTCommand->chop(1); // remove the last character
        if (speedSwitchState == 1) {
            this->setMoveSpeed();
            ui->rbMoveSpeed->setChecked(true);
        } else {
            this->setCorrectionSpeed();
            ui->rbCorrSpeed->setChecked(true);
        } // set speeds according to the last digit
        if ((this->mountMotion.RADriveIsMoving==false) && (this->mountMotion.DeclDriveIsMoving==false)) {
            // just to make sure that the handbox does not mess up a motion initiated from the GUI
            ui->fMainHBCtrl->setEnabled(false); // disable handcontrol widget
            ui->fHBSpeeds->setEnabled(false);
            ui->pbMeridianFlip->setEnabled(false);
            this->repaint();
            wait->start();
            do {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            } while (wait->elapsed() < 250);
            if (localBTCommand->compare("1000") == 0) { // start motions according the first 4 digits.
                this->mountMotion.btMoveNorth = 1;
                this->declinationMoveHandboxUp();
            } else if (localBTCommand->compare("0100") == 0) {
                this->mountMotion.btMoveWest = 1;
                this->RAMoveHandboxFwd();
            } else if (localBTCommand->compare("0010") == 0) {
                this->mountMotion.btMoveSouth = 1;
                this->declinationMoveHandboxDown();
            } else if (localBTCommand->compare("0001") == 0) {
                this->mountMotion.btMoveEast = 1;
                this->RAMoveHandboxBwd();
            }
        }
        if (localBTCommand->compare("0000") == 0) {
            if (this->mountMotion.btMoveNorth == 1) {
                this->mountMotion.btMoveNorth = 0;
                this->declinationMoveHandboxUp();
            }
            if (this->mountMotion.btMoveEast == 1) {
                this->mountMotion.btMoveEast = 0;
                this->RAMoveHandboxBwd();
            }
            if (this->mountMotion.btMoveSouth == 1) {
                this->mountMotion.btMoveSouth = 0;
                this->declinationMoveHandboxDown();
            }
            if (this->mountMotion.btMoveWest == 1) {
                this->mountMotion.btMoveWest = 0;
                this->RAMoveHandboxFwd();
            }
            ui->fMainHBCtrl->setEnabled(true); // disable handcontrol widget
            ui->fHBSpeeds->setEnabled(true);
            ui->pbMeridianFlip->setEnabled(true);
        } // stop the respective motions
    }
    delete localBTCommand; // delete the local deep copy of the command string
    delete wait;
}

//-----------------------------------------------------------------
//-----------------------------------------------------------------
//-----------------------------------------------------------------
// series of methods related to the DSLR ...
//-----------------------------------------------------------------
//-----------------------------------------------------------------
//-----------------------------------------------------------------
// slot that handles start of DSLR exposure ...
void MainWindow::handleDSLRSingleExposure(void) {
    int duration;

    duration = ui->sbDSLRDuration->value();
    dslrStates.dslrExpTime=duration;
    dslrStates.dslrExposureIsRunning=true;
    dslrStates.dslrExpElapsed.start();
    ui->pbDSLRSingleShot->setEnabled(false);
    ui->sbDSLRDuration->setEnabled(false);
    ui->pbDSLRStartSeries->setEnabled(false);
    if (this->dslrStates.dslrSeriesRunning == false) {
        ui->pbDSLRTerminateExposure->setEnabled(true);
    }
    digitalWrite(0,1); // set wiring pi pin 0 to high ...
}

//------------------------------------------------------------------
// slot that handles updates of the GUI during DSLR exposure - i.e.
// display of the remaining exposure time. it emits a signal once
// the exposure is done. this may trigger another exposure if a series
// is carried out ...
void MainWindow::updateDSLRGUIAndCountdown(void) {
    int remTime;

    remTime = (this->dslrStates.dslrExpTime - round(this->dslrStates.dslrExpElapsed.elapsed()/1000.0));
    ui->lcdDSLRTimeRemaining->display(QString::number(remTime));
    if (this->dslrStates.dslrExpElapsed.elapsed() > this->ui->sbDSLRDuration->value()*1000) {
        digitalWrite(0,0); // set wiring pi pin 0 to low ...
        ui->pbDSLRSingleShot->setEnabled(true);
        if (this->dslrStates.dslrSeriesRunning == false) {
            ui->pbDSLRTerminateExposure->setEnabled(false);
        }
        ui->sbDSLRDuration->setEnabled(true);
        this->dslrStates.dslrExposureIsRunning = false;
        ui->pbDSLRStartSeries->setEnabled(true);
        if (this->dslrStates.dslrSeriesRunning == true) {
            emit dslrExposureDone();
        }
    }
}

//------------------------------------------------------------------
// slot that starts a series of exposures ...
void MainWindow::startDSLRSeries(void) {

    if (ui->sbDSLRRepeat->value() > 0) {
        ui->cbExpSeriesDone->setChecked(false);
        ui->sbDSLRRepeat->setEnabled(false);
        ui->pbDSLRStartSeries->setEnabled(false);
        ui->pbDSLRStopSeries->setEnabled(true);
        ui->pbDSLRTerminateExposure->setEnabled(false);
        this->dslrStates.dslrSeriesRunning = true;
        this->dslrStates.noOfExposures = ui->sbDSLRRepeat->value();
        this->dslrStates.noOfExposuresLeft=this->dslrStates.noOfExposures;
        ui->sbDSLRRepeat->setEnabled(false);
        ui->lcdDSLRExpsTaken->display("0");
        this->handleDSLRSingleExposure();
    }
}

//-------------------------------------------------------------------
// slot that is called once an exposure was taken
void MainWindow::takeNextExposureInSeries(void) {
    QElapsedTimer *wait;
    int expsTaken;

    ui->pbDSLRSingleShot->setEnabled(false);
    ui->sbDSLRDuration->setEnabled(false);
    ui->pbDSLRStartSeries->setEnabled(false);
    this->dslrStates.noOfExposuresLeft--;
    expsTaken=this->dslrStates.noOfExposures-this->dslrStates.noOfExposuresLeft;
    ui->lcdDSLRExpsTaken->display(QString::number(expsTaken));
    if (this->dslrStates.noOfExposuresLeft > 0) {
    wait = new QElapsedTimer();
    wait->start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    } while (wait->elapsed() < 5000); // just wait for 5 seconds until next exposure is taken
    delete wait;
        this->handleDSLRSingleExposure();
    } else {
        this->stopDSLRExposureSeries();
    }
}

//-------------------------------------------------------------------
// this slot is called when all exposures of a series are taken
void MainWindow::stopDSLRExposureSeries(void) {
    ui->pbDSLRSingleShot->setEnabled(true);
    ui->sbDSLRDuration->setEnabled(true);
    ui->cbExpSeriesDone->setChecked(true);
    ui->sbDSLRRepeat->setEnabled(true);
    ui->pbDSLRStartSeries->setEnabled(true);
    ui->pbDSLRStopSeries->setEnabled(false);
    ui->sbDSLRRepeat->setValue(0);
    this->dslrStates.dslrSeriesRunning = false;
}

//--------------------------------------------------------------------
// this slot terminates a series of exposures prematurely
void MainWindow::terminateDSLRSeries(void) {
    this->stopDSLRExposureSeries();
    this->dslrStates.noOfExposuresLeft = 0;
    this->terminateDSLRSingleShot();
    ui->lcdDSLRExpsTaken->display("0");
}

//--------------------------------------------------------------------
// this slot stops a single exposure
void MainWindow::terminateDSLRSingleShot(void) {
    this->dslrStates.dslrExposureIsRunning=false;
    this->dslrStates.dslrExpElapsed.invalidate();
    ui->pbDSLRSingleShot->setEnabled(true);
    ui->pbDSLRTerminateExposure->setEnabled(false);
    ui->sbDSLRDuration->setEnabled(true);
    ui->pbDSLRStartSeries->setEnabled(true);
    ui->lcdDSLRTimeRemaining->display("0");
    digitalWrite(0,0); // set wiring pi pin 0 to low ...
}
