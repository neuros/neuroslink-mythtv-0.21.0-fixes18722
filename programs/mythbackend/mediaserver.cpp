//////////////////////////////////////////////////////////////////////////////
// Program Name: mediaserver.cpp
//                                                                            
// Purpose - uPnp Media Server main Class
//                                                                            
// Created By  : David Blain                    Created On : Jan. 15, 2007
// Modified By :                                Modified On:                  
//                                                                            
//////////////////////////////////////////////////////////////////////////////

#include "mediaserver.h"
#include "mythxml.h"

#include "upnpcdstv.h"
#include "upnpcdsmusic.h"
#include "upnpcdsvideo.h"

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// UPnp Class implementaion
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

MediaServer::MediaServer( bool bIsMaster, bool bDisableUPnp /* = FALSE */ ) 
{
    VERBOSE(VB_UPNP, QString("MediaServer::Begin"));

    // ----------------------------------------------------------------------
    // Initialize Configuration class (Database for Servers)
    // ----------------------------------------------------------------------

    SetConfiguration( new DBConfiguration() );

    // ----------------------------------------------------------------------
    // Create mini HTTP Server
    // ----------------------------------------------------------------------

    int     nPort = g_pConfig->GetValue( "BackendStatusPort", 6544 );
    QString sIP   = g_pConfig->GetValue( "BackendServerIP"  , ""   );

    if (sIP.isEmpty())
    {
        VERBOSE(VB_IMPORTANT,
                "MediaServer:: No BackendServerIP Address defined");
        m_pHttpServer = NULL;
        return;
    }


    m_pHttpServer = new HttpServer( nPort ); 

    if (!m_pHttpServer->ok())
    {
        VERBOSE(VB_IMPORTANT, "MediaServer::HttpServer Create Error");
        // exit(BACKEND_BUGGY_EXIT_NO_BIND_STATUS);
        delete m_pHttpServer;
        m_pHttpServer = NULL;
        return;
    }

    m_sSharePath = gContext->GetShareDir();
    m_pHttpServer->m_sSharePath = m_sSharePath;

    QString sFileName = g_pConfig->GetValue( "upnpDescXmlPath",
                                                m_sSharePath );
    QString sDeviceType;

    if ( bIsMaster )
    {
        sFileName  += "devicemaster.xml";
        sDeviceType = "urn:schemas-mythtv-org:device:MasterMediaServer:1";
    }
    else
    {
        sFileName += "deviceslave.xml";
        sDeviceType = "urn:schemas-mythtv-org:device:SlaveMediaServer:1";
    }

    // ------------------------------------------------------------------
    // Make sure our device Description is loaded.
    // ------------------------------------------------------------------

    VERBOSE(VB_UPNP, "MediaServer::Loading UPnp Description " + sFileName );

    g_UPnpDeviceDesc.Load( sFileName );

    UPnpDevice *pMythDevice = UPnpDeviceDesc::FindDevice( RootDevice(),
                                                            sDeviceType );

    // ------------------------------------------------------------------
    // Register the MythXML protocol...
    // ------------------------------------------------------------------

    VERBOSE(VB_UPNP, "MediaServer::Registering MythXML Service." );

    m_pHttpServer->RegisterExtension( new MythXML( pMythDevice ));

    if (sIP == "localhost" || sIP.startsWith("127."))
    {
        VERBOSE(VB_IMPORTANT, "MediaServer:: Loopback address specified - "
                              + sIP + ". Disabling UPnP");
        return;
    }

    if (bDisableUPnp)
    {
        VERBOSE(VB_IMPORTANT, "*** The UPNP service has been DISABLED with the "
                                "--noupnp option ***");
        return;
    }

    // ----------------------------------------------------------------------
    // BackendServerIP is only one IP address at this time... Doing Split anyway
    // ----------------------------------------------------------------------

    QStringList sIPAddrList = QStringList::split( ";", sIP );

    // ----------------------------------------------------------------------
    // Initialize UPnp Stack
    // ----------------------------------------------------------------------

    if (Initialize( sIPAddrList, nPort, m_pHttpServer ))
    {

        // ------------------------------------------------------------------
        // Register any HttpServerExtensions... Only The Master Backend 
        // ------------------------------------------------------------------

        if (bIsMaster)
        {
            QString sSourceProtocols = "http-get:*:image/gif:*,"
                                       "http-get:*:image/jpeg:*,"
                                       "http-get:*:image/png:*,"
                                       "http-get:*:video/avi:*,"
                                       "http-get:*:audio/mpeg:*,"
                                       "http-get:*:audio/wav:*,"
                                       "http-get:*:video/mpeg:*,"
                                       "http-get:*:video/nupplevideo:*,"
                                       "http-get:*:video/x-ms-wmv:*";

            VERBOSE(VB_UPNP, "MediaServer::Registering MSRR Service." );

            m_pHttpServer->RegisterExtension( new UPnpMSRR( RootDevice(),
                                                m_sSharePath ) );

            VERBOSE(VB_UPNP, "MediaServer::Registering CMGR Service." );

            m_pUPnpCMGR = new UPnpCMGR( RootDevice(),
                                        m_sSharePath, sSourceProtocols );
            m_pHttpServer->RegisterExtension( m_pUPnpCMGR );

            VERBOSE(VB_UPNP, "MediaServer::Registering CDS Service." );

            m_pUPnpCDS = new UPnpCDS ( RootDevice(), m_sSharePath );
            m_pHttpServer->RegisterExtension( m_pUPnpCDS );

            // ----------------------------------------------------------------
            // Register CDS Extensions
            // ----------------------------------------------------------------

            VERBOSE(VB_UPNP, "MediaServer::Registering UPnpCDSTv Extension");

            RegisterExtension(new UPnpCDSTv());

            VERBOSE(VB_UPNP, "MediaServer::Registering UPnpCDSMusic Extension");

            RegisterExtension(new UPnpCDSMusic());

            VERBOSE(VB_UPNP, "MediaServer::Registering UPnpCDSVideo Extension");

            RegisterExtension(new UPnpCDSVideo());

            upnpMedia = new UPnpMedia(true,true);
            //upnpMedia->BuildMediaMap();
        }

        // VERBOSE(VB_UPNP, QString( "MediaServer::Adding Context Listener" ));

        // gContext->addListener( this );

        Start();

    }

    VERBOSE(VB_UPNP, QString( "MediaServer::End" ));
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

MediaServer::~MediaServer()
{
    // -=>TODO: Need to check to see if calling this more than once is ok.

//    gContext->removeListener(this);

    if (m_pHttpServer)
        delete m_pHttpServer;
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////
/*
void MediaServer::customEvent( QCustomEvent *e )
{
    if (MythEvent::Type(e->type()) == MythEvent::MythEventMessage)
    {
        MythEvent *me = (MythEvent *)e;
        QString message = me->Message();

        //-=>TODO: Need to handle events to notify clients of changes
    }
}
*/
//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

void MediaServer::RegisterExtension( UPnpCDSExtension *pExtension )
{
    m_pUPnpCDS->RegisterExtension( pExtension );
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

void MediaServer::UnregisterExtension( UPnpCDSExtension *pExtension )
{
    m_pUPnpCDS->UnregisterExtension( pExtension );
}
