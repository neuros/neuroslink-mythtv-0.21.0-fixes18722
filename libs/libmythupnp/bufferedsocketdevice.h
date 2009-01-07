//////////////////////////////////////////////////////////////////////////////
// Program Name: bufferedsocketdevice.h
//                                                                            
// Purpose - 
//                                                                            
// Created By  : David Blain                    Created On : Oct. 1, 2005
// Modified By :                                Modified On:                  
//                                                                            
//////////////////////////////////////////////////////////////////////////////

#ifndef __BUFFEREDSOCKETDEVICE_H__
#define __BUFFEREDSOCKETDEVICE_H__

// Qt headers
#include <qsocketdevice.h>

// MythTV headers
#include "private/qinternal_p.h"
#include "compat.h"

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
// 
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

class BufferedSocketDevice 
{
    protected:

        QSocketDevice          *m_pSocket;

        Q_ULONG                 m_nMaxReadBufferSize; 
        QIODevice::Offset       m_nWriteSize;           // write total buf size
        QIODevice::Offset       m_nWriteIndex;          // write index

        bool                    m_bHandleSocketDelete;

        QHostAddress            m_DestHostAddress;
        Q_UINT16                m_nDestPort;

        QMembuf                 m_bufRead;
        QPtrList<QByteArray>    m_bufWrite;

        int     ReadBytes      ( );
        bool    ConsumeWriteBuf( Q_ULONG nbytes );

    public:

        BufferedSocketDevice( int nSocket );
        BufferedSocketDevice( QSocketDevice *pSocket = NULL,
                              bool    bTakeOwnership = false );

        virtual ~BufferedSocketDevice( );

        QSocketDevice      *SocketDevice        ();
        void                SetSocketDevice     ( QSocketDevice *pSocket );

        void                SetDestAddress      ( QHostAddress hostAddress,
                                                  Q_UINT16     nPort );

        bool                Connect             ( const QHostAddress & addr, Q_UINT16 port );
        void                Close               ();
        void                Flush               ();
        QIODevice::Offset   Size                ();
        QIODevice::Offset   At                  (); 
        bool                At                  ( QIODevice::Offset );
        bool                AtEnd               ();

        Q_ULONG             BytesAvailable      (); 
        Q_ULONG             WaitForMore         ( int msecs, bool *timeout = NULL );

        Q_ULONG             BytesToWrite        () const;
        void                ClearPendingData    ();
        void                ClearReadBuffer     ();

        Q_LONG              ReadBlock           ( char *data, Q_ULONG maxlen );
        Q_LONG              WriteBlock          ( const char *data, Q_ULONG len );
        Q_LONG              WriteBlockDirect    ( const char *data, Q_ULONG len );

        int                 Getch               ();
        int                 Putch               ( int );
        int                 Ungetch             (int);

        bool                CanReadLine         ();
        QString             ReadLine            ();
        QString             ReadLine            ( int msecs );
        Q_LONG              ReadLine            ( char *data, Q_ULONG maxlen );

        Q_UINT16            Port                () const;
        Q_UINT16            PeerPort            () const;
        QHostAddress        Address             () const;
        QHostAddress        PeerAddress         () const;

        void                SetReadBufferSize   ( Q_ULONG );
        Q_ULONG             ReadBufferSize      () const;

        bool                IsValid             () { return( ( m_pSocket ) ? m_pSocket->isValid() : false ); }
        int                 socket              () { return( ( m_pSocket ) ? m_pSocket->socket() : 0 ); }
};

#endif
