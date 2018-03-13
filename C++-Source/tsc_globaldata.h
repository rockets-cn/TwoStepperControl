#ifndef TSC_GLOBALDATA_H
#define TSC_GLOBALDATA_H

#include <QElapsedTimer>
#include <QImage>
#include <fstream>
#include <string>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sstream>

class TSC_GlobalData {
public:
    TSC_GlobalData(void);
    ~TSC_GlobalData(void);
    void storeGlobalData(void);
    bool loadGlobalData(void); // trying to load a "TSC_Preferences.tsc" datafile in the home directory
    void setHandBoxSpeeds(int, int); // store Goto and Motion speed for handbox
    int getHandBoxSpeeds(short); // 0 for GoToSpeed, 1 for MotionSpeed
    void setINDIState(bool); // true if INDI-Server is connected
    bool getINDIState(void);
    void setInitialStarPosition(float, float); // store the coordinates of the last click - the routine converts it also to ccd-coordinates
    float getInitialStarPosition(short); // 0 is screen x, 1 is screen y, 2 is ccd x and 3 is ccd y
    void setCameraDisplaySize(int, int); // set the size of the widget that displays the camera image
    int getCameraDisplaySize(short);
    float getCameraImageScalingFactor(void); // get the factor that is used to scale the CCD image to the widget size
    void setCameraImageScalingFactor(float);
    void setCameraParameters(float, float, int, int); // set ccd pixelsize x, y and ccd chip width and height in pixels
    float getCameraPixelSize(short); // 0 for width in microns in x, 1 for y
    int getCameraChipPixels(short); // get ccd width and height, 0 for x and 1 for y
    void setSyncPosition(float, float); // when syncing, set RA and decl in decimal format (degrees)
    float getSyncPositionCoords(short); // 0 for decimal RA, 1 for declination- the monotonic timer "monotonicGlobalTimer" starts
    bool wasMountSynced(void); // get the sync-state ...
    qint64 getTimeSinceLastSync(void); // get time in milliseconds since last sync
    void setGearData(float,float,float,float,float,float,float,float,float); // store data on stepper gears and stepsize
    float getGearData(short); //0 for planetary ratio for RA, 1 for other in RA, 2 for # of wormwheels, 3 for stepsize in RA, 4,5,6 and 7 for declination, 8 for microstep-resolution
    void setDriveData(short, int); // 0 for controller ID of RA, 1 for ID of Decl
    int getDriveID(short); // 0 for controller ID of RA, 1 for ID of Decl
    void setDriveParams(short, short, double); // 0 for  RA, 1 for decl, 0 for speed, 1 for Acc, 2 for Current, and the value
    double getDriveParams(short, short); // 0 for RA, 1 for decl and 0 for speed, 1, for Acc and 2 for current
    double getActualScopePosition(short); // 0 for hour angle, 1 for decl, 2 for RA
    void incrementActualScopePosition(double, double); // add hour angle and decl increments
    void storeCameraImage(QImage);
    void setGuideScopeFocalLength(int); // FL of guidescope in mm
    int getGuideScopeFocalLength(void);
    bool getGuidingState(void); // check if system is in autoguiding state
    void setGuidingState(bool);
    bool getTrackingMode(void); // a global variable checking if the mount is tracking or slewing
    void setTrackingMode(bool);
    void setSiteParams(double, double, double); // latitude, longitude and UTC offset of site
    void setSiteParams(QString); // set the name of the site
    double getSiteCoords(short); // get coordinates of site
    QString getSiteName(void); // get name of site
    QImage* getCameraImage(void); // retrieve the topical image from the guiding camera
    QString* getBTMACAddress(void); // get the MAC address of the BT-adapter
    void setLX200IPAddress(QString); // store the IP address for LX200
    QString* getLX200IPAddress(void); // get IP address for LX200
    void setHandboxIPAddress(QString); // store the IP address for the TCP Handbox
    QString* getHandboxIPAddress(void); // get IP address for the TCP Handbox
    void setLocalSTime(double); // set the local sidereal time
    double getLocalSTime(void); // get the local sidereal time
    void setCelestialSpeed(short); // speed is sidereal, lunar or solar
    double getCelestialSpeed(void);
    void setAuxName(short, QString); // store the name of an auxiliary drive; there are 2 of them
    QString getAuxName(short);
    void setStepsToBeDone(short, long); // store the steps carried out by default by one of the aux drives
    long getStepsToBeDone(short);
    void setAuxAcc(long); // acceleration for auxiliary drives
    long getAuxAcc(void);
    void setAuxSpeed(long); // speed for auxiliary drives
    long getAuxSpeed(void);
    void setAuxMSteps(long); // set microstepping ratio for auxiliary drives
    long getAuxMSteps(void);
    void setGuiderFocusDrive(short); // define which auxiliry motor focuses the guider
    short getGuiderFocusDrive(void);
    void setDSLRDiagPixSize(float); // diagonal size of the DSLR chip in microns. needed for dithering
    float getDSLRDiagPixSize(void);
    void setMainScopeFocalLength(int); // store the focal length of the main scope; needed for dithering
    int getMainScopeFocalLength(void);
    void setDitherRange(int, bool); // set dither range; first argument is the number, second is the flag "isMinimum"
    int getDitherRange(bool); // the boolean indicates whether the number requested "isMinimum"
    void setParkingPosition(float, float); // store the actual position as parking position
    float getParkingPosition(short); // get the stored parking position. 0 is for the hour angle, 1 is for declination.

private:
    QElapsedTimer *monotonicGlobalTimer;
    double localSiderealTime;
    bool INDIServerIsConnected;
    bool guidingState;
    bool isInTrackingMode;
    QImage *currentCameraImage;
    int guideScopeFocalLength;
    float dslrPixelDiagSize;
    int mainScopeFocalLength;
    int ditherRangeMin;
    int ditherRangeMax;
    QString *BTMACAddress;
    QString *LX200IPAddress;
    QString *HandboxIPAddress;
    double celestialSpeed;
    int gotoSpeed;
    int motionSpeed;
    float parkingHA;
    float parkingDecl;

    struct initialStarPosStruct {
        float screenx;
        float screeny;
        float ccdx;
        float ccdy;
    };
    struct cameraDisplaySizeStruct {
        int width;
        int height;
        float scalingFactor;
    };
    struct cameraParametersStruct {
        float pixelSizeMicronsX;
        float pixelSizeMicronsY;
        int chipWidth;
        int chipHeight;
    };
    struct syncPositionStruct {
        float rightAscension;
        float declination;
        qint64 timeSinceSyncInMS;
        bool mountWasSynced;
    };
    struct gearDataStruct {
        float planetaryRatioRA;
        float gearRatioRA;
        float wormSizeRA;
        float stepSizeRA;
        float planetaryRatioDecl;
        float gearRatioDecl;
        float wormSizeDecl;
        float stepSizeDecl;
        float microsteps;
    };

    struct driveDataStruct {
        int RAControllerID;
        int DeclControllerID;
        double actualRASpeed;
        double actualDeclSpeed;
        double driveAccRA;
        double driveAccDecl;
        double driveCurrRA;
        double driveCurrDecl;
    };

    struct actualScopePositionStruct {
        double actualHA;
        double actualDecl;
        double actualRA;
    };

    struct siteParamsStruct {
        double longitude;
        double latitude;
        QString siteName;
        double UTCOffset;
    };

    struct auxDriveStruct {
        QString nameAux1;
        QString nameAux2;
        long stepsAux1;
        long stepsAux2;
        long auxAcc;
        long auxSpeed;
        short mSteps;
        short guideScopeFocuserDrive; // 0 is no guider, 1 is auxDrive1, 2 is auxDrive2
    };

    struct initialStarPosStruct initialStarPos;
    struct cameraDisplaySizeStruct cameraDisplaySize;
    struct cameraParametersStruct cameraParameters;
    struct syncPositionStruct syncPosition;
    struct gearDataStruct gearData;
    struct driveDataStruct driveData;
    struct actualScopePositionStruct actualScopePosition;
    struct siteParamsStruct siteParams;
    struct auxDriveStruct auxDriveParams;
};

#endif // TSC_GLOBALDATA_H
