#include "lx200_communication.h"
#include "tsc_globaldata.h"
#include <QDebug>


extern TSC_GlobalData *g_AllData;

//--------------------------------------------------------

lx200_communication::lx200_communication(void) {
    this->portIsUp=false;
    rs232port.setPortName("/dev/ttyS0");
    rs232port.setBaudRate(QSerialPort::Baud9600);
    rs232port.setDataBits(QSerialPort::Data8);
    rs232port.setParity(QSerialPort::NoParity);
    rs232port.setStopBits(QSerialPort::OneStop);
    rs232port.setFlowControl(QSerialPort::SoftwareControl);
    replyStrLX = new QString();
    this->serialData = new QByteArray();
    receivedRAFromLX = 0.0;
    receivedDeclFromLX = 0.0;
    LX200Commands.getDecl = QString(":GD");
    LX200Commands.getRA = QString(":GR");
    LX200Commands.getHiDef = QString(":U");
    LX200Commands.stopMotion = QString(":Q");
    LX200Commands.slewRA = QString(":Sr");
    LX200Commands.slewDecl = QString(":Sd");
    LX200Commands.slewPossible = QString(":MS");
    LX200Commands.syncCommand = QString(":CM");
    LX200Commands.moveEast = QString(":Me");
    LX200Commands.moveWest = QString(":Mw");
    LX200Commands.moveNorth = QString(":Mn");
    LX200Commands.moveSouth = QString(":Ms");
    LX200Commands.stopMoveEast = QString(":Qe");
    LX200Commands.stopMoveWest = QString(":Qw");
    LX200Commands.stopMoveNorth = QString(":Qn");
    LX200Commands.stopMoveSouth = QString(":Qs");
    LX200Commands.setCenterSpeed = QString(":RC");
    LX200Commands.setGuideSpeed = QString(":RG");
    LX200Commands.setFindSpeed = QString(":RM");
    LX200Commands.setGOTOSpeed = QString(":RS");
}

//--------------------------------------------------------

lx200_communication::~lx200_communication(void) {
    qDebug() << "Entered RS232 Destructor";
    if (portIsUp == 1) {
        rs232port.setBreakEnabled(true);
        rs232port.clear(QSerialPort::AllDirections);
        rs232port.close();
        portIsUp = 0;
    }
    delete replyStrLX;
    delete serialData;
    qDebug() << "Left RS232 Destructor";
}

//--------------------------------------------------------

void lx200_communication::shutDownPort(void) {
    qDebug() << "Break enabled:" << rs232port.setBreakEnabled(true);
    portIsUp = 0;
    rs232port.clear(QSerialPort::AllDirections);
    rs232port.close();
    replyStrLX->clear();
}

//--------------------------------------------------------

void lx200_communication::openPort(void) {
    portIsUp = 1;
    qDebug() << "Trying to open serial port";
    if (!rs232port.open(QIODevice::ReadWrite)) {
        qDebug() << "Open port failed";
        portIsUp = 0;
    } else {
        qDebug() << "Open port succeeded";
        rs232port.setBreakEnabled(false);
        portIsUp = 1;
        rs232port.clear(QSerialPort::AllDirections);
    }
    replyStrLX->clear();
}

//--------------------------------------------------------

bool lx200_communication::getPortState(void) {
    return portIsUp;
}

//--------------------------------------------------------

qint64 lx200_communication::getDataFromSerialPort(void) {
    qint64 charsToBeRead, charsRead=0;
    QString *incomingCommand;
    QChar lastChar;

    charsToBeRead=rs232port.bytesAvailable();
    incomingCommand = new QString();
    if (charsToBeRead == 1) { // a single <ACK> is sent at establishing connection
        this->serialData->append(rs232port.readAll());
        charsRead=serialData->length();
        if (charsRead != -1) {
            incomingCommand->append(serialData->data());
            this->serialData->clear();
        }
        this->handleBasicLX200Protocol(*incomingCommand);
        delete incomingCommand;
        qDebug() << "Exited early";
        return charsRead;
    }

    if (charsToBeRead > 3) { // a LX200 command is #:_# where _ is a command,
        this->serialData->append(rs232port.readAll());
        charsRead=serialData->length();
        if (charsRead != -1) {
            incomingCommand->append(serialData->data());
            this->serialData->clear();
            lastChar = ((incomingCommand->data())[charsRead-1]);
            if (lastChar != '#') {
                rs232port.waitForReadyRead(1000);
                this->serialData->append(rs232port.readAll());
                charsRead=serialData->length();
                if (charsRead > 0) {
                    incomingCommand->append(serialData->data());
                }
                this->serialData->clear();
            }
      //      qDebug() << "Received" << incomingCommand->toLatin1();
            this->handleBasicLX200Protocol(*incomingCommand);
        }
    }
    delete incomingCommand;
    return charsRead;
}

//--------------------------------------------------------

bool lx200_communication::handleBasicLX200Protocol(QString cmd) {
    QString *assembledString, *lx200cmd, *numSubStr;
    qint64 bytesWritten;
    bool commandToBeSent = 0;
    QStringList commandList,numericalList;
    int numberOfCommands, cmdCounter;
    double rah,ram,ras,decldeg,declmin,declsec;

    lx200cmd = new QString();
    assembledString = new QString();
    if ((cmd.length() == 1) && ((int)(cmd.toLatin1()[0])==6)){
    // if LX200 sends <ACK> -> reply with P for forks, G for german equatorials or A for Alt/Az
        bytesWritten = rs232port.write("P");
        qDebug() << "Sent 'P' as a reply for <ACK> ...";
        return true; // exit here
    }
    commandList = cmd.split('#',QString::SkipEmptyParts,Qt::CaseSensitive);
    numberOfCommands=commandList.count();

    for (cmdCounter = 0; cmdCounter < numberOfCommands; cmdCounter++) {
        lx200cmd->clear();
        assembledString->clear();
        lx200cmd->append(commandList[cmdCounter]);

        if (lx200cmd->startsWith(this->LX200Commands.slewRA,Qt::CaseSensitive)==1) {
            commandToBeSent = 1;
            assembledString->append(QString::number(1));
            lx200cmd->remove(' ');
            numSubStr = new QString(lx200cmd->right(8));
            numericalList=numSubStr->split(':',QString::SkipEmptyParts,Qt::CaseSensitive);
            rah=numericalList[0].toDouble();
            ram=numericalList[1].toDouble();
            ras=numericalList[2].toDouble();
            delete numSubStr;
            this->receivedRAFromLX =(rah+ram/60.0+ras/3600.0)*15.0;
            // got RA coordinates from LX200 ...
        }
        if (lx200cmd->startsWith(this->LX200Commands.slewDecl ,Qt::CaseSensitive)==1) {
            commandToBeSent = 1;
            assembledString->append(QString::number(1));
            lx200cmd->remove(' ');
            numSubStr = new QString(lx200cmd->right(9));
            decldeg=(numSubStr->left(3)).toDouble();
            numSubStr->clear();
            numSubStr->append(lx200cmd->right(5));
            declmin=(numSubStr->left(2)).toDouble();
            declsec=(numSubStr->right(2)).toDouble();
            delete numSubStr;
            this->receivedDeclFromLX =(decldeg+declmin/60.0+declsec/3600.0);
            // got Decl coordinates from LX200 ...
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.slewPossible, Qt::CaseSensitive)==0) {
            commandToBeSent = 1;
            assembledString->append(QString::number(1));
            // asks whether slew is possible
            qDebug() << "Got a request for slewing to" << this->receivedRAFromLX << "and" << this->receivedDeclFromLX;
            emit RS232slew();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.syncCommand, Qt::CaseSensitive)==0) {
            commandToBeSent = 1;
            assembledString->append("M31 EX GAL MAG 35 SZ178.0'#");
            // now set the global coordinates in g_AllData to receivedRA and received Decl
            qDebug() << "now syncing to" << this->receivedRAFromLX << "and" << this->receivedDeclFromLX;
            emit RS232sync();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.getDecl, Qt::CaseSensitive)==0) {
            // returns actual scope declination as "sDD*MM’SS#"
            commandToBeSent = 1;
            this->assembleDeclinationString();
            assembledString->append(replyStrLX->toLatin1());
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.getRA, Qt::CaseSensitive)==0) {
            // returns actual scope RA as "HH:MM:SS#"
            commandToBeSent = 1;
            this->assembleRAString();
            assembledString->append(replyStrLX->toLatin1());
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.stopMotion, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232stopMotion();
                // tell TSC to stop all Motion here; as this one does not require a reply,
                // commandToBeSent is set to 0 ...
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.moveEast, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232moveEast();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.moveWest, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232moveWest();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.moveNorth, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232moveNorth();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.moveSouth, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232moveSouth();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.stopMoveEast, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232stopMoveEast();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.stopMoveWest, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232stopMoveWest();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.stopMoveNorth, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232stopMoveNorth();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.stopMoveSouth, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232stopMoveSouth();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.setCenterSpeed, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232centerSpeed();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.setGuideSpeed, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232guideSpeed();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.setFindSpeed, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232findSpeed();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.setGOTOSpeed, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            emit this->RS232gotoSpeed();
        }
        if (QString::compare(lx200cmd->toLatin1(),this->LX200Commands.getHiDef, Qt::CaseSensitive)==0) {
            commandToBeSent = 0;
            // ignore this as we are always sending in high resolution
        }
        if (commandToBeSent == true) {
//            qDebug() << "Sending: " << assembledString->toLatin1();
            bytesWritten = rs232port.write((assembledString->toLatin1()));
            rs232port.flush();
        }
    }
    delete lx200cmd;
    delete assembledString;
    return true; // do something here later ...
}

//-----------------------------------------------

void lx200_communication::assembleDeclinationString(void) {
    QString *helper;
    double currDecl, remainder;
    int declDeg, declMin, declSec,sign=1;

    replyStrLX->clear();
    currDecl = g_AllData->getActualScopePosition(1);
    if (currDecl < 0) {
        sign = -1;
    } else {
        sign = 1;
    }
    declDeg=(int)(sign*floor(fabs(currDecl)));
    remainder = fabs(currDecl-((double)declDeg));
    declMin=(int)(floor(remainder*60.0));
    remainder = remainder*60.0-declMin;
    declSec=round(remainder);
    helper = new QString();
    if (sign == 1) {
        replyStrLX->insert(0,'+');
    } else {
        replyStrLX->insert(0,'-');
    }

    if ((abs(declDeg)) < 10) {
        replyStrLX->insert(1,'0');
    }
    helper->setNum(abs(declDeg));
    replyStrLX->append(helper);
    helper->clear();
    replyStrLX->append("*");
    if (declMin < 10) {
        replyStrLX->append("0");
    }
    helper->setNum(declMin);
    replyStrLX->append(helper);
    helper->clear();
    replyStrLX->append(":");
    if (declSec < 10) {
        replyStrLX->append("0");
    }
    helper->setNum(declSec);
    replyStrLX->append(helper);
    replyStrLX->append("#");
    delete helper;
}

//---------------------------------------------------

void lx200_communication::assembleRAString(void) {
    QString *helper;
    double currRA, remainder, RAInHours;
    int RAHrs, RAMin, RASec;

    replyStrLX->clear();
    currRA = g_AllData->getActualScopePosition(2);
    RAInHours = currRA/360.0*24.0;
    RAHrs = floor(RAInHours);
    RAMin = floor((RAInHours - RAHrs)*60.0);
    remainder = ((RAInHours - RAHrs)*60.0) - RAMin;
    RASec = round(remainder*60.0);
    helper = new QString();
    if (RAHrs < 10) {
        replyStrLX->append("0");
    }
    helper->setNum(RAHrs);
    replyStrLX->append(helper);
    helper->clear();
    replyStrLX->append(":");
    if (RAMin < 10) {
        replyStrLX->append("0");
    }
    helper->setNum(RAMin);
    replyStrLX->append(helper);
    helper->clear();
    replyStrLX->append(":");
    if (RASec < 10) {
        replyStrLX->append("0");
    }
    helper->setNum(RASec);
    replyStrLX->append(helper);
    helper->clear();
    replyStrLX->append("#");
}

//---------------------------------------------------

double lx200_communication::getReceivedCoordinates(short what) {
    double retval;

    if (what == 0) {
        retval = this->receivedRAFromLX;
    } else {
        retval = this->receivedDeclFromLX;
    }
    return retval;
}