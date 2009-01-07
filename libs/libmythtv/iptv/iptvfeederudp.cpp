/** -*- Mode: c++ -*-
 *  IPTVFeederUdp
 *  Copyright (c) 2006 by Mike Mironov & Mickaël Remars
 *  Distributed as part of MythTV under GPL v2 and later.
 */
#include <algorithm>

#include "iptvfeederudp.h"

// Qt headers
#include <qurl.h>

// Live555 headers
#include <BasicUsageEnvironment.hh>
#include <Groupsock.hh>
#include <GroupsockHelper.hh>
#include <BasicUDPSource.hh>
#include <TunnelEncaps.hh>

// MythTV headers
#include "iptvmediasink.h"
#include "mythcontext.h"
#include "tspacket.h"

#define LOC QString("IPTVFeedUDP: ")
#define LOC_ERR QString("IPTVFeedUDP, Error: ")

IPTVFeederUDP::IPTVFeederUDP() :
    _source(NULL),
    _sink(NULL)
{
    VERBOSE(VB_RECORD, LOC + "ctor -- success");
}

IPTVFeederUDP::~IPTVFeederUDP()
{
    VERBOSE(VB_RECORD, LOC + "dtor -- begin");
    Close();
    VERBOSE(VB_RECORD, LOC + "dtor -- end");
}

bool IPTVFeederUDP::IsUDP(const QString &url)
{
    return url.startsWith("udp://", false);
}

bool IPTVFeederUDP::Open(const QString &url)
{
    VERBOSE(VB_RECORD, LOC + QString("Open(%1) -- begin").arg(url));

    QMutexLocker locker(&_lock);

    if (_source)
    {
        VERBOSE(VB_RECORD, LOC + "Open() -- end 1");
        return true;
    }
        
    QUrl parse(url);
    if (!parse.isValid() || !parse.hasHost() || !parse.hasPort())
    {
        VERBOSE(VB_RECORD, LOC + "Open() -- end 2");
        return false;
    }
        
    struct in_addr addr;
    addr.s_addr = our_inet_addr(parse.host().latin1());

    // Begin by setting up our usage environment:
    if (!InitEnv())
        return false;
    
    Groupsock *socket = new Groupsock(*_live_env, addr, parse.port(), 0);
    if (!socket)
    {
        VERBOSE(VB_IMPORTANT, LOC + "Failed to create Live UDP Socket.");
        FreeEnv();
        return false;
    }
    _source = BasicUDPSource::createNew(*_live_env, socket);
    if (!_source)
    {
        VERBOSE(VB_IMPORTANT, LOC + "Failed to create Live UDP Source.");

        if (socket)
            delete socket;

        FreeEnv();
        return false;
    }

    _sink = IPTVMediaSink::CreateNew(*_live_env, TSPacket::SIZE * 128*1024);
    if (!_sink)
    {
        VERBOSE(VB_IMPORTANT,
                QString("IPTV # Failed to create sink: %1")
                .arg(_live_env->getResultMsg()));

        Medium::close(_source);
        _source = NULL;
        if (socket)
            delete socket;
        FreeEnv();

        return false;
    }

    _sink->startPlaying(*_source, NULL, NULL);
    vector<TSDataListener*>::iterator it = _listeners.begin();
    for (; it != _listeners.end(); ++it)
        _sink->AddListener(*it);
        
    VERBOSE(VB_RECORD, LOC + "Open() -- end");

    return true;
}

void IPTVFeederUDP::Close(void)
{
    VERBOSE(VB_RECORD, LOC + "Close() -- begin");
    Stop();

    QMutexLocker locker(&_lock);

    if (_sink)
    {
        Medium::close(_sink);
        _sink = NULL;
    }

    if (_source)
    {
        Groupsock *socket = _source->gs();
        Medium::close(_source);
        _source = NULL;
        if (socket)
            delete socket;
    }

    FreeEnv();

    VERBOSE(VB_RECORD, LOC + "Close() -- end");
}

void IPTVFeederUDP::AddListener(TSDataListener *item)
{
    VERBOSE(VB_RECORD, LOC + "AddListener("<<item<<") -- begin");
    if (!item)
    {
        VERBOSE(VB_RECORD, LOC + "AddListener("<<item<<") -- end");
        return;
    }

    // avoid duplicates
    RemoveListener(item);

    // add to local list
    QMutexLocker locker(&_lock);
    _listeners.push_back(item);
    
    if (_sink)
        _sink->AddListener(item);

    VERBOSE(VB_RECORD, LOC + "AddListener("<<item<<") -- end");
}

void IPTVFeederUDP::RemoveListener(TSDataListener *item)
{
    VERBOSE(VB_RECORD, LOC + "RemoveListener("<<item<<") -- begin");
    QMutexLocker locker(&_lock);
    vector<TSDataListener*>::iterator it =
        find(_listeners.begin(), _listeners.end(), item);

    if (it == _listeners.end())
    {
        VERBOSE(VB_RECORD, LOC + "RemoveListener("<<item<<") -- end 1");
        return;
    }

    // remove from local list..
    *it = *_listeners.rbegin();
    _listeners.resize(_listeners.size() - 1);

    if (_sink)
        _sink->RemoveListener(item);

    VERBOSE(VB_RECORD, LOC + "RemoveListener("<<item<<") -- end 2");
}
