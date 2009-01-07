// -*- Mode: c++ -*-
#ifndef _SOURCEUTIL_H_
#define _SOURCEUTIL_H_

// Qt headers
#include <qstring.h>

#include "mythexp.h"

class MPUBLIC SourceUtil
{
  public:
    static bool    HasDigitalChannel(uint sourceid);
    static QString GetSourceName(uint sourceid);
    static QString GetChannelSeparator(uint sourceid);
    static QString GetChannelFormat(uint sourceid);
    static uint    GetChannelCount(uint sourceid);
    static bool    GetListingsLoginData(uint sourceid,
                                        QString &grabber, QString &userid,
                                        QString &passwd,  QString &lineupid);
    static uint    GetConnectionCount(uint sourceid);
    static bool    IsProperlyConnected(uint sourceid, bool strich = false);
    static bool    IsEncoder(uint sourceid, bool strict = false);
    static bool    IsUnscanable(uint sourceid);
    static bool    IsAnySourceScanable(void);
    static bool    UpdateChannelsFromListings(
        uint sourceid, QString cardtype = QString::null);

    static bool    DeleteSource(uint sourceid);
    static bool    DeleteAllSources(void);
};

#endif //_SOURCEUTIL_H_
