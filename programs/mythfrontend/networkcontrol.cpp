#include <unistd.h>
#include <iostream>
using namespace std;

#include <qapplication.h>
#include <qregexp.h>
#include <qstringlist.h>
#include <qtextstream.h>
#include <qdir.h>
#include <qdeepcopy.h>

#include "libmyth/mythcontext.h"
#include "libmyth/mythdialogs.h"
#include "networkcontrol.h"
#include "programinfo.h"
#include "remoteutil.h"
#include "previewgenerator.h"
#include "compat.h"

#define LOC QString("NetworkControl: ")
#define LOC_ERR QString("NetworkControl Error: ")

const int kNetworkControlDataReadyEvent = 35671;
const int kNetworkControlCloseEvent     = 35672;

/** Is @p test an abbreviation of @p command ?
 * The @p test substring must be at least @p minchars long.
 * @param command the full command name
 * @param test the string to test against the command name
 * @param minchars the minimum length of test in order to declare a match
 * @return true if @p test is the initial substring of @p command
 */
static bool is_abbrev(QString const& command, QString const& test, unsigned minchars = 1)
{
    if (test.length() < minchars)
        return command.lower() == test.lower();
    else
        return test.lower() == command.left(test.length()).lower();
}

NetworkControl::NetworkControl(int port)
          : QServerSocket(port, 1),
            prompt("# "),
            gotAnswer(false), answer(""),
            client(NULL), cs(NULL)
{
    VERBOSE(VB_IMPORTANT, LOC +
            QString("Listening for remote connections on port %1").arg(port));

    // Eventually this map should be in the jumppoints table
    jumpMap["channelpriorities"]     = "Channel Recording Priorities";
    jumpMap["livetv"]                = "Live TV";
    jumpMap["livetvinguide"]         = "Live TV In Guide";
    jumpMap["mainmenu"]              = "Main Menu";
    jumpMap["managerecordings"]      = "Manage Recordings / Fix Conflicts";
    jumpMap["manualrecording"]       = "Manual Record Scheduling";
    jumpMap["mythgallery"]           = "MythGallery";
    jumpMap["mythmovietime"]         = "MythMovieTime";
    jumpMap["mythvideo"]             = "MythVideo";
    jumpMap["mythweather"]           = "MythWeather";
    jumpMap["mythgame"]              = "MythGame";
    jumpMap["mythnews"]              = "MythNews";
    jumpMap["playdvd"]               = "Play DVD";
    jumpMap["playmusic"]             = "Play music";
    jumpMap["programfinder"]         = "Program Finder";
    jumpMap["programguide"]          = "Program Guide";
    jumpMap["recordingpriorities"]   = "Program Recording Priorities";
    jumpMap["ripcd"]                 = "Rip CD";
    jumpMap["ripdvd"]                = "Rip DVD";
    jumpMap["musicplaylists"]        = "Select music playlists";
    jumpMap["deleterecordings"]      = "TV Recording Deletion";
    jumpMap["playbackrecordings"]    = "TV Recording Playback";
    jumpMap["videobrowser"]          = "Video Browser";
    jumpMap["videogallery"]          = "Video Gallery";
    jumpMap["videolistings"]         = "Video Listings";
    jumpMap["videomanager"]          = "Video Manager";
    jumpMap["flixbrowse"]            = "Netflix Browser";
    jumpMap["flixqueue"]             = "Netflix Queue";
    jumpMap["flixhistory"]           = "Netflix History";
    jumpMap["zoneminderconsole"]     = "ZoneMinder Console";
    jumpMap["zoneminderliveview"]    = "ZoneMinder Live View";
    jumpMap["zoneminderevents"]      = "ZoneMinder Events";

    // These jump point names match the (lowercased) locations from gContext
    jumpMap["channelrecpriority"]    = "Channel Recording Priorities";
    jumpMap["viewscheduled"]         = "Manage Recordings / Fix Conflicts";
    jumpMap["manualbox"]             = "Manual Record Scheduling";
    jumpMap["previousbox"]           = "Previously Recorded";
    jumpMap["progfinder"]            = "Program Finder";
    jumpMap["guidegrid"]             = "Program Guide";
    jumpMap["programrecpriority"]    = "Program Recording Priorities";
    jumpMap["statusbox"]             = "Status Screen";
    jumpMap["deletebox"]             = "TV Recording Deletion";
    jumpMap["playbackbox"]           = "TV Recording Playback";

    keyMap["up"]                     = Qt::Key_Up;
    keyMap["down"]                   = Qt::Key_Down;
    keyMap["left"]                   = Qt::Key_Left;
    keyMap["right"]                  = Qt::Key_Right;
    keyMap["home"]                   = Qt::Key_Home;
    keyMap["end"]                    = Qt::Key_End;
    keyMap["enter"]                  = Qt::Key_Enter;
    keyMap["return"]                 = Qt::Key_Return;
    keyMap["pageup"]                 = Qt::Key_Prior;
    keyMap["pagedown"]               = Qt::Key_Next;
    keyMap["escape"]                 = Qt::Key_Escape;
    keyMap["tab"]                    = Qt::Key_Tab;
    keyMap["backtab"]                = Qt::Key_Backtab;
    keyMap["space"]                  = Qt::Key_Space;
    keyMap["backspace"]              = Qt::Key_Backspace;
    keyMap["insert"]                 = Qt::Key_Insert;
    keyMap["delete"]                 = Qt::Key_Delete;
    keyMap["plus"]                   = Qt::Key_Plus;
    keyMap["+"]                      = Qt::Key_Plus;
    keyMap["comma"]                  = Qt::Key_Comma;
    keyMap[","]                      = Qt::Key_Comma;
    keyMap["minus"]                  = Qt::Key_Minus;
    keyMap["-"]                      = Qt::Key_Minus;
    keyMap["underscore"]             = Qt::Key_Underscore;
    keyMap["_"]                      = Qt::Key_Underscore;
    keyMap["period"]                 = Qt::Key_Period;
    keyMap["."]                      = Qt::Key_Period;
    keyMap["numbersign"]             = Qt::Key_NumberSign;
    keyMap["poundsign"]              = Qt::Key_NumberSign;
    keyMap["hash"]                   = Qt::Key_NumberSign;
    keyMap["#"]                      = Qt::Key_NumberSign;
    keyMap["bracketleft"]            = Qt::Key_BracketLeft;
    keyMap["["]                      = Qt::Key_BracketLeft;
    keyMap["bracketright"]           = Qt::Key_BracketRight;
    keyMap["]"]                      = Qt::Key_BracketRight;
    keyMap["backslash"]              = Qt::Key_Backslash;
    keyMap["\\"]                     = Qt::Key_Backslash;
    keyMap["dollar"]                 = Qt::Key_Dollar;
    keyMap["$"]                      = Qt::Key_Dollar;
    keyMap["percent"]                = Qt::Key_Percent;
    keyMap["%"]                      = Qt::Key_Percent;
    keyMap["ampersand"]              = Qt::Key_Ampersand;
    keyMap["&"]                      = Qt::Key_Ampersand;
    keyMap["parenleft"]              = Qt::Key_ParenLeft;
    keyMap["("]                      = Qt::Key_ParenLeft;
    keyMap["parenright"]             = Qt::Key_ParenRight;
    keyMap[")"]                      = Qt::Key_ParenRight;
    keyMap["asterisk"]               = Qt::Key_Asterisk;
    keyMap["*"]                      = Qt::Key_Asterisk;
    keyMap["question"]               = Qt::Key_Question;
    keyMap["?"]                      = Qt::Key_Question;
    keyMap["slash"]                  = Qt::Key_Slash;
    keyMap["/"]                      = Qt::Key_Slash;
    keyMap["colon"]                  = Qt::Key_Colon;
    keyMap[":"]                      = Qt::Key_Colon;
    keyMap["semicolon"]              = Qt::Key_Semicolon;
    keyMap[";"]                      = Qt::Key_Semicolon;
    keyMap["less"]                   = Qt::Key_Less;
    keyMap["<"]                      = Qt::Key_Less;
    keyMap["equal"]                  = Qt::Key_Equal;
    keyMap["="]                      = Qt::Key_Equal;
    keyMap["greater"]                = Qt::Key_Greater;
    keyMap[">"]                      = Qt::Key_Greater;
    keyMap["bar"]                    = Qt::Key_Bar;
    keyMap["pipe"]                   = Qt::Key_Bar;
    keyMap["|"]                      = Qt::Key_Bar;
    keyMap["f1"]                     = Qt::Key_F1;
    keyMap["f2"]                     = Qt::Key_F2;
    keyMap["f3"]                     = Qt::Key_F3;
    keyMap["f4"]                     = Qt::Key_F4;
    keyMap["f5"]                     = Qt::Key_F5;
    keyMap["f6"]                     = Qt::Key_F6;
    keyMap["f7"]                     = Qt::Key_F7;
    keyMap["f8"]                     = Qt::Key_F8;
    keyMap["f9"]                     = Qt::Key_F9;
    keyMap["f10"]                    = Qt::Key_F10;
    keyMap["f11"]                    = Qt::Key_F11;
    keyMap["f12"]                    = Qt::Key_F12;
    keyMap["f13"]                    = Qt::Key_F13;
    keyMap["f14"]                    = Qt::Key_F14;
    keyMap["f15"]                    = Qt::Key_F15;
    keyMap["f16"]                    = Qt::Key_F16;
    keyMap["f17"]                    = Qt::Key_F17;
    keyMap["f18"]                    = Qt::Key_F18;
    keyMap["f19"]                    = Qt::Key_F19;
    keyMap["f20"]                    = Qt::Key_F20;
    keyMap["f21"]                    = Qt::Key_F21;
    keyMap["f22"]                    = Qt::Key_F22;
    keyMap["f23"]                    = Qt::Key_F23;
    keyMap["f24"]                    = Qt::Key_F24;

    stopCommandThread = false;
    pthread_create(&command_thread, NULL, CommandThread, this);

    gContext->addListener(this);
}

NetworkControl::~NetworkControl(void)
{
    nrLock.lock();
    networkControlReplies.push_back(
        "mythfrontend shutting down, connection closing...");
    nrLock.unlock();

    notifyDataAvailable();

    stopCommandThread = true;
    ncLock.lock();
    ncCond.wakeOne();
    ncLock.unlock();
    pthread_join(command_thread, NULL);
}

void *NetworkControl::CommandThread(void *param)
{
    NetworkControl *networkControl = (NetworkControl *)param;
    networkControl->RunCommandThread();

    return NULL;
}

void NetworkControl::RunCommandThread(void)
{
    QString command;

    while (!stopCommandThread)
    {
        ncLock.lock();
        while (!networkControlCommands.size()) {
            ncCond.wait(&ncLock);
            if (stopCommandThread)
            {
                ncLock.unlock();
                return;
            }
        }
        command = networkControlCommands.front(); 
        networkControlCommands.pop_front();
        ncLock.unlock();

        processNetworkControlCommand(command);
    }
}

void NetworkControl::processNetworkControlCommand(QString command)
{
    QMutexLocker locker(&clientLock);
    QString result = "";
    QStringList tokens = QStringList::split(" ", command);

    if (is_abbrev("jump", tokens[0]))
        result = processJump(tokens);
    else if (is_abbrev("key", tokens[0]))
        result = processKey(tokens);
    else if (is_abbrev("play", tokens[0]))
        result = processPlay(tokens);
    else if (is_abbrev("query", tokens[0]))
        result = processQuery(tokens);
    else if (is_abbrev("help", tokens[0]))
        result = processHelp(tokens);
    else if ((tokens[0].lower() == "exit") || (tokens[0].lower() == "quit"))
        QApplication::postEvent(this,
                                new QCustomEvent(kNetworkControlCloseEvent));
    else if (! tokens[0].isEmpty())
        result = QString("INVALID command '%1', try 'help' for more info")
                         .arg(tokens[0]);

    if (result != "")
    {
        nrLock.lock();
        networkControlReplies.push_back(QDeepCopy<QString>(result));
        nrLock.unlock();

        notifyDataAvailable();
    }
}

void NetworkControl::newConnection(int socket)
{
    QString welcomeStr = "";
    bool closedOldConn = false;
    QSocket *s = new QSocket(this);
    connect(s, SIGNAL(readyRead()), this, SLOT(readClient()));
    connect(s, SIGNAL(delayedCloseFinished()), this, SLOT(discardClient()));
    connect(s, SIGNAL(connectionClosed()), this, SLOT(discardClient()));
    s->setSocket(socket);

    VERBOSE(VB_IMPORTANT, LOC +
            QString("New connection established. (%1)").arg(socket));

    QTextStream *os = new QTextStream(s);
    os->setEncoding(QTextStream::UnicodeUTF8);

    QMutexLocker locker(&clientLock);
    if (client)
    {
        closedOldConn = true;
        client->close();
        delete client;
        delete cs;
    }

    client = s;
    cs = os;

    ncLock.lock();
    networkControlCommands.clear();
    ncLock.unlock();

    nrLock.lock();
    networkControlReplies.clear();
    nrLock.unlock();

    welcomeStr = "MythFrontend Network Control\r\n";
    if (closedOldConn)
    {
        welcomeStr +=
            "WARNING: mythfrontend was already under network control.\r\n";
        welcomeStr +=
            "         Previous session is being disconnected.\r\n";
    }

    welcomeStr += "Type 'help' for usage information\r\n"
                  "---------------------------------";
    nrLock.lock();
    networkControlReplies.push_back(welcomeStr);
    nrLock.unlock();

    notifyDataAvailable();
}

void NetworkControl::readClient(void)
{
    QSocket *socket = (QSocket *)sender();
    if (!socket)
        return;

    QString lineIn;
    QStringList tokens;
    while (socket->canReadLine())
    {
        lineIn = socket->readLine();
        lineIn.replace(QRegExp("[^-a-zA-Z0-9\\s\\.:_#/$%&()*+,;<=>?\\[\\]\\|]"), "");
        lineIn.replace(QRegExp("[\r\n]"), "");
        lineIn.replace(QRegExp("^\\s"), "");

        if (lineIn.isEmpty())
            continue;

        tokens = QStringList::split(" ", lineIn);

        ncLock.lock();
        networkControlCommands.push_back(QDeepCopy<QString>(lineIn));
        ncCond.wakeOne();
        ncLock.unlock();
    }
}

void NetworkControl::discardClient(void)
{
    QSocket *socket = (QSocket *)sender();
    if (!socket)
        return;

    QMutexLocker locker(&clientLock);
    if (client == socket)
    {
        delete cs;
        delete client;
        client = NULL;
        cs = NULL;
    }
    else
        delete socket;
}

QString NetworkControl::processJump(QStringList tokens)
{
    QString result = "OK";

    if ((tokens.size() < 2) || (!jumpMap.contains(tokens[1])))
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    gContext->GetMainWindow()->JumpTo(jumpMap[tokens[1]]);

    // Fixme, should do some better checking here, but that would
    // depend on all Locations matching their jumppoints
    QTime timer;
    timer.start();
    while ((timer.elapsed() < 2000) &&
           (gContext->getCurrentLocation().lower() != tokens[1]))
        usleep(10000);

    return result;
}

QString NetworkControl::processKey(QStringList tokens)
{
    QString result = "OK";
    QKeyEvent *event = NULL;

    if (tokens.size() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    QObject *keyDest = NULL;

    if (!gContext)
        return QString("ERROR: Application has no gContext!\n");

    if (gContext->GetMainWindow())
        keyDest = gContext->GetMainWindow();
    else
        return QString("ERROR: Application has no main window!\n");

    if (gContext->GetMainWindow()->currentWidget())
        keyDest = gContext->GetMainWindow()->currentWidget()->focusWidget();

    unsigned int curToken = 1;
    unsigned int tokenLen = 0;
    while (curToken < tokens.size())
    {
        tokenLen = tokens[curToken].length();

        if (tokens[curToken] == "sleep")
        {
            sleep(1);
        }
        else if (keyMap.contains(tokens[curToken]))
        {
            int keyCode = keyMap[tokens[curToken]];

            event = new QKeyEvent(QEvent::KeyPress, keyCode, 0, NoButton);
            QApplication::postEvent(keyDest, event);

            event = new QKeyEvent(QEvent::KeyRelease, keyCode, 0, NoButton);
            QApplication::postEvent(keyDest, event);
        }
        else if (((tokenLen == 1) &&
                  (tokens[curToken][0].isLetterOrNumber())) ||
                 ((tokenLen >= 1) &&
                  (tokens[curToken].contains("+"))))
        {
            QKeySequence a(tokens[curToken]);
            int keyCode = a[0];
            int ch = (tokens[curToken].ascii())[tokenLen - 1];
            int buttons = NoButton;

            if (tokenLen > 1)
            {
                QStringList tokenParts =
                    QStringList::split("+", tokens[curToken]);

                unsigned int partNum = 0;
                while (partNum < (tokenParts.size() - 1))
                {
                    if (tokenParts[partNum].upper() == "CTRL")
                        buttons |= ControlButton;
                    if (tokenParts[partNum].upper() == "SHIFT")
                        buttons |= ShiftButton;
                    if (tokenParts[partNum].upper() == "ALT")
                        buttons |= AltButton;
                    if (tokenParts[partNum].upper() == "META")
                        buttons |= MetaButton;

                    partNum++;
                }
            }
            else
            {
                if (tokens[curToken] == tokens[curToken].upper())
                    buttons = ShiftButton;
            }

            event = new QKeyEvent(QEvent::KeyPress, keyCode, ch, buttons,
                                  tokens[curToken]);
            QApplication::postEvent(keyDest, event);

            event = new QKeyEvent(QEvent::KeyRelease, keyCode, ch, buttons,
                                  tokens[curToken]);
            QApplication::postEvent(keyDest, event);
        }
        else
            return QString("ERROR: Invalid syntax at '%1', see 'help %2' for "
                           "usage information")
                           .arg(tokens[curToken]).arg(tokens[0]);

        curToken++;
    }

    return result;
}

QString NetworkControl::processPlay(QStringList tokens)
{
    QString result = "OK";
    QString message = "";

    if (tokens.size() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    if ((tokens.size() >= 4) &&
        (is_abbrev("program", tokens[1])) &&
        (tokens[2].contains(QRegExp("^\\d+$"))) &&
        (tokens[3].contains(QRegExp(
                         "^\\d\\d\\d\\d-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d$"))))
    {
        if (gContext->getCurrentLocation() == "Playback")
        {
            QString message = QString("NETWORK_CONTROL STOP");
            MythEvent me(message);
            gContext->dispatch(me);

            QTime timer;
            timer.start();
            while ((timer.elapsed() < 10000) &&
                   (gContext->getCurrentLocation() == "Playback"))
                usleep(10000);
        }

        if (gContext->getCurrentLocation() != "PlaybackBox")
        {
            gContext->GetMainWindow()->JumpTo(jumpMap["playbackbox"]);

            QTime timer;
            timer.start();
            while ((timer.elapsed() < 10000) &&
                   (gContext->getCurrentLocation() != "PlaybackBox"))
                usleep(10000);
        }

        if (gContext->getCurrentLocation() == "PlaybackBox")
        {
            QString action = "PLAY";
            if (tokens.size() == 5 && tokens[4] == "resume")
                action = "RESUME";

            QString message = QString("NETWORK_CONTROL %1 PROGRAM %2 %3")
                                      .arg(action).arg(tokens[2])
                                      .arg(tokens[3].upper());
            MythEvent me(message);
            gContext->dispatch(me);

            result = "";
        }
        else
        {
            result = QString("ERROR: Unable to change to PlaybackBox from "
                             "%1, can not play requested file.")
                             .arg(gContext->getCurrentLocation());
        }
    }
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    else if (gContext->getCurrentLocation().lower() != "playback")
    {
        return QString("ERROR: You are in %1 mode and this command is only "
                       "for playback mode")
                       .arg(gContext->getCurrentLocation());
    }
    else if (is_abbrev("chanid", tokens[1], 5))
    {
        if (tokens[2].contains(QRegExp("^\\d+$")))
            message = QString("NETWORK_CONTROL CHANID %1").arg(tokens[2]);
        else
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);
    }
    else if (is_abbrev("channel", tokens[1], 5))
    {
        if (tokens.size() < 3)
            return "ERROR: See 'help play' for usage information";

        if (is_abbrev("up", tokens[2]))
            message = "NETWORK_CONTROL CHANNEL UP";
        else if (is_abbrev("down", tokens[2]))
            message = "NETWORK_CONTROL CHANNEL DOWN";
        else if (tokens[2].contains(QRegExp("^[-\\.\\d_#]+$")))
            message = QString("NETWORK_CONTROL CHANNEL %1").arg(tokens[2]);
        else
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);
    }
    else if (is_abbrev("seek", tokens[1], 2))
    {
        if (tokens.size() < 3)
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);

        if (is_abbrev("beginning", tokens[2]))
            message = "NETWORK_CONTROL SEEK BEGINNING";
        else if (is_abbrev("forward", tokens[2]))
            message = "NETWORK_CONTROL SEEK FORWARD";
        else if (is_abbrev("rewind",   tokens[2]) ||
                 is_abbrev("backward", tokens[2]))
            message = "NETWORK_CONTROL SEEK BACKWARD";
        else if (tokens[2].contains(QRegExp("^\\d\\d:\\d\\d:\\d\\d$")))
        {
            int hours   = tokens[2].mid(0, 2).toInt();
            int minutes = tokens[2].mid(3, 2).toInt();
            int seconds = tokens[2].mid(6, 2).toInt();
            message = QString("NETWORK_CONTROL SEEK POSITION %1")
                              .arg((hours * 3600) + (minutes * 60) + seconds);
        }
        else
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);
    }
    else if (is_abbrev("speed", tokens[1], 2))
    {
        if (tokens.size() < 3)
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);

        tokens[2] = tokens[2].lower();
        if ((tokens[2].contains(QRegExp("^\\-*\\d+x$"))) ||
            (tokens[2].contains(QRegExp("^\\-*\\d+\\/\\d+x$"))) ||
            (tokens[2].contains(QRegExp("^\\d*\\.\\d+x$"))))
            message = QString("NETWORK_CONTROL SPEED %1").arg(tokens[2]);
        else if (is_abbrev("normal", tokens[2]))
            message = QString("NETWORK_CONTROL SPEED 1x");
        else if (is_abbrev("pause", tokens[2]))
            message = QString("NETWORK_CONTROL SPEED 0x");
        else
            return QString("ERROR: See 'help %1' for usage information")
                           .arg(tokens[0]);
    }
    else if (is_abbrev("save", tokens[1], 2))
    {
        if (is_abbrev("screenshot", tokens[2], 2))
            return saveScreenshot(tokens);
    }
    else if (is_abbrev("stop", tokens[1], 2))
        message = QString("NETWORK_CONTROL STOP");
    else
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    if (message != "")
    {
        MythEvent me(message);
        gContext->dispatch(me);
    }

    return result;
}

QString NetworkControl::processQuery(QStringList tokens)
{
    QString result = "OK";

    if (tokens.size() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    if (is_abbrev("location", tokens[1]))
    {
        QString location = gContext->getCurrentLocation();
        result = location;

        // if we're playing something, then find out what
        if (location == "Playback")
        {
            result += " ";
            gotAnswer = false;
            QString message = QString("NETWORK_CONTROL QUERY POSITION");
            MythEvent me(message);
            gContext->dispatch(me);

            QTime timer;
            timer.start();
            while (timer.elapsed() < 2000  && !gotAnswer)
                usleep(10000);

            if (gotAnswer)
                result += answer;
            else
                result = "ERROR: Timed out waiting for reply from player";
        }
    }
    else if (is_abbrev("liveTV", tokens[1]))
    {
        if(tokens.size() == 3) // has a channel ID
            return listSchedule(tokens[2]);
        else
            return listSchedule();
    }
    else if(is_abbrev("time", tokens[1]))
        return QDateTime::currentDateTime().toString(Qt::ISODate);
    else if ((tokens.size() == 4) &&
             is_abbrev("recording", tokens[1]) &&
             (tokens[2].contains(QRegExp("^\\d+$"))) &&
             (tokens[3].contains(QRegExp(
                         "^\\d\\d\\d\\d-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d$"))))
        return listRecordings(tokens[2], tokens[3].upper());
    else if (is_abbrev("recordings", tokens[1]))
        return listRecordings();
    else
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(tokens[0]);

    return result;
}

QString NetworkControl::processHelp(QStringList tokens)
{
    QString command = "";
    QString helpText = "";

    if (tokens.size() >= 1)
    {
        if (is_abbrev("help", tokens[0]))
        {
            if (tokens.size() >= 2)
                command = tokens[1];
            else
                command = "";
        }
        else
        {
            command = tokens[0];
        }
    }
        
    if (is_abbrev("jump", command))
    {
        QMap<QString, QString>::Iterator it;
        helpText +=
            "Usage: jump JUMPPOINT\r\n"
            "\r\n"
            "Where JUMPPOINT is one of the following:\r\n";

        for (it = jumpMap.begin(); it != jumpMap.end(); ++it)
        {
            helpText += it.key().leftJustify(20, ' ', true) + " - " +
                        it.data() + "\r\n";
        }
    }
    else if (is_abbrev("key", command))
    {
        helpText +=
            "key LETTER           - Send the letter key specified\r\n"
            "key NUMBER           - Send the number key specified\r\n"
            "key CODE             - Send one of the following key codes\r\n"
            "\r\n";

        QMap<QString, int>::Iterator it;
        bool first = true;
        for (it = keyMap.begin(); it != keyMap.end(); ++it)
        {
            if (first)
                first = false;
            else
                helpText += ", ";

            helpText += it.key();
        }
        helpText += "\r\n";
    }
    else if (is_abbrev("play", command))
    {
        helpText +=
            "play channel up       - Change channel Up\r\n"
            "play channel down     - Change channel Down\r\n"
            "play channel NUMBER   - Change to a specific channel number\r\n"
            "play chanid NUMBER    - Change to a specific channel id (chanid)\r\n"
            "play program CHANID yyyy-mm-ddThh:mm:ss\r\n"
            "                      - Play program with chanid & starttime\r\n"
            "play program CHANID yyyy-mm-ddThh:mm:ss resume\r\n"
            "                      - Resume program with chanid & starttime\r\n"
            "play save screenshot FILENAME\r\n"
            "                      - Save screenshot from current position\r\n"
            "play seek beginning   - Seek to the beginning of the recording\r\n"
            "play seek forward     - Skip forward in the video\r\n"
            "play seek backward    - Skip backwards in the video\r\n"
            "play seek HH:MM:SS    - Seek to a specific position\r\n"
            "play speed pause      - Pause playback\r\n"
            "play speed normal     - Playback at normal speed\r\n"
            "play speed 1x         - Playback at normal speed\r\n"
            "play speed -1x        - Playback at normal speed in reverse\r\n"
            "play speed 1/16x      - Playback at 1/16x speed\r\n"
            "play speed 1/8x       - Playback at 1/8x speed\r\n"
            "play speed 1/4x       - Playback at 1/4x speed\r\n"
            "play speed 1/2x       - Playback at 1/2x speed\r\n"
            "play speed 2x         - Playback at 2x speed\r\n"
            "play speed 4x         - Playback at 4x speed\r\n"
            "play speed 8x         - Playback at 8x speed\r\n"
            "play speed 16x        - Playback at 16x speed\r\n"
            "play stop             - Stop playback\r\n";
    }
    else if (is_abbrev("query", command))
    {
        helpText +=
            "query location        - Query current screen or location\r\n"
            "query recordings      - List currently available recordings\r\n"
            "query recording CHANID STARTTIME\r\n"
            "                      - List info about the specified program\r\n"
            "query liveTV          - List current TV schedule\r\n"
            "query liveTV CHANID   - Query current program for specified channel\r\n"
            "query time            - Query current time on server\r\n";
    }
    else if (command == "exit")
    {
        helpText +=
            "exit                  - Terminates session\r\n\r\n";
    }

    if (helpText != "")
        return helpText;

    if (command != "")
            helpText += QString("Unknown command '%1'\r\n\r\n").arg(command);

    helpText +=
        "Valid Commands:\r\n"
        "---------------\r\n"
        "jump               - Jump to a specified location in Myth\r\n"
        "key                - Send a keypress to the program\r\n"
        "play               - Playback related commands\r\n"
        "query              - Queries\r\n"
        "exit               - Exit Network Control\r\n"
        "\r\n"
        "Type 'help COMMANDNAME' for help on any specific command.\r\n";

    return helpText;
}

void NetworkControl::notifyDataAvailable(void)
{
    QApplication::postEvent(this,
                            new QCustomEvent(kNetworkControlDataReadyEvent));
}

void NetworkControl::customEvent(QCustomEvent *e)
{       
    if ((MythEvent::Type)(e->type()) == MythEvent::MythEventMessage)
    {   
        MythEvent *me = (MythEvent *)e;
        QString message = me->Message();

        if (message.left(15) != "NETWORK_CONTROL")
            return;

        QStringList tokens = QStringList::split(" ", message);
        if ((tokens.size() >= 3) &&
            (tokens[1] == "ANSWER"))
        {
            answer = tokens[2];
            for (unsigned int i = 3; i < tokens.size(); i++)
                answer += QString(" ") + tokens[i];
            gotAnswer = true;
        }
        else if ((tokens.size() >= 3) &&
                 (tokens[1] == "RESPONSE"))
        {
            QString response = tokens[2];
            for (unsigned int i = 3; i < tokens.size(); i++)
                response += QString(" ") + tokens[i];
            nrLock.lock();
            networkControlReplies.push_back(QDeepCopy<QString>(response));
            nrLock.unlock();

            notifyDataAvailable();
        }
    }
    else if (e->type() == kNetworkControlDataReadyEvent)
    {
        QString reply;
        int replies;
        QRegExp crlfRegEx("\r\n$");
        QRegExp crlfcrlfRegEx("\r\n.*\r\n");

        nrLock.lock();
        replies = networkControlReplies.size();
        while (client && cs && replies > 0 &&
               client->state() == QSocket::Connected)
        {
            reply = networkControlReplies.front();
            networkControlReplies.pop_front();
            *cs << reply;
            if (!reply.contains(crlfRegEx) || reply.contains(crlfcrlfRegEx))
                *cs << "\r\n" << prompt;
            client->flush();

            replies = networkControlReplies.size();
        }
        nrLock.unlock();
    }
    else if (e->type() == kNetworkControlCloseEvent)
    {
        if (client && client->state() == QSocket::Connected)
        {
            clientLock.lock();
            client->close();
            delete client;
            delete cs;
            client = NULL;
            cs = NULL;
            clientLock.unlock();
        }
    }
}

QString NetworkControl::listSchedule(const QString& chanID) const
{
    QString result("");
    MSqlQuery query(MSqlQuery::InitCon());
    bool appendCRLF = true;
    QString queryStr("SELECT chanid, starttime, endtime, title, subtitle "
                         "FROM program "
                         "WHERE starttime < :NOW AND endtime > :NOW ");

    if(chanID != "")
    {
        queryStr += " AND chanid = :CHANID";
        appendCRLF = false;
    }

    queryStr += " ORDER BY starttime, endtime, chanid";

    query.prepare(queryStr);
    query.bindValue(":NOW", QDateTime::currentDateTime());
    query.bindValue(":CHANID", chanID);

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            QString title = QString::fromUtf8(query.value(3).toString());
            QString subtitle = QString::fromUtf8(query.value(4).toString());

            if (subtitle > " ")
                title += QString(" -\"%1\"").arg(subtitle);

            result +=
                QString("%1 %2 %3 %4")
                        .arg(QString::number(query.value(0).toInt())
                             .rightJustify(5, ' '))
                        .arg(query.value(1).toDateTime().toString(Qt::ISODate))
                        .arg(query.value(2).toDateTime().toString(Qt::ISODate))
                        .arg(title.local8Bit());

            if (appendCRLF)
                result += "\r\n";
        }
    }
    else
    {
       result = "ERROR: Unable to retrieve current schedule list.";
    }
    return result;
}

QString NetworkControl::listRecordings(QString chanid, QString starttime)
{
    QString result = "";
    QString episode;
    MSqlQuery query(MSqlQuery::InitCon());
    QString queryStr;
    bool appendCRLF = true;

    queryStr = "SELECT chanid, starttime, title, subtitle "
               "FROM recorded WHERE deletepending = 0 ";

    if ((chanid != "") && (starttime != ""))
    {
        queryStr += "AND chanid = " + chanid + " "
                    "AND starttime = '" + starttime + "' ";
        appendCRLF = false;
    }

    queryStr += "ORDER BY starttime, title;";

    query.prepare(queryStr);
    if (query.exec() && query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            QString title = QString::fromUtf8(query.value(2).toString());
            QString subtitle = QString::fromUtf8(query.value(3).toString());

            if (subtitle > " ")
                episode = QString("%1 -\"%2\"")
                                  .arg(title)
                                  .arg(subtitle);
            else
                episode = title;

            result +=
                QString("%1 %2 %3").arg(query.value(0).toInt())
                        .arg(query.value(1).toDateTime().toString(Qt::ISODate))
                        .arg(episode);

            if (appendCRLF)
                result += "\r\n";
        }
    }
    else
        result = "ERROR: Unable to retrieve recordings list.";

    return result;
}

QString NetworkControl::saveScreenshot(QStringList tokens)
{
    QString result = "";
    int width = -1;
    int height = -1;
    long long frameNumber = 150;

    QString location = gContext->getCurrentLocation();

    if (location != "Playback")
    {
        return "ERROR: Not in playback mode, unable to save screenshot";
    }

    gotAnswer = false;
    QString message = QString("NETWORK_CONTROL QUERY POSITION");
    MythEvent me(message);
    gContext->dispatch(me);

    QTime timer;
    timer.start();
    while (timer.elapsed() < 2000  && !gotAnswer)
        usleep(10000);

    ProgramInfo *pginfo = NULL;
    if (gotAnswer)
    {
        QStringList results = QStringList::split(" ", answer);
        pginfo = ProgramInfo::GetProgramFromRecorded(results[5], results[6]);
        if (!pginfo)
            return "ERROR: Unable to find program info for current program";

        QString outFile = QDir::homeDirPath() + "/.mythtv/screenshot.png";

        if (tokens.size() >= 4)
            outFile = tokens[3];

        if (tokens.size() >= 5)
        {
            QStringList size = QStringList::split("x", tokens[4]);
            width  = size[0].toInt();
            height = size[1].toInt();
        }

        frameNumber = results[7].toInt();

        PreviewGenerator *previewgen = new PreviewGenerator(pginfo);
        previewgen->SetPreviewTimeAsFrameNumber(frameNumber);
        previewgen->SetOutputFilename(outFile);
        previewgen->SetOutputSize(QSize(width, height));
        bool ok = previewgen->Run();
        previewgen->deleteLater();

        delete pginfo;

        QString str = "ERROR: Unable to generate screenshot, check logs";
        if (ok)
        {
            str = QString("OK %1x%2")
                .arg((width > 0) ? width : 64).arg((height > 0) ? height : 64);
        }

        return str;
    }
    else
        return "ERROR: Timed out waiting for reply from player";

    return "ERROR: Unknown reason";
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */

