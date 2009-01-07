/**
 *  DBox2Channel
 *  Copyright (c) 2005 by Levent Gündogdu
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// C++ headers
#include <iostream>
using namespace std;

// Qt headers
#include <qdeepcopy.h>
#include <qhttp.h>

// MythTV headers
#include "mythcontext.h"
#include "dbox2channel.h"
#include "dbox2recorder.h"
#include "dbox2epg.h"

//#define DBOX2_CHANNEL_DEBUG

#define LOC      QString("DBox2Ch(%1): ").arg(m_cardid)
#define LOC_WARN QString("DBox2Ch(%1) Warning: ").arg(m_cardid)
#define LOC_ERR  QString("DBox2Ch(%1) Error: ").arg(m_cardid)

void DBox2CRelay::SetChannel(DBox2Channel *ch)
{
    QMutexLocker locker(&m_lock);
    m_ch = ch;
}

void DBox2CRelay::HttpChannelChangeDone(bool error)
{
    QMutexLocker locker(&m_lock);
    if (m_ch)
        m_ch->HttpChannelChangeDone(error);
}

void DBox2CRelay::HttpRequestDone(bool error)
{
    QMutexLocker locker(&m_lock);
    if (m_ch)
        m_ch->HttpRequestDone(error);
}

DBox2Channel::DBox2Channel(TVRec *parent, DBox2DBOptions *dbox2_options,
                           int cardid)
    : ChannelBase(parent),
      m_dbox2options(dbox2_options), m_cardid(cardid),
      m_channelListReady(false),     m_lastChannel("1"),
      m_requestChannel(""),          m_epg(new DBox2EPG()),
      m_recorderAlive(false),        m_recorder(NULL),
      http(new QHttp()),             httpChanger(new QHttp()),
      m_relay(new DBox2CRelay(this)),
      m_dbox2channelcount(0)
{
    QObject::connect(http,        SIGNAL(           done(bool)),
                     m_relay,     SLOT(  HttpRequestDone(bool)));
    QObject::connect(httpChanger, SIGNAL(                 done(bool)),
                     m_relay,     SLOT(  HttpChannelChangeDone(bool)));

    // Load channel names and ids from the dbox
    LoadChannels();
}

void DBox2Channel::TeardownAll(void)
{
    // Shutdown EPG
    if (m_epg)
    {
        m_epg->Shutdown();
        m_epg->disconnect();
        m_epg->deleteLater();
        m_epg = NULL;
    }

    // Abort pending channel changes
    if (httpChanger)
    {
        httpChanger->abort();
        httpChanger->closeConnection();
        httpChanger->disconnect();
        httpChanger->deleteLater();
        httpChanger = NULL;
    }

    // Abort pending channel list requests
    if (http)
    {
        http->abort();
        http->closeConnection();
        http->disconnect();
        http->deleteLater();
        http = NULL;
    }

    if (m_relay)
    {
        m_relay->SetChannel(NULL);
        m_relay->deleteLater();
        m_relay = NULL;
    }
}

void DBox2Channel::SetRecorder(DBox2Recorder *rec)
{
    QMutexLocker locker(&m_lock);
    m_recorder = rec;
}

void DBox2Channel::SwitchToLastChannel(void)
{
    VERBOSE(VB_CHANNEL, LOC + QString("Switching to last channel '%1'.")
            .arg(m_lastChannel));

    SetChannelByString(m_lastChannel);
}

bool DBox2Channel::SetChannelByString(const QString &newChan)
{
    // Delay set channel when list has not yet been retrieved
    if (!m_channelListReady)
    {
        VERBOSE(VB_CHANNEL, LOC + "Channel list not received yet. \n\t\t\t" +
                QString("Will switch to channel %1 later...").arg(newChan));

        m_requestChannel = QDeepCopy<QString>(newChan);
        return true;
    }

    QString chan = QDeepCopy<QString>(newChan);
    if (chan.isEmpty())
    {
        VERBOSE(VB_CHANNEL, LOC + "Empty channel name has been provided. "
                "\n\t\t\tGetting default name.");

        chan = GetDefaultChannel();
    }

    VERBOSE(VB_CHANNEL, LOC + QString("Changing to '%1'.").arg(chan));

    // input switching code would go here

    InputMap::const_iterator it = inputs.find(currentInputID);
    if (it == inputs.end())
        return false;

    uint mplexid_restriction;
    if (!IsInputAvailable(currentInputID, mplexid_restriction))
        return false;

    if (m_lastChannel != curchannelname)
        m_lastChannel = curchannelname;
    curchannelname = chan;

    // Zap DBox2 to requested channel
    // Find channel name from channel number
    QString channelName = GetChannelNameFromNumber(chan);
    if (channelName.isEmpty())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString("Changing to '%1' failed. ")
                .arg(chan) + "Channel not found!");

        QString defaultChannel = GetDefaultChannel();
        if (defaultChannel != chan)
        {
            VERBOSE(VB_CHANNEL, LOC + QString("Trying default channel '%1'")
                    .arg(defaultChannel));

            return SetChannelByString(defaultChannel);
        }
        return false;
    }

    // Find dbox2 channel id from channel name
    QString channelID = GetChannelID(channelName);
    if (channelID.isEmpty())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString(
                    "Changing to '%1' failed. "
                    "DBox2 channel ID for name '%2' not found!")
                .arg(chan).arg(channelName));

        QString defaultChannel = GetDefaultChannel();
        if (defaultChannel != chan)
        {
            VERBOSE(VB_CHANNEL, LOC + QString("Trying default channel '%1'")
                    .arg(defaultChannel));

            return SetChannelByString(defaultChannel);
        }
        return false;
    }

    VERBOSE(VB_CHANNEL, LOC + QString("Channel ID for '%1' is '%2'.")
            .arg(chan).arg(channelID));

    // Request channel change
    m_lock.lock();
    if (m_recorder)
        m_recorder->ChannelChanging();
    m_lock.unlock();

    RequestChannelChange(channelID);
    return true;
}

bool DBox2Channel::Open(void)
{
    VERBOSE(VB_CHANNEL, LOC + "Open()");

    if (!InitializeInputs())
        return false;

    return true;
}

void DBox2Channel::Close(void)
{
    VERBOSE(VB_CHANNEL, LOC + "Close()");
}

void DBox2Channel::LoadChannels(void)
{
    VERBOSE(VB_CHANNEL, LOC + "Loading channels...\n\t\t\t" +
            QString("Reading channel list from %1:%2")
            .arg(m_dbox2options->host).arg(m_dbox2options->httpport));

    // Request Channel list via http.
    // Signal will be emmitted when list is ready.

    QHttpRequestHeader header("GET", "/control/channellist");
    header.setValue("Host", m_dbox2options->host);
    http->setHost(m_dbox2options->host, m_dbox2options->httpport);
    http->request(header);
}

void DBox2Channel::HttpRequestDone(bool error)
{
    if (error)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Reading channel list failed!");
        return;
    }

    QString buffer = http->readAll();

    VERBOSE(VB_CHANNEL, LOC + "Reading channel list succeeded.");

    m_dbox2channelcount = 0;

    while (true)
    {
        QString line = buffer.section("\n", m_dbox2channelcount,
                                      m_dbox2channelcount);

        if (line.isEmpty())
            break;

        m_dbox2channelids[m_dbox2channelcount] = line.section(" ", 0, 0);
        m_dbox2channelnames[m_dbox2channelcount] = line.section(" ", 1, 5);

#ifdef DBOX2_CHANNEL_DEBUG
        VERBOSE(VB_CHANNEL, LOC + QString("Found Channel '%1'.")
                .arg(m_dbox2channelnames[m_dbox2channelcount]));
#endif

        m_dbox2channelcount++;
    }

    VERBOSE(VB_CHANNEL, LOC + QString("Read %1 channels.")
            .arg(m_dbox2channelcount));

    // Initialize EPG
    m_epg->Init(m_dbox2options, m_cardid, this);

    // Channel list is ready.
    m_channelListReady = true;

    // Change channel if request available
    if (!m_requestChannel.isEmpty())
    {
        SetChannelByString(m_requestChannel);
        m_requestChannel = "";
    }
}

void DBox2Channel::RequestChannelChange(QString channelID)
{
    // Prepare request
    QString requestString;
    requestString = QString("/control/zapto?%1").arg(channelID);

    VERBOSE(VB_CHANNEL, LOC +
            QString("Changing channel on %1:%2 to channel id %3: %4")
            .arg(m_dbox2options->host).arg(m_dbox2options->httpport)
            .arg(channelID).arg(requestString));

    // Request channel change via http.
    // Signal will be emmited when request is done.

    QHttpRequestHeader header("GET", requestString);
    header.setValue("Host", m_dbox2options->host);
    httpChanger->setHost(m_dbox2options->host, m_dbox2options->httpport);
    httpChanger->request(header);
}

void DBox2Channel::HttpChannelChangeDone(bool error)
{
    if (error)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Changing channel failed!");
        return;
    }

    QString buffer = httpChanger->readAll();
    QString line   = buffer;

    if (line == "ok")
    {
        VERBOSE(VB_CHANNEL, LOC + "Changing channel succeeded...");

        // Send signal to record that channel has changed.
        m_lock.lock();
        if (m_recorder)
            m_recorder->ChannelChanged();
        m_lock.unlock();

        // Request EPG for this channel if recorder is not alive
        if (!m_recorderAlive)
          m_epg->ScheduleRequestEPG(curchannelname);
        return;
    }

    VERBOSE(VB_IMPORTANT, LOC_ERR +
            QString("Changing channel failed: %1.").arg(line));

    return;
}

QString DBox2Channel::GetChannelID(const QString &name)
{
    for (int i = m_dbox2channelcount-1; i >= 0; i--)
        if (m_dbox2channelnames[i].upper() == name.upper())
            return m_dbox2channelids[i];

    return "";
}

QString DBox2Channel::GetChannelNameFromNumber(const QString &channelnumber)
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT name "
        "FROM channel, cardinput "
        "WHERE channel.sourceid = cardinput.sourceid AND "
        "      cardinput.cardid = :CARDID AND "
        "      channel.channum  = :CHANNUM");
    query.bindValue(":CARDID",  m_cardid);
    query.bindValue(":CHANNUM", channelnumber);

    if (query.exec() && query.isActive() && query.next())
        return query.value(0).toString();

    return "";
}

QString DBox2Channel::GetChannelNumberFromName(const QString &channelName)
{
    VERBOSE(VB_CHANNEL, LOC + "Getting channel number from " +
            QString("channel '%1'.").arg(channelName));

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT channum "
        "FROM channel, cardinput "
        "WHERE channel.sourceid = cardinput.sourceid AND "
        "      cardinput.cardid = :CARDID AND "
        "      channel.name     = :NAME");
    query.bindValue(":CARDID", m_cardid);
    query.bindValue(":NAME", channelName);

    if (query.exec() && query.isActive() && query.next())
        return query.value(0).toString();

    VERBOSE(VB_IMPORTANT, LOC_ERR + "Channel number from channel " +
            QString("'%1' is unknown.").arg(channelName));

    return "";
}

QString DBox2Channel::GetDefaultChannel(void)
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT channum "
        "FROM channel, cardinput "
        "WHERE channel.sourceid = cardinput.sourceid AND "
        "      cardinput.cardid = :CARDID "
        "ORDER BY channum limit 1");
    query.bindValue(":CARDID", m_cardid);

    if (query.exec() && query.isActive() && query.next())
        return query.value(0).toString();

    return "";
}

void DBox2Channel::RecorderAlive(bool alive)
{
    if (m_recorderAlive == alive)
        return;

    m_recorderAlive = alive;
    if (m_recorderAlive)
    {
        VERBOSE(VB_EIT, LOC + "Recorder now online. Deactivating EPG scan");
    }
    else
    {
        VERBOSE(VB_EIT, LOC + "Recorder now offline. Reactivating EPG scan");
        uint nextchanid = GetNextChannel(0, CHANNEL_DIRECTION_UP);
        SetChannelByString(ChannelUtil::GetChanNum(nextchanid));
    }
}

void DBox2Channel::EPGFinished(void)
{
    // Switch to next channel to retrieve EPG
    if (m_recorderAlive)
    {
        VERBOSE(VB_EIT, LOC + "EPG finished. Recorder still online. "
                "Deactivating EPG scan");
    }
    else
    {
        VERBOSE(VB_EIT, LOC + "EPG finished. Recorder still offline. "
                "Continuing EPG scan");
        uint nextchanid = GetNextChannel(0, CHANNEL_DIRECTION_UP);
        SetChannelByString(ChannelUtil::GetChanNum(nextchanid));
    }
}
