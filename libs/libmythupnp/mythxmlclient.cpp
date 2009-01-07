//////////////////////////////////////////////////////////////////////////////
// Program Name: mythxmlclient.cpp
//                                                                            
// Purpose - Myth XML protocol client
//                                                                            
// Created By  : David Blain                    Created On : Mar. 19, 2007
// Modified By :                                Modified On:                  
//                                                                            
//////////////////////////////////////////////////////////////////////////////

#include "mythxmlclient.h"

/////////////////////////////////////////////////////////////////////////////
// 
/////////////////////////////////////////////////////////////////////////////

MythXMLClient::MythXMLClient( const QUrl &url, bool bInQtThread )
              :   SOAPClient( url,
                              "urn:schemas-mythtv-org:service:MythTv:1",
                              "/Myth")
{
    m_bInQtThread = bInQtThread;
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

MythXMLClient::~MythXMLClient() 
{
}

/////////////////////////////////////////////////////////////////////////////
// 
/////////////////////////////////////////////////////////////////////////////

UPnPResultCode MythXMLClient::GetConnectionInfo( const QString &sPin, DatabaseParams *pParams, QString &sMsg )
{
    if (pParams == NULL)
        return UPnPResult_InvalidArgs;

    int           nErrCode = 0;
    QString       sErrDesc;
    QStringMap    list;

    sMsg = "";

    list.insert( "Pin", sPin );

    if (SendSOAPRequest( "GetConnectionInfo", list, nErrCode, sErrDesc, m_bInQtThread ))
    {
        QString sXml = "<Info>" + list[ "Info" ] + "</Info>";

        sMsg = sErrDesc;

        QDomDocument doc;

        if ( !doc.setContent( sXml, false, &sErrDesc, &nErrCode ))
        {
            sMsg = QString( "Error Requesting Connection Info : (%1) - %2" )
                                 .arg( nErrCode )
                                 .arg( sErrDesc );

            VERBOSE( VB_UPNP, sMsg );

            return UPnPResult_ActionFailed;
        }

        // --------------------------------------------------------------
        // Is this a valid response?
        // --------------------------------------------------------------

        QDomNode infoNode = doc.namedItem( "Info" );

        if (!infoNode.isNull())
        {
            QDomNode dbNode = infoNode.namedItem( "Database" );

            pParams->dbHostName     = GetNodeValue( dbNode, "Host"     , QString( "" ));
            pParams->dbPort         = GetNodeValue( dbNode, "Port"     , 0            );
            pParams->dbUserName     = GetNodeValue( dbNode, "UserName" , QString( "" ));
            pParams->dbPassword     = GetNodeValue( dbNode, "Password" , QString( "" ));
            pParams->dbName         = GetNodeValue( dbNode, "Name"     , QString( "" ));
            pParams->dbType         = GetNodeValue( dbNode, "Type"     , QString( "" ));

            QDomNode wolNode = infoNode.namedItem( "WOL" );

            pParams->wolEnabled     = GetNodeValue( wolNode, "Enabled"  , false        );
            pParams->wolReconnect   = GetNodeValue( wolNode, "Reconnect", 0            );
            pParams->wolRetry       = GetNodeValue( wolNode, "Retry"    , 0            );
            pParams->wolCommand     = GetNodeValue( wolNode, "Command"  , QString( "" ));

            return UPnPResult_Success;
        }
        else
        {
            if (sMsg.isEmpty())
                sMsg = "Unexpected Response";

            VERBOSE( VB_IMPORTANT, QString( "MythXMLClient::GetConnectionInfo Failed : Unexpected Response - %1" )
                                      .arg( sXml   ));
        }
    }
    else
    {
        sMsg = sErrDesc;

        if (sMsg.isEmpty())
            sMsg = "Access Denied";

        VERBOSE( VB_IMPORTANT, QString( "MythXMLClient::GetConnectionInfo Failed - (%1) %2" )
                             .arg( nErrCode )
                             .arg( sErrDesc ));
    }
    
    if (UPnPResult_HumanInterventionRequired == nErrCode
           || UPnPResult_ActionNotAuthorized == nErrCode)
        return (UPnPResultCode)nErrCode;

    return UPnPResult_ActionFailed;
}
