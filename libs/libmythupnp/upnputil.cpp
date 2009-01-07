//////////////////////////////////////////////////////////////////////////////
// Program Name: upnputil.cpp
//
// Purpose - Global Helper Methods...
//
// Created By  : David Blain                    Created On : Jan 15. 24, 2007
// Modified By :                                Modified On:
//
//////////////////////////////////////////////////////////////////////////////

// POSIX headers
#include <sys/types.h>
#include <sys/time.h>

// Qt headers
#include <quuid.h>

// MythTV headers
#include "upnputil.h"
#include "upnp.h"
#include "compat.h"
#include "mythconfig.h" // for HAVE_GETIFADDRS

// POSIX headers 2, needs to be after compat.h for OS X
#ifndef USING_MINGW
#include <net/if.h>
#endif // USING_MINGW
#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>
#endif

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

QString LookupUDN( QString sDeviceType )
{
    QStringList sList = QStringList::split( ":", sDeviceType );
    QString     sLoc  = "LookupUDN(" + sDeviceType + ")";
    QString     sName;
    QString     sUDN;

    if (sList.size() <= 2) 
    { 
        VERBOSE(VB_IMPORTANT, sLoc + "- bad device type, not enough tokens"); 
        return QString::null; 
    }

    sName = "UPnP/UDN/" + sList[ sList.size() - 2 ];
    sUDN  = UPnp::g_pConfig->GetValue( sName, "" );

    VERBOSE(VB_UPNP, sLoc + " sName=" + sName + ", sUDN=" + sUDN);

    if ( sUDN.length() == 0) 
    {
        sUDN = QUuid::createUuid().toString();

        sUDN = sUDN.mid( 1, sUDN.length() - 2);

        UPnp::g_pConfig->SetValue( sName, sUDN );

        UPnp::g_pConfig->Save();
    }

    return( sUDN );
}

/////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_GETIFADDRS

long GetIPAddressList(QStringList &sStrList)
{
    struct ifaddrs *list, *ifa;


    sStrList.clear();

    if (getifaddrs(&list) == -1)
    {
        VERBOSE(VB_UPNP, QString("GetIPAddressList() - getifaddrs failed: ")
                         + strerror(errno));
        return 0;
    }

    for (ifa=list; ifa; ifa=ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;


        char  address[15];

        if (inet_ntop(ifa->ifa_addr->sa_family,
                      &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                      address, sizeof(address)) == NULL)
        {
            VERBOSE(VB_UPNP, QString("GetIPAddressList() - inet_ntop failed: ")
                             + strerror(errno));
            continue;
        }

        sStrList.append(address);
        //VERBOSE(VB_UPNP, QString("GetIPAddressList() - Added %1 as %2")
        //                 .arg(ifa->ifa_name).arg(address));
    }

    freeifaddrs(list);

    return(sStrList.count());
}

#else // HAVE_GETIFADDRS

// On some Unixes (e.g. Darwin), this implementation is buggy because
// struct ifreq is variable size. Luckily, most of them have getifaddrs()

long GetIPAddressList( QStringList &sStrList )
{
#ifdef USING_MINGW
    VERBOSE(VB_UPNP, QString("GetIPAddressList() not implemented in MinGW"));
    return 0;
#else

    sStrList.clear();

    QSocketDevice socket( QSocketDevice::Datagram );

    struct ifreq  ifReqs[ 16 ];
    struct ifreq  ifReq;
    struct ifconf ifConf;

    // ----------------------------------------------------------------------
    // Get Configuration information...
    // ----------------------------------------------------------------------

    ifConf.ifc_len           = sizeof( struct ifreq ) * sizeof( ifReqs );
    ifConf.ifc_ifcu.ifcu_req = ifReqs;

    if ( ioctl( socket.socket(), SIOCGIFCONF, &ifConf ) < 0)
        return( 0 );

    long nCount = ifConf.ifc_len / sizeof( struct ifreq );

    // ----------------------------------------------------------------------
    // Loop through looking for IP addresses.
    // ----------------------------------------------------------------------

    for (long nIdx = 0; nIdx < nCount; nIdx++ )
    {
        // ------------------------------------------------------------------
        // Is this an interface we want?
        // ------------------------------------------------------------------

        strcpy ( ifReq.ifr_name, ifReqs[ nIdx ].ifr_name );

        if (ioctl ( socket.socket(), SIOCGIFFLAGS, &ifReq ) < 0)
            continue;

        // ------------------------------------------------------------------
        // Skip loopback and down interfaces
        // ------------------------------------------------------------------

        if ((ifReq.ifr_flags & IFF_LOOPBACK) || (!(ifReq.ifr_flags & IFF_UP)))
            continue;

        if ( ifReqs[ nIdx ].ifr_addr.sa_family == AF_INET)
        {
            struct sockaddr_in addr;

            // --------------------------------------------------------------
            // Get a pointer to the address
            // --------------------------------------------------------------

            memcpy (&addr, &(ifReqs[ nIdx ].ifr_addr), sizeof( ifReqs[ nIdx ].ifr_addr ));

            if (addr.sin_addr.s_addr != htonl( INADDR_LOOPBACK ))
            {
                QHostAddress address( htonl( addr.sin_addr.s_addr ));

                sStrList.append( address.toString() ); 
            }
        }
    }

    return( sStrList.count() );
#endif // !USING_MINGW
}

#endif // HAVE_GETIFADDRS

/////////////////////////////////////////////////////////////////////////////
//           
/////////////////////////////////////////////////////////////////////////////

bool operator< ( TaskTime t1, TaskTime t2 )
{
    if ( (t1.tv_sec  < t2.tv_sec) or 
        ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec < t2.tv_usec)))
    {
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
//           
/////////////////////////////////////////////////////////////////////////////

bool operator== ( TaskTime t1, TaskTime t2 )
{
    if ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec == t2.tv_usec))
        return true;

    return false;
}

/////////////////////////////////////////////////////////////////////////////
//           
/////////////////////////////////////////////////////////////////////////////

void AddMicroSecToTaskTime( TaskTime &t, suseconds_t uSecs )
{
    uSecs += t.tv_usec;

    t.tv_sec  += (uSecs / 1000000);
    t.tv_usec  = (uSecs % 1000000);
}

/////////////////////////////////////////////////////////////////////////////
//           
/////////////////////////////////////////////////////////////////////////////

void AddSecondsToTaskTime( TaskTime &t, long nSecs )
{
    t.tv_sec  += nSecs;
}
