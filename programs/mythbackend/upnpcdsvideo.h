//////////////////////////////////////////////////////////////////////////////
// Program Name: upnpcdstv.h
//                                                                            
// Purpose - uPnp Content Directory Extention for Recorded TV 
//                                                                            
// Created By  : David Blain                    Created On : Jan. 24, 2005
// Modified By :                                Modified On:                  
//                                                                            
//////////////////////////////////////////////////////////////////////////////

#ifndef UPnpCDSVIDEO_H_
#define UPnpCDSVIDEO_H_

#include "mainserver.h"
#include "upnpcds.h"
              
typedef QMap<int, QString> IntMap;

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

class UPnpCDSVideo : public UPnpCDSExtension
{
    private:

        static UPnpCDSRootInfo g_RootNodes[];
        static int             g_nRootCount;

        QStringMap             m_mapBackendIp;
        QStringMap             m_mapBackendPort;

    protected:

        virtual UPnpCDSExtensionResults *ProcessItem( UPnpCDSRequest          *pRequest,
                                                      UPnpCDSExtensionResults *pResults,
                                                      QStringList             &idPath );

        virtual void             CreateItems   ( UPnpCDSRequest          *pRequest,
                                                 UPnpCDSExtensionResults *pResults,
                                                 int                      nNodeIdx,
                                                 const QString           &sKey, 
                                                 bool                     bAddRef );

        virtual bool             IsBrowseRequestForUs( UPnpCDSRequest *pRequest );
        virtual bool             IsSearchRequestForUs( UPnpCDSRequest *pRequest );

        virtual int              GetDistinctCount( UPnpCDSRootInfo *pInfo );

        virtual UPnpCDSRootInfo *GetRootInfo   (int nIdx);
        virtual int              GetRootCount  ( );
        virtual QString          GetTableName  ( QString sColumn );
        virtual QString          GetItemListSQL( QString sColumn = "");

        virtual void             BuildItemQuery( MSqlQuery        &query, 
                                                 const QStringMap &mapParams );

        virtual void             AddItem( const QString           &sObjectId,
                                          UPnpCDSExtensionResults *pResults,
                                          bool                     bAddRef, 
                                          MSqlQuery               &query );

    public:

        UPnpCDSVideo( ) : UPnpCDSExtension( "Videos", "Videos",
                                            "object.item.videoItem" )
        {
        }
        virtual ~UPnpCDSVideo() {}
};

#endif
