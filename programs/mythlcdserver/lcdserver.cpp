/*
	lcdserver.cpp

	Methods for the core lcdserver object
*/


/*
    Command list
 
    SWITCH_TO_TIME
    SWITCH_TO_MUSIC "Artist" "Album" "Track"
    SWITCH_TO_VOLUME "AppName"
    SWITCH_TO_CHANNEL "ChanNum" "Title" "SubTitle"
    SWITCH_TO_NOTHING
    SWITCH_TO_MENU AppName popup ["Text" Checked Selected Scroll Indent ...]
        AppName is a string
        Popup can be TRUE or FALSE
        Text is a string
        Checked can be CHECKED, UNCHECKED, NOTCHECKABLE     
        Selected can be TRUE or FALSE
        Scroll can be TRUE or FALSE
        Indent is an integer
        ... repeat as required
        
    SWITCH_TO_GENERIC RowNo Align "Text" "Screen" Scrollable ...
        RowNo is an integer
        Align can be ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTERED
        Scrollable can be TRUE or FALSE 
        ... repeat as required
    
    SET_VOLUME_LEVEL <level>
        level is a float between 0.0 and 1.0
    
    SET_CHANNEL_PROGRESS <progress>
        progress is a float between 0.0 and 1.0
    
    SET_MUSIC_PROGRESS "Time" <progress>
        time is a string
        progress is a float between 0.0 and 1.0

    SET_MUSIC_PLAYER_PROP <field> <val>
        field is a string, currently SHUFFLE or REPEAT
        val depends on field, currently integer

    SET_GENERIC_PROGRESS <busy> <progress>
        busy is 0 for busy spinner, 0 for normal progess bar
        progress is a float between 0.0 and 1.0

    UPDATE_LEDS

    STOP_ALL

    RESET

*/

#include <stdlib.h>
#include <qstringlist.h>
#include <qregexp.h>
#include <qdir.h>
#include <qapplication.h>
#include "util.h"
#include "mythcontext.h"
#include "lcddevice.h"

#include "lcdserver.h"

int debug_level = 0;

LCDServer::LCDServer(int port, QString message, int messageTime)
    :QObject()
{
    m_lcd = new LCDProcClient(this);
    if (!m_lcd->SetupLCD())
    {
        delete m_lcd;
        m_lcd = NULL; 
    }

    //  Create the socket to listen to for connections
    m_serverSocket = new LCDServerSocket(port);
    connect(m_serverSocket, SIGNAL(newConnect(QSocket *)),
            this, SLOT(newConnection(QSocket *)));
    connect(m_serverSocket, SIGNAL(endConnect(QSocket *)),
            this, SLOT(endConnection(QSocket *)));

    m_lastSocket = NULL;

    //  Announce the port we're listening on
    if (debug_level > 0)
        VERBOSE(VB_NETWORK, QString("LCDServer: is listening on port %1")
                .arg(port));

    if (m_lcd)
        m_lcd->setStartupMessage(message, messageTime);
}

void LCDServer::newConnection(QSocket *socket)
{
    connect(socket, SIGNAL(readyRead()), this, SLOT(readSocket()));

    if (debug_level > 0)
        VERBOSE(VB_NETWORK, "LCDServer: new connection");

    if (m_lcd)
        m_lcd->switchToTime();
}

void LCDServer::endConnection(QSocket *socket)
{
    socket->close();

    if (debug_level > 0)
        VERBOSE(VB_NETWORK, "LCDServer: close connection");

    if (m_lastSocket == socket)
        m_lastSocket = NULL;
}

void LCDServer::readSocket()
{

    QSocket *socket = (QSocket *)sender();
    m_lastSocket = socket;

    while(socket->canReadLine())
    {
        QString incoming_data = socket->readLine();
        incoming_data = incoming_data.replace( QRegExp("\n"), "" );
        incoming_data = incoming_data.replace( QRegExp("\r"), "" );
        incoming_data.simplifyWhiteSpace();
        QStringList tokens = parseCommand(incoming_data);
        parseTokens(tokens, socket);
    }
}

QStringList LCDServer::parseCommand(QString &command)
{
    QStringList tokens;
    QString s = "";
    QChar c;
    bool bInString = false;

    for (uint x = 0; x < command.length(); x++)
    {
        c = command[x];
        if (!bInString && c == '"')
            bInString = true;
        else if (bInString && c == '"')
            bInString = false;
        else if (!bInString && c == ' ')
        {
            tokens.append(s);
            s = "";
        }    
        else
            s = s + c;
    }

    tokens.append(s);

    return tokens;
}

void LCDServer::parseTokens(const QStringList &tokens, QSocket *socket)
{
    //
    //  parse commands coming in from the socket
    //

    if (tokens[0] == "HALT" ||
       tokens[0] == "QUIT" ||
       tokens[0] == "SHUTDOWN")
    {
        shutDown();
    }
    else if (tokens[0] == "HELLO")
    {
        sendConnected(socket);
    }
    else if (tokens[0] == "SWITCH_TO_TIME")
    {
        switchToTime(socket);
    }
    else if (tokens[0] == "SWITCH_TO_MUSIC")
    {
        switchToMusic(tokens, socket);
    }
    else if (tokens[0] == "SWITCH_TO_VOLUME")
    {
        switchToVolume(tokens, socket);
    }
    else if (tokens[0] == "SWITCH_TO_GENERIC")
    {
        switchToGeneric(tokens, socket);
    }
    else if (tokens[0] == "SWITCH_TO_MENU")
    {
        switchToMenu(tokens, socket);
    }
    else if (tokens[0] == "SWITCH_TO_CHANNEL")
    {
        switchToChannel(tokens, socket);
    }
    else if (tokens[0] == "SWITCH_TO_NOTHING")
    {
        switchToNothing(socket);
    }
    else if (tokens[0] == "SET_VOLUME_LEVEL")
    {
        setVolumeLevel(tokens, socket);
    }
    else if (tokens[0] == "SET_GENERIC_PROGRESS")
    {
        setGenericProgress(tokens, socket);
    }
    else if (tokens[0] == "SET_MUSIC_PROGRESS")
    {
        setMusicProgress(tokens, socket);
    }
    else if (tokens[0] == "SET_MUSIC_PLAYER_PROP")
    {
        setMusicProp(tokens, socket);
    }
    else if (tokens[0] == "SET_CHANNEL_PROGRESS")
    {
        setChannelProgress(tokens, socket);
    }
    else if (tokens[0] == "UPDATE_LEDS")
    {
        updateLEDs(tokens, socket);
    }
    else if (tokens[0] == "STOP_ALL")
    {
        if (m_lcd)
            m_lcd->stopAll();
    }
    else if (tokens[0] == "RESET")
    {
        // reset lcd & reload settings
        if (m_lcd)
            m_lcd->reset();
    }
    else
    {
        QString did_not_parse = tokens.join(" ");

        if (debug_level > 0)
            VERBOSE(VB_IMPORTANT, "LCDServer::failed to parse this: " 
                << did_not_parse);

        sendMessage(socket, "HUH?");
    }
}

void LCDServer::shutDown()
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer:: shuting down");

    delete m_serverSocket;

    exit(0);
}

void LCDServer::sendMessage(QSocket *where, const QString &what)
{
    QString message = what;
    message.append("\n");
    where->writeBlock(message.utf8(), message.utf8().length());
}

void LCDServer::sendKeyPress(QString key_pressed)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer:: send key press: " << key_pressed);

    // send key press to last client that sent us a message
    if (m_lastSocket)
        sendMessage(m_lastSocket, "KEY " + key_pressed);  
}

void LCDServer::sendConnected(QSocket *socket)
{
    QString sWidth, sHeight;
    int nWidth = 0, nHeight = 0;

    if (m_lcd)
    {
        nWidth = m_lcd->getLCDWidth();
        nHeight = m_lcd->getLCDHeight();
    }

    sWidth = sWidth.setNum(nWidth);
    sHeight = sHeight.setNum(nHeight);

    sendMessage(socket, "CONNECTED " + sWidth + " " + sHeight);
}

void LCDServer::switchToTime(QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer:: SWITCH_TO_TIME");

    if (m_lcd)
        m_lcd->switchToTime();

    sendMessage(socket, "OK");
}

void LCDServer::switchToMusic(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_MUSIC");

    QString flat = tokens.join(" ");

    if (tokens.count() != 4)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SWITCH_TO_MUSIC command: " << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->switchToMusic(tokens[1], tokens[2], tokens[3]);

    sendMessage(socket, "OK");
}

void LCDServer::switchToGeneric(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_GENERIC");

    QString flat = tokens.join(" ");

    if ((tokens.count() - 1) % 5 != 0)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad no. of args SWITCH_TO_GENERIC "
                        "command: " <<  flat);
        sendMessage(socket, "HUH?");
        return;
    }

    QPtrList<LCDTextItem> items;
    items.setAutoDelete(true);

    for (uint x = 1; x < tokens.count(); x += 5)
    {
        bool bOK;
        int row = tokens[x].toInt(&bOK);
        if (!bOK)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad row no. in SWITCH_TO_GENERIC " 
                    "command: " << tokens[x]);
            sendMessage(socket, "HUH?");
            return;
        }

        TEXT_ALIGNMENT align;
        if (tokens[x + 1] == "ALIGN_LEFT")
            align = ALIGN_LEFT;
        else if (tokens[x + 1] == "ALIGN_RIGHT") 
            align = ALIGN_RIGHT;
        else if (tokens[x + 1] == "ALIGN_CENTERED")
            align = ALIGN_CENTERED;
        else
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad align in SWITCH_TO_GENERIC " 
                    "command: " << tokens[x + 1]);
            sendMessage(socket, "HUH?");
            return;
        }

        QString text = tokens[x + 2];
        QString screen = tokens[x + 3];
        bool scrollable;
        if (tokens[x + 4] == "TRUE")
            scrollable = true;
        else if (tokens[x + 4] == "FALSE")
            scrollable = false; 
        else
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad scrollable bool in "
                "SWITCH_TO_GENERIC command: " << tokens[x + 4]);
            sendMessage(socket, "HUH?");
            return;
        }

        items.append(new LCDTextItem(row, align, text, screen, scrollable));
    }

    if (m_lcd)
        m_lcd->switchToGeneric(&items);

    sendMessage(socket, "OK");
}

void LCDServer::switchToChannel(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_CHANNEL");

    QString flat = tokens.join(" ");

    if (tokens.count() != 4)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SWITCH_TO_CHANNEL command: " 
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->switchToChannel(tokens[1], tokens[2], tokens[3]);

    sendMessage(socket, "OK");
}

void LCDServer::switchToVolume(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_VOLUME");

    QString flat = tokens.join(" ");

    if (tokens.count() != 2)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SWITCH_TO_VOLUME command: " 
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->switchToVolume(tokens[1]);

    sendMessage(socket, "OK");
}

void LCDServer::switchToNothing(QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_NOTHING");

    if (m_lcd)
        m_lcd->switchToNothing();

    sendMessage(socket, "OK");
}

void LCDServer::switchToMenu(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SWITCH_TO_MENU: " << tokens.count());

    QString flat = tokens.join(" ");

    if ((tokens.count() - 3) % 5 != 0)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad no. of args SWITCH_TO_MENU command: " 
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    QString appName = tokens[1];

    bool bPopup;
    if (tokens[2] == "TRUE")
        bPopup = true;
    else if (tokens[2] == "FALSE")
        bPopup = false; 
    else
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad popup bool in SWITCH_TO_MENU " 
                        "command: " << tokens[2]);
        sendMessage(socket, "HUH?");
        return;
    }

    QPtrList<LCDMenuItem> items;
    items.setAutoDelete(true);

    for (uint x = 3; x < tokens.count(); x += 5)
    {
        QString text = tokens[x];

        CHECKED_STATE checked;
        if (tokens[x + 1] == "CHECKED")
            checked = CHECKED;    
        else if (tokens[x + 1] == "UNCHECKED") 
            checked = UNCHECKED;
        else if (tokens[x + 1] == "NOTCHECKABLE")
            checked = NOTCHECKABLE;
        else
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad checked state in " 
                    "SWITCH_TO_MENU command: " << tokens[x + 1]);
            sendMessage(socket, "HUH?");
            return;
        }

        bool selected;
        if (tokens[x + 2] == "TRUE")
            selected = true;
        else if (tokens[x + 2] == "FALSE")
            selected = false; 
        else
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad selected state in "
                    "SWITCH_TO_MENU command: " << tokens[x + 2]);
            sendMessage(socket, "HUH?");
            return;
        }

        bool scrollable;
        if (tokens[x + 3] == "TRUE")
            scrollable = true;
        else if (tokens[x + 3] == "FALSE")
            scrollable = false; 
        else
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad scrollable bool in "
                    "SWITCH_TO_MENU command: " << tokens[x + 3]);
            sendMessage(socket, "HUH?");
            return;
        }

        bool bOK;
        int indent = tokens[x + 4].toInt(&bOK);
        if (!bOK)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad indent in SWITCH_TO_MENU " 
                            "command: " << tokens[x + 4]);
            sendMessage(socket, "HUH?");
            return;
        }

        items.append(new LCDMenuItem(selected, checked, text, indent));
    }

    if (m_lcd)
        m_lcd->switchToMenu(&items, appName, bPopup);

    sendMessage(socket, "OK");
}

void LCDServer::setChannelProgress(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SET_CHANNEL_PROGRESS");

    QString flat = tokens.join(" ");

    if (tokens.count() != 2)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SET_CHANNEL_PROGRESS command: "
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    bool bOK;
    float progress = tokens[1].toFloat(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad float value in " 
                "SET_CHANNEL_PROGRESS command: %1" << tokens[1]);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->setChannelProgress(progress);

    sendMessage(socket, "OK");
}

void LCDServer::setGenericProgress(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SET_GENERIC_PROGRESS");

    QString flat = tokens.join(" ");

    if (tokens.count() != 3)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SET_GENERIC_PROGRESS command: "
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    bool bOK;
    bool busy = tokens[1].toInt(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad bool value in "
                "SET_GENERIC_PROGRESS command: %1 %2" << tokens[1] << tokens[2]);
        sendMessage(socket, "HUH?");
        return;
    }
    float progress = tokens[2].toFloat(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad float value in "
                "SET_GENERIC_PROGRESS command: %1" << tokens[1]);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->setGenericProgress(busy, progress);

    sendMessage(socket, "OK");
}

void LCDServer::setMusicProgress(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SET_MUSIC_PROGRESS");

    QString flat = tokens.join(" ");

    if (tokens.count() != 3)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SET_MUSIC_PROGRESS command: " 
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    bool bOK;
    float progress = tokens[2].toFloat(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad float value in "
                "SET_MUSIC_PROGRESS command: " << tokens[2]);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->setMusicProgress(tokens[1], progress);

    sendMessage(socket, "OK");
}

void LCDServer::setMusicProp(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SET_MUSIC_PROP");

    QString flat = tokens.join(" ");

    if (tokens.count() < 3)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SET_MUSIC_PROP command: "
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    if (tokens[1] == "SHUFFLE")
    {
        if (tokens.count () != 3)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: missing argument for "
                    "SET_MUSIC_PROP SHUFFLE command: " << flat);
            sendMessage(socket, "HUH?");
            return;
        }
        bool bOk;
        int state = tokens[2].toInt (&bOk);
        if (!bOk)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad argument for "
                    "SET_MUSIC_PROP SHUFFLE command: " << tokens[2]);
            sendMessage(socket, "HUH?");
            return;
        }
        if (m_lcd)
            m_lcd->setMusicShuffle (state);
    }
    else if (tokens[1] == "REPEAT")
    {
        if (tokens.count () != 3)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: missing argument for "
                    "SET_MUSIC_PROP REPEAT command: " << flat);
            sendMessage(socket, "HUH?");
            return;
        }
        bool bOk;
        int state = tokens[2].toInt (&bOk);
        if (!bOk)
        {
            VERBOSE(VB_IMPORTANT, "LCDServer: bad argument for "
                    "SET_MUSIC_PROP REPEAT command: " << tokens[2]);
            sendMessage(socket, "HUH?");
            return;
        }
        if (m_lcd)
            m_lcd->setMusicRepeat (state);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad argument for "
                "SET_MUSIC_PROP command: " << tokens[1]);
        sendMessage(socket, "HUH?");
        return;
    }

    sendMessage(socket, "OK");
}

void LCDServer::setVolumeLevel(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: SET_VOLUME_LEVEL");

    QString flat = tokens.join(" ");

    if (tokens.count() != 2)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad SET_VOLUME_LEVEL command: " 
                << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    bool bOK;
    float progress = tokens[1].toFloat(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad float value in "
                "SET_VOLUME_LEVEL command: " << tokens[1]);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->setVolumeLevel(progress);

    sendMessage(socket, "OK");
}

void LCDServer::updateLEDs(const QStringList &tokens, QSocket *socket)
{
    if (debug_level > 0)
        VERBOSE(VB_GENERAL, "LCDServer: UPDATE_LEDS");

    QString flat = tokens.join(" ");

    if (tokens.count() != 2)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad UPDATE_LEDs command: " << flat);
        sendMessage(socket, "HUH?");
        return;
    }

    bool bOK;
    int mask = tokens[1].toInt(&bOK);
    if (!bOK)
    {
        VERBOSE(VB_IMPORTANT, "LCDServer: bad mask in UPDATE_LEDS " 
                              "command: " << tokens[1]);
        sendMessage(socket, "HUH?");
        return;
    }

    if (m_lcd)
        m_lcd->updateLEDs(mask);

    sendMessage(socket, "OK");
}
