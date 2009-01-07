#include <qapplication.h>
#include <qsqldatabase.h>
#include <qdatetime.h>
#include <qfile.h>
#include <qdir.h>
#include <qurl.h>
#include <qthread.h>
#include <qwaitcondition.h>
#include <qregexp.h>

#include <cstdlib>
#include <cerrno>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include "../../config.h"
#ifdef HAVE_SYS_SOUNDCARD_H
    #include <sys/soundcard.h>
#elif HAVE_SOUNDCARD_H
    #include <soundcard.h>
#endif
#ifndef USING_MINGW
#include <sys/ioctl.h>
#endif

#include <list>
#include <iostream>
using namespace std;

#include <sys/stat.h>
#ifdef __linux__
#  include <sys/vfs.h>
#else // if !__linux__
#  include <sys/param.h>
#  ifndef USING_MINGW
#    include <sys/mount.h>
#  endif // USING_MINGW
#endif // !__linux__

#include "libmyth/exitcodes.h"
#include "libmyth/mythcontext.h"
#include "libmyth/util.h"
#include "libmyth/mythdbcon.h"

#include "mainserver.h"
#include "scheduler.h"
#include "backendutil.h"
#include "programinfo.h"
#include "jobqueue.h"
#include "autoexpire.h"
#include "previewgenerator.h"
#include "storagegroup.h"
#include "compat.h"

/** Milliseconds to wait for an existing thread from
 *  process request thread pool.
 */
#define PRT_TIMEOUT 10
/** Number of threads in process request thread pool at startup. */
#define PRT_STARTUP_THREAD_COUNT 5

namespace {

int delete_file_immediately(const QString &filename,
                            bool followLinks, bool checkexists)
{
    /* Return 0 for success, non-zero for error. */
    QFile checkFile(filename);
    int success1, success2;

    VERBOSE(VB_FILE, QString("About to delete file: %1").arg(filename));
    success1 = true;
    success2 = true;
    if (followLinks)
    {
        QFileInfo finfo(filename);
        if (finfo.isSymLink())
        {
            QString linktext = finfo.readLink();
            if (linktext.left(1) != "/")
                linktext = finfo.dirPath(true) + "/" + finfo.readLink();

            QFile target(linktext);
            if (!(success1 = target.remove()))
            {
                VERBOSE(VB_IMPORTANT, QString("Error deleting '%1' -> '%2': %3")
                        .arg(filename).arg(linktext.local8Bit())
                        .arg(strerror(errno)));
            }
        }
    }
    if ((!checkexists || checkFile.exists()) &&
            !(success2 = checkFile.remove()))
    {
        VERBOSE(VB_IMPORTANT, QString("Error deleting '%1': %2")
                .arg(filename).arg(strerror(errno)));
    }
    return success1 && success2 ? 0 : -1;
}

};

QMutex MainServer::truncate_and_close_lock;

class ProcessRequestThread : public QThread
{
  public:
    ProcessRequestThread(MainServer *ms) { parent = ms; }
   
    void setup(MythSocket *sock)
    {
        lock.lock();
        socket = sock;
        socket->UpRef();
        waitCond.wakeOne();
        lock.unlock();
    }

    void killit(void)
    {
        lock.lock();
        threadlives = false;
        waitCond.wakeOne();
        lock.unlock();
    }

    virtual void run()
    {
        threadlives = true;

        lock.lock();

        // Signal back to the thread that created this one in case it is
        // waiting to find out that it is up and running.
        waitCond.wakeOne();

        while (1)
        {
            waitCond.wait(&lock);

            if (!threadlives)
                break;

            if (!socket)
                continue;

            parent->ProcessRequest(socket);
            socket->DownRef();
            socket = NULL;
            parent->MarkUnused(this);
        }

        lock.unlock();
    }

    QMutex lock;
    QWaitCondition waitCond;

  private:
    MainServer *parent;

    MythSocket *socket;

    bool threadlives;
};

MainServer::MainServer(bool master, int port, 
                       QMap<int, EncoderLink *> *tvList,
                       Scheduler *sched, AutoExpire *expirer)
{
    m_sched = sched;
    m_expirer = expirer;

    ismaster = master;
    masterServer = NULL;

    encoderList = tvList;
    AutoExpire::Update(true);

    for (int i = 0; i < PRT_STARTUP_THREAD_COUNT; i++)
    {
        ProcessRequestThread *prt = new ProcessRequestThread(this);
        prt->lock.lock();
        prt->start();
        prt->waitCond.wait(&prt->lock);
        prt->lock.unlock();
        threadPool.push_back(prt);
    }

    masterBackendOverride = gContext->GetNumSetting("MasterBackendOverride", 0);

    mythserver = new MythServer(port);
    if (!mythserver->ok())
    {
        VERBOSE(VB_IMPORTANT, QString("Failed to bind port %1. Exiting.")
                .arg(port));
        exit(BACKEND_BUGGY_EXIT_NO_BIND_MAIN);
    }

    connect(mythserver, SIGNAL(newConnect(MythSocket *)), 
            SLOT(newConnection(MythSocket *)));

    gContext->addListener(this);

    if (!ismaster)
    {
        masterServerReconnect = new QTimer(this);
        connect(masterServerReconnect, SIGNAL(timeout()), this, 
                SLOT(reconnectTimeout()));
        masterServerReconnect->start(1000, true);
    }

    deferredDeleteTimer = new QTimer(this);
    connect(deferredDeleteTimer, SIGNAL(timeout()), this,
            SLOT(deferredDeleteSlot()));
    deferredDeleteTimer->start(30 * 1000);

    autoexpireUpdateTimer = new QTimer(this);
    connect(autoexpireUpdateTimer, SIGNAL(timeout()), this,
            SLOT(autoexpireUpdate()));

    if (sched)
        sched->SetMainServer(this);
}

MainServer::~MainServer()
{
    delete mythserver;

    if (masterServerReconnect)
        delete masterServerReconnect;
    if (deferredDeleteTimer)
        delete deferredDeleteTimer;
}

void MainServer::autoexpireUpdate(void)
{
    AutoExpire::Update(false);
}

void MainServer::newConnection(MythSocket *socket)
{
    socket->setCallbacks(this);
}

void MainServer::readyRead(MythSocket *sock)
{
    PlaybackSock *testsock = getPlaybackBySock(sock);
    if (testsock && testsock->isExpectingReply())
    {
        return;
    }

    readReadyLock.lock();

    ProcessRequestThread *prt = NULL;
    threadPoolLock.lock();
    if (threadPool.empty())
    {
        VERBOSE(VB_IMPORTANT, "Waiting for a process request thread..");
        threadPoolCond.wait(&threadPoolLock, PRT_TIMEOUT);
    }
    if (!threadPool.empty())
    {
        prt = threadPool.back();
        threadPool.pop_back();
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "Adding a new process request thread");
        prt = new ProcessRequestThread(this);
        prt->lock.lock();
        prt->start();
        prt->waitCond.wait(&prt->lock);
        prt->lock.unlock();
    }
    threadPoolLock.unlock();

    prt->setup(sock);

    readReadyLock.unlock();
}

void MainServer::ProcessRequest(MythSocket *sock)
{
    sock->Lock();

    if (sock->bytesAvailable() > 0)
    {
        ProcessRequestWork(sock);
    }

    sock->Unlock();
}

void MainServer::ProcessRequestWork(MythSocket *sock)
{
    QStringList listline;
    if (!sock->readStringList(listline))
        return;

    QString line = listline[0];

    line = line.simplifyWhiteSpace();
    QStringList tokens = QStringList::split(" ", line);
    QString command = tokens[0];
    //cerr << "command='" << command << "'\n";
    if (command == "MYTH_PROTO_VERSION")
    {
        if (tokens.size() < 2)
            VERBOSE(VB_IMPORTANT, "Bad MYTH_PROTO_VERSION command");
        else
            HandleVersion(sock,tokens[1]);
        return;
    }
    else if (command == "ANN")
    {
        if (tokens.size() < 3 || tokens.size() > 5)
            VERBOSE(VB_IMPORTANT, "Bad ANN query");
        else
            HandleAnnounce(listline, tokens, sock);
        return;
    }
    else if (command == "DONE")
    {
        HandleDone(sock);
        return;
    }

    PlaybackSock *pbs = getPlaybackBySock(sock);
    if (!pbs)
    {
        VERBOSE(VB_IMPORTANT, "unknown socket");
        return;
    }

    // Increase refcount while using..
    pbs->UpRef();

    if (command == "QUERY_RECORDINGS")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_RECORDINGS query");
        else
            HandleQueryRecordings(tokens[1], pbs);
    }
    else if (command == "QUERY_RECORDING")
    {
        HandleQueryRecording(tokens, pbs);
    }
    else if (command == "QUERY_FREE_SPACE")
    {
        HandleQueryFreeSpace(pbs, false);
    }
    else if (command == "QUERY_FREE_SPACE_LIST")
    {
        HandleQueryFreeSpace(pbs, true);
    }
    else if (command == "QUERY_FREE_SPACE_SUMMARY")
    {
        HandleQueryFreeSpaceSummary(pbs);
    }
    else if (command == "QUERY_LOAD")
    {
        HandleQueryLoad(pbs);
    }
    else if (command == "QUERY_UPTIME")
    {
        HandleQueryUptime(pbs);
    }
    else if (command == "QUERY_MEMSTATS")
    {
        HandleQueryMemStats(pbs);
    }
    else if (command == "QUERY_CHECKFILE")
    {
        HandleQueryCheckFile(listline, pbs);
    }
    else if (command == "QUERY_GUIDEDATATHROUGH")
    {
        HandleQueryGuideDataThrough(pbs);
    }
    else if (command == "STOP_RECORDING")
    {
        HandleStopRecording(listline, pbs);
    }
    else if (command == "CHECK_RECORDING")
    {
        HandleCheckRecordingActive(listline, pbs);
    }
    else if (command == "DELETE_RECORDING")
    {
        HandleDeleteRecording(listline, pbs, false);
    }
    else if (command == "FORCE_DELETE_RECORDING")
    {
        HandleDeleteRecording(listline, pbs, true);
    }
    else if (command == "UNDELETE_RECORDING")
    {
        HandleUndeleteRecording(listline, pbs);
    }
    else if (command == "RESCHEDULE_RECORDINGS")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad RESCHEDULE_RECORDINGS request");
        else
            HandleRescheduleRecordings(tokens[1].toInt(), pbs);
    }
    else if (command == "FORGET_RECORDING")
    {
        HandleForgetRecording(listline, pbs);
    }
    else if (command == "QUERY_GETALLPENDING")
    {
        if (tokens.size() == 1)
            HandleGetPendingRecordings(pbs);
        else if (tokens.size() == 2)
            HandleGetPendingRecordings(pbs, tokens[1]);
        else
            HandleGetPendingRecordings(pbs, tokens[1], tokens[2].toInt());
    }
    else if (command == "QUERY_GETALLSCHEDULED")
    {
        HandleGetScheduledRecordings(pbs);
    }
    else if (command == "QUERY_GETCONFLICTING")
    {
        HandleGetConflictingRecordings(listline, pbs);
    }
    else if (command == "QUERY_GETEXPIRING")
    {
        HandleGetExpiringRecordings(pbs);
    }
    else if (command == "GET_FREE_RECORDER")
    {
        HandleGetFreeRecorder(pbs);
    }
    else if (command == "GET_FREE_RECORDER_COUNT")
    {
        HandleGetFreeRecorderCount(pbs);
    }
    else if (command == "GET_FREE_RECORDER_LIST")
    {
        HandleGetFreeRecorderList(pbs);
    }
    else if (command == "GET_NEXT_FREE_RECORDER")
    {
        HandleGetNextFreeRecorder(listline, pbs);
    }
    else if (command == "QUERY_RECORDER")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_RECORDER");
        else
            HandleRecorderQuery(listline, tokens, pbs);
    }
    else if (command == "SET_NEXT_LIVETV_DIR")
    {
        if (tokens.size() != 3)
            VERBOSE(VB_IMPORTANT, "Bad SET_NEXT_LIVETV_DIR");
        else
            HandleSetNextLiveTVDir(tokens, pbs);
    }
    else if (command == "SET_CHANNEL_INFO")
    {
        HandleSetChannelInfo(listline, pbs);
    }
    else if (command == "QUERY_REMOTEENCODER")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_REMOTEENCODER");
        else
            HandleRemoteEncoder(listline, tokens, pbs);
    }
    else if (command == "GET_RECORDER_FROM_NUM")
    {
        HandleGetRecorderFromNum(listline, pbs);
    }
    else if (command == "GET_RECORDER_NUM")
    {
        HandleGetRecorderNum(listline, pbs);
    }
    else if (command == "QUERY_FILETRANSFER")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_FILETRANSFER");
        else
            HandleFileTransferQuery(listline, tokens, pbs);
    }
    else if (command == "QUERY_GENPIXMAP")
    {
        HandleGenPreviewPixmap(listline, pbs);
    }
    else if (command == "QUERY_PIXMAP_LASTMODIFIED")
    {
        HandlePixmapLastModified(listline, pbs);
    }
    else if (command == "QUERY_ISRECORDING") 
    {
        HandleIsRecording(listline, pbs);
    }
    else if (command == "MESSAGE")
    {
        HandleMessage(listline, pbs);
    } 
    else if (command == "FILL_PROGRAM_INFO")
    {
        HandleFillProgramInfo(listline, pbs);
    }
    else if (command == "LOCK_TUNER")
    {
        HandleLockTuner(pbs);
    }
    else if (command == "FREE_TUNER")
    {
        if (tokens.size() != 2)
            VERBOSE(VB_IMPORTANT, "Bad FREE_TUNER query");
        else
            HandleFreeTuner(tokens[1].toInt(), pbs);
    }
    else if (command == "QUERY_IS_ACTIVE_BACKEND")
    {
        if (tokens.size() != 1)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_IS_ACTIVE_BACKEND");
        else
            HandleIsActiveBackendQuery(listline, pbs);
    }
    else if (command == "QUERY_COMMBREAK")
    {
        if (tokens.size() != 3)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_COMMBREAK");
        else
            HandleCommBreakQuery(tokens[1], tokens[2], pbs);
    }
    else if (command == "QUERY_CUTLIST")
    {
        if (tokens.size() != 3)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_CUTLIST");
        else
            HandleCutlistQuery(tokens[1], tokens[2], pbs);
    }
    else if (command == "QUERY_BOOKMARK")
    {
        if (tokens.size() != 3)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_BOOKMARK");
        else
            HandleBookmarkQuery(tokens[1], tokens[2], pbs);
    }
    else if (command == "SET_BOOKMARK")
    {
        if (tokens.size() != 5)
            VERBOSE(VB_IMPORTANT, "Bad SET_BOOKMARK");
        else
            HandleSetBookmark(tokens, pbs);
    }
    else if (command == "QUERY_SETTING")
    {
        if (tokens.size() != 3)
            VERBOSE(VB_IMPORTANT, "Bad QUERY_SETTING");
        else
            HandleSettingQuery(tokens, pbs);
    }
    else if (command == "SET_SETTING")
    {
        if (tokens.size() != 4)
            VERBOSE(VB_IMPORTANT, "Bad SET_SETTING");
        else
            HandleSetSetting(tokens, pbs);
    }
    else if (command == "ALLOW_SHUTDOWN")
    {
        if (tokens.size() != 1)
            VERBOSE(VB_IMPORTANT, "Bad ALLOW_SHUTDOWN");
        else
            HandleBlockShutdown(false, pbs);
    }
    else if (command == "BLOCK_SHUTDOWN")
    {
        if (tokens.size() != 1)
            VERBOSE(VB_IMPORTANT, "Bad BLOCK_SHUTDOWN");
        else
            HandleBlockShutdown(true, pbs);
    }
    else if (command == "SHUTDOWN_NOW")
    {
        if (tokens.size() != 1)
            VERBOSE(VB_IMPORTANT, "Bad SHUTDOWN_NOW query");
        else if (!ismaster)
        {
            QString halt_cmd = listline[1];
            if (!halt_cmd.isEmpty())
            {
                VERBOSE(VB_IMPORTANT, "Going down now as of Mainserver request!");
                system(halt_cmd.ascii());
            }
            else
                VERBOSE(VB_IMPORTANT,
                        "WARNING: Recieved an empty SHUTDOWN_NOW query!");
        }
    }
    else if (command == "BACKEND_MESSAGE")
    {
        QString message = listline[1];
        QStringList extra = listline[2];
        for (uint i = 3; i < listline.size(); i++)
            extra << listline[i];
        MythEvent me(message, extra);
        gContext->dispatch(me);
    }
    else if (command == "REFRESH_BACKEND")
    {
        VERBOSE(VB_IMPORTANT,"Reloading backend settings");
        HandleBackendRefresh(sock);
    }
    else if (command == "OK")
    {
        VERBOSE(VB_IMPORTANT, "Got 'OK' out of sequence.");
    }
    else if (command == "UNKNOWN_COMMAND")
    {
        VERBOSE(VB_IMPORTANT, "Got 'UNKNOWN_COMMAND' out of sequence.");
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "Unknown command: " + command);

        MythSocket *pbssock = pbs->getSocket();

        QStringList strlist;
        strlist << "UNKNOWN_COMMAND";
        
        SendResponse(pbssock, strlist);
    }

    // Decrease refcount..
    pbs->DownRef();
}

void MainServer::MarkUnused(ProcessRequestThread *prt)
{
    threadPoolLock.lock();
    threadPool.push_back(prt);
    threadPoolLock.unlock();
}

void MainServer::customEvent(QCustomEvent *e)
{
    QStringList broadcast;
    bool sendstuff = false;

    if ((MythEvent::Type)(e->type()) == MythEvent::MythEventMessage)
    {
        MythEvent *me = (MythEvent *)e;

        if (me->Message().left(11) == "AUTO_EXPIRE")
        {
            QStringList tokens = QStringList::split(" ", me->Message());
            if (tokens.size() != 3)
            {
                VERBOSE(VB_IMPORTANT, "Bad AUTO_EXPIRE message");
                return;
            }

            QDateTime startts = QDateTime::fromString(tokens[2], Qt::ISODate);
            ProgramInfo *pinfo = ProgramInfo::GetProgramFromRecorded(tokens[1],
                                                                     startts);
            if (pinfo)
            {
                // allow re-record if auto expired but not expired live 
                // or already "deleted" programs
                if (pinfo->recgroup != "LiveTV" &&
                    pinfo->recgroup != "Deleted" &&
                    (gContext->GetNumSetting("RerecordWatched", 0) ||
                     !(pinfo->getProgramFlags() & FL_WATCHED)))
                {
                    pinfo->ForgetHistory();
                }
                DoHandleDeleteRecording(pinfo, NULL, false, true);
            }
            else
            {
                cerr << "Cannot find program info for '" << me->Message()
                     << "', while attempting to Auto-Expire." << endl;
            }

            return;
        }

        if (me->Message().left(21) == "QUERY_NEXT_LIVETV_DIR" && m_sched)
        {
            QStringList tokens = QStringList::split(" ", me->Message());
            if (tokens.size() != 2)
            {
                VERBOSE(VB_IMPORTANT, QString("Bad %1 message").arg(tokens[0]));
                return;
            }

            m_sched->GetNextLiveTVDir(tokens[1].toInt());
            return;
        }

        if ((me->Message().left(16) == "DELETE_RECORDING") ||
            (me->Message().left(22) == "FORCE_DELETE_RECORDING"))
        {
            QStringList tokens = QStringList::split(" ", me->Message());
            if (tokens.size() != 3)
            {
                VERBOSE(VB_IMPORTANT, QString("Bad %1 message").arg(tokens[0]));
                return;
            }

            QDateTime startts = QDateTime::fromString(tokens[2], Qt::ISODate);
            ProgramInfo *pinfo = ProgramInfo::GetProgramFromRecorded(tokens[1],
                                                                     startts);
            if (pinfo)
            {
                if (tokens[0] == "FORCE_DELETE_RECORDING")
                    DoHandleDeleteRecording(pinfo, NULL, true);
                else
                    DoHandleDeleteRecording(pinfo, NULL, false);
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                    QString("Cannot find program info for '%1' while "
                            "attempting to delete.").arg(me->Message()));
            }

            return;
        }

        if (me->Message().left(21) == "RESCHEDULE_RECORDINGS" && m_sched)
        {
            QStringList tokens = QStringList::split(" ", me->Message());
            if (tokens.size() != 2)
            {
                VERBOSE(VB_IMPORTANT, "Bad RESCHEDULE_RECORDINGS message");
                return;
            }

            int recordid = tokens[1].toInt();
            m_sched->Reschedule(recordid);
            return;
        }

        if (me->Message().left(23) == "SCHEDULER_ADD_RECORDING" && m_sched)
        {
            ProgramInfo pi;
            QStringList list = me->ExtraDataList();
            if (!pi.FromStringList(list, 0))
            {
                VERBOSE(VB_IMPORTANT, "Bad SCHEDULER_ADD_RECORDING message");
                return;
            }

            m_sched->AddRecording(pi);
            return;
        }

        if (me->Message().left(23) == "UPDATE_RECORDING_STATUS" && m_sched)
        {
            QStringList tokens = QStringList::split(" ", me->Message());
            if (tokens.size() != 6)
            {
                VERBOSE(VB_IMPORTANT, "Bad UPDATE_RECORDING_STATUS message");
                return;
            }

            int cardid = tokens[1].toInt();
            QString chanid = tokens[2];
            QDateTime startts = QDateTime::fromString(tokens[3], Qt::ISODate);
            RecStatusType recstatus = RecStatusType(tokens[4].toInt());
            QDateTime recendts = QDateTime::fromString(tokens[5], Qt::ISODate);
            m_sched->UpdateRecStatus(cardid, chanid, startts, 
                                     recstatus, recendts);
            return;
        }

        if (me->Message().left(13) == "LIVETV_EXITED")
        {
            QString chainid = me->ExtraData();
            LiveTVChain *chain = GetExistingChain(chainid);
            if (chain)
                DeleteChain(chain);

            return;
        }

        if (me->Message() == "CLEAR_SETTINGS_CACHE")
            gContext->ClearSettingsCache();

        if (me->Message().left(14) == "RESET_IDLETIME" && m_sched)
            m_sched->ResetIdleTime();

        if (me->Message().left(6) == "LOCAL_")
            return;

        broadcast = "BACKEND_MESSAGE";
        broadcast << me->Message();
        broadcast += me->ExtraDataList();
        sendstuff = true;
    }

    if (sendstuff)
    {
        readReadyLock.lock();

        bool sendGlobal = false;
        if (ismaster && broadcast[1].left(7) == "GLOBAL_")
        {
            broadcast[1].replace(QRegExp("GLOBAL_"), "LOCAL_");
            MythEvent me(broadcast[1], broadcast[2]);
            gContext->dispatch(me);

            sendGlobal = true;
        }

        QPtrList<PlaybackSock> sentSet;

        // Make a local copy of the list, upping the refcount as we go..
        vector<PlaybackSock *> localPBSList;
        sockListLock.lock();
        vector<PlaybackSock *>::iterator iter = playbackList.begin();
        for (; iter != playbackList.end(); iter++)
        {
            PlaybackSock *pbs = (*iter);
            pbs->UpRef();
            localPBSList.push_back(pbs);
        }
        sockListLock.unlock();

        for (iter = localPBSList.begin(); iter != localPBSList.end(); iter++)
        {
            PlaybackSock *pbs = (*iter);

            if (sentSet.containsRef(pbs) || pbs->IsDisconnected())
                continue;

            sentSet.append(pbs);

            bool reallysendit = false;

            if (broadcast[1] == "CLEAR_SETTINGS_CACHE")
            {
                if ((ismaster) &&
                    (pbs->isSlaveBackend() || pbs->wantsEvents()))
                    reallysendit = true;
            }
            else if (sendGlobal)
            {
                if (pbs->isSlaveBackend())
                    reallysendit = true;
            }
            else if (pbs->wantsEvents())
            {
                reallysendit = true; 
            }

            MythSocket *sock = pbs->getSocket();
            sock->UpRef();

            if (reallysendit)
            {
                sock->Lock();
                sock->writeStringList(broadcast);
                sock->Unlock();
            }

            sock->DownRef();
        }

        // Done with the pbs list, so decrement all the instances..
        for (iter = localPBSList.begin(); iter != localPBSList.end(); iter++)
        {
            PlaybackSock *pbs = (*iter);
            pbs->DownRef();
        }

        readReadyLock.unlock();
    }
}

/**
 * \addtogroup myth_network_protocol
 * \par        MYTH_PROTO_VERSION \e version
 * Checks that \e version matches the backend's version.
 * If it matches, the stringlist of "ACCEPT" \e "version" is returned.
 * If it does not, "REJECT" \e "version" is returned,
 * and the socket is closed (for this client)
 */
void MainServer::HandleVersion(MythSocket *socket, QString version)
{
    QStringList retlist;
    if (version != MYTH_PROTO_VERSION)
    {
        VERBOSE(VB_GENERAL,
                "MainServer::HandleVersion - Client speaks protocol version "
                + version + " but we speak " + MYTH_PROTO_VERSION + "!");
        retlist << "REJECT" << MYTH_PROTO_VERSION;
        socket->writeStringList(retlist);
        HandleDone(socket);
        return;
    }

    retlist << "ACCEPT" << MYTH_PROTO_VERSION;
    socket->writeStringList(retlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        ANN Playback \e host \e wantevents
 * Register \e host as a client, and prevent shutdown of the socket.
 * 
 * \par        ANN Monitor  \e host \e wantevents
 * Register \e host as a client, and allow shutdown of the socket
 * \par        ANN SlaveBackend \e IPaddress
 * \par        ANN FileTransfer stringlist(\e hostname, \e filename)
 * \par        ANN FileTransfer stringlist(\e hostname, \e filename) \e useReadahead \e retries
 */
void MainServer::HandleAnnounce(QStringList &slist, QStringList commands, 
                                MythSocket *socket)
{
    QStringList retlist = "OK";

    sockListLock.lock();
    vector<PlaybackSock *>::iterator iter = playbackList.begin();
    for (; iter != playbackList.end(); iter++)
    {
        PlaybackSock *pbs = (*iter);
        if (pbs->getSocket() == socket)
        {
            sockListLock.unlock();
            VERBOSE(VB_IMPORTANT, QString("Client %1 is trying to announce a socket "
                                    "multiple times.")
                                    .arg(commands[2]));
            socket->writeStringList(retlist);
            return;
        }
    }
    sockListLock.unlock();

    if (commands[1] == "Playback" || commands[1] == "Monitor")
    {
        // Monitor connections are same as Playback but they don't
        // block shutdowns. See the Scheduler event loop for more.

        bool wantevents = commands[3].toInt();
        VERBOSE(VB_GENERAL, QString("MainServer::HandleAnnounce %1")
                                    .arg(commands[1]));
        VERBOSE(VB_IMPORTANT, QString("adding: %1 as a client (events: %2)")
                               .arg(commands[2]).arg(wantevents));
        PlaybackSock *pbs = new PlaybackSock(this, socket, commands[2], wantevents);
        pbs->setBlockShutdown(commands[1] == "Playback");
        sockListLock.lock();
        playbackList.push_back(pbs);
        sockListLock.unlock();
    }
    else if (commands[1] == "SlaveBackend")
    {
        VERBOSE(VB_IMPORTANT, QString("adding: %1 as a slave backend server")
                               .arg(commands[2]));
        PlaybackSock *pbs = new PlaybackSock(this, socket, commands[2], false);
        pbs->setAsSlaveBackend();
        pbs->setIP(commands[3]);

        if (m_sched)
        {
            ProgramInfo pinfo;
            ProgramList slavelist;
            QStringList::const_iterator sit = slist.at(1);
            while (sit != slist.end())
            {
                if (!pinfo.FromStringList(sit, slist.end()))
                    break;
                slavelist.append(new ProgramInfo(pinfo));
            }
            m_sched->SlaveConnected(slavelist);
        }

        QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
        for (; iter != encoderList->end(); ++iter)
        {
            EncoderLink *elink = iter.data();
            if (elink->GetHostName() == commands[2])
                elink->SetSocket(pbs);
        }

        if (m_sched)
            m_sched->Reschedule(0);

        QString message = QString("LOCAL_SLAVE_BACKEND_ONLINE %2")
                                  .arg(commands[2]);
        MythEvent me(message);
        gContext->dispatch(me);

        pbs->setBlockShutdown(false);

        sockListLock.lock();
        playbackList.push_back(pbs);
        sockListLock.unlock();

        autoexpireUpdateTimer->start(1000, true);
    }
    else if (commands[1] == "FileTransfer")
    {
        VERBOSE(VB_GENERAL, "MainServer::HandleAnnounce FileTransfer");
        VERBOSE(VB_IMPORTANT, QString("adding: %1 as a remote file transfer")
                               .arg(commands[2]));
        QUrl qurl = slist[1];
        QString filename = LocalFilePath(qurl);

        FileTransfer *ft = NULL;
        bool usereadahead = true;
        int retries = -1;
        if (commands.size() >= 5)
        {
            usereadahead = commands[3].toInt();
            retries = commands[4].toInt();
        }

        if (retries >= 0)
            ft = new FileTransfer(filename, socket, usereadahead, retries);
        else
            ft = new FileTransfer(filename, socket);
        
        sockListLock.lock();
        fileTransferList.push_back(ft);
        sockListLock.unlock();

        retlist << QString::number(socket->socket());
        ft->UpRef();
        encodeLongLong(retlist, ft->GetFileSize());
        ft->DownRef();
    }

    socket->writeStringList(retlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        DONE
 * Closes this client's socket.
 */
void MainServer::HandleDone(MythSocket *socket)
{
    socket->close();
}

void MainServer::SendResponse(MythSocket *socket, QStringList &commands)
{
    if (getPlaybackBySock(socket) || getFileTransferBySock(socket))
    {
        socket->writeStringList(commands);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "SendResponse: Unable to write to client socket,"
                " as it's no longer there");
    }
}

/**
 * \addingroup myth_network_protocol
 * \par        QUERY_RECORDINGS \e type
 * The \e type parameter can be either "Play", "Recording" or "Delete".
 * Returns programinfo (title, subtitle, description, category, chanid,
 * channum, callsign, channel.name, fileURL, \e et \e cetera)
 */
void MainServer::HandleQueryRecordings(QString type, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QString playbackhost = pbs->getHostname();

    QString fs_db_name = "";

    QDateTime rectime = QDateTime::currentDateTime().addSecs(
                            -gContext->GetNumSetting("RecordOverTime"));
    RecIter ri;
    RecList schedList;
    if (m_sched)
        m_sched->getAllPending(&schedList);

    QString ip = gContext->GetSetting("BackendServerIP");
    QString port = gContext->GetSetting("BackendServerPort");

    QMap<QString, int> inUseMap;
    QString inUseKey;
    QString inUseForWhat;
    QDateTime oneHourAgo = QDateTime::currentDateTime().addSecs(-61 * 60);

    MSqlQuery query(MSqlQuery::InitCon());

    query.prepare("SELECT DISTINCT chanid, starttime, recusage "
                  " FROM inuseprograms WHERE lastupdatetime >= :ONEHOURAGO ;");
    query.bindValue(":ONEHOURAGO", oneHourAgo);

    if (query.exec() && query.isActive() && query.size() > 0)
        while (query.next())
        {
            inUseKey = query.value(0).toString() + " " +
                       query.value(1).toDateTime().toString(Qt::ISODate);
            inUseForWhat = query.value(2).toString();

            if (!inUseMap.contains(inUseKey))
                inUseMap[inUseKey] = 0;

            if ((inUseForWhat == "player") ||
                (inUseForWhat == "preview player") ||
                (inUseForWhat == "PIP player"))
                inUseMap[inUseKey] = inUseMap[inUseKey] | FL_INUSEPLAYING;
            else if (inUseForWhat == "recorder")
                inUseMap[inUseKey] = inUseMap[inUseKey] | FL_INUSERECORDING;
        }


    QString thequery =
        "SELECT recorded.chanid,recorded.starttime,recorded.endtime,"
        "recorded.title,recorded.subtitle,recorded.description,"
        "recorded.hostname,channum,name,callsign,commflagged,cutlist,"
        "recorded.autoexpire,editing,bookmark,recorded.category,"
        "recorded.recgroup,record.dupin,record.dupmethod,"
        "recorded.recordid,channel.outputfilters,"
        "recorded.seriesid,recorded.programid,recorded.filesize, "
        "recorded.lastmodified, recorded.findid, "
        "recorded.originalairdate, recorded.playgroup, "
        "recorded.basename, recorded.progstart, "
        "recorded.progend, recorded.stars, "
        "recordedprogram.audioprop+0, recordedprogram.videoprop+0, "
        "recordedprogram.subtitletypes+0, transcoded, "
        "recorded.recpriority, watched, recorded.preserve, "
        "recorded.storagegroup "
        "FROM recorded "
        "LEFT JOIN record ON recorded.recordid = record.recordid "
        "LEFT JOIN channel ON recorded.chanid = channel.chanid "
        "LEFT JOIN recordedprogram ON "
        " ( recorded.chanid = recordedprogram.chanid AND "
        "  recorded.progstart = recordedprogram.starttime ) "
        "WHERE ( recorded.deletepending = 0 OR "
        "        DATE_ADD(recorded.lastmodified, INTERVAL 5 MINUTE) <= NOW() "
        "      ) ";

    if (type == "Recording")
        thequery += "AND recorded.endtime >= NOW() AND "
            "recorded.starttime <= NOW()";

    thequery += "ORDER BY recorded.starttime";

    if (type == "Delete")
        thequery += " DESC";

    QString chanorder = gContext->GetSetting("ChannelOrdering", "channum");
    if (chanorder != "channum")
        thequery += ", " + chanorder;
    else // approximation which the DB can handle
        thequery += ",atsc_major_chan,atsc_minor_chan,channum,callsign";

    QStringList outputlist;
    QString fileprefix = gContext->GetFilePrefix();
    QMap<QString, QString> backendIpMap;
    QMap<QString, QString> backendPortMap;

    query.prepare(thequery);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("ProgramList::FromRecorded", query);
        outputlist << "0";
    }
    else
    {
        outputlist << QString::number(query.size());

        while (query.next())
        {
            ProgramInfo *proginfo = new ProgramInfo;

            proginfo->chanid = query.value(0).toString();
            proginfo->startts = query.value(29).toDateTime();
            proginfo->endts = query.value(30).toDateTime();
            proginfo->recstartts = query.value(1).toDateTime();
            proginfo->recendts = query.value(2).toDateTime();
            proginfo->title = QString::fromUtf8(query.value(3).toString());
            proginfo->subtitle = QString::fromUtf8(query.value(4).toString());
            proginfo->description = QString::fromUtf8(query.value(5).toString());
            proginfo->hostname = query.value(6).toString();

            proginfo->dupin = RecordingDupInType(query.value(17).toInt());
            proginfo->dupmethod = RecordingDupMethodType(query.value(18).toInt());
            proginfo->recordid = query.value(19).toInt();
            proginfo->chanOutputFilters = query.value(20).toString();
            proginfo->seriesid = query.value(21).toString();
            proginfo->programid = query.value(22).toString();
            proginfo->filesize = stringToLongLong(query.value(23).toString());
            proginfo->lastmodified =
                      QDateTime::fromString(query.value(24).toString(),
                                            Qt::ISODate);
            proginfo->findid = query.value(25).toInt();

            if (query.value(26).isNull() || 
                query.value(26).toString().isEmpty())
            {
                proginfo->originalAirDate = QDate::QDate (0, 1, 1);
                proginfo->hasAirDate      = false;
            }
            else
            {
                proginfo->originalAirDate = 
                    QDate::fromString(query.value(26).toString(),Qt::ISODate);

                if (proginfo->originalAirDate > QDate(1940, 1, 1))
                    proginfo->hasAirDate  = true;
                else
                    proginfo->hasAirDate  = false;
            }

            proginfo->pathname = QString::fromUtf8(query.value(28).toString());

            if (proginfo->hostname.isEmpty() || proginfo->hostname.isNull())
                proginfo->hostname = gContext->GetHostName();

            if (!query.value(7).toString().isEmpty())
            {
                proginfo->chanstr = query.value(7).toString();
                proginfo->channame = QString::fromUtf8(query.value(8).toString());
                proginfo->chansign = QString::fromUtf8(query.value(9).toString());
            }
            else
            {
                proginfo->chanstr = "#" + proginfo->chanid;
                proginfo->channame = "#" + proginfo->chanid;
                proginfo->chansign = "#" + proginfo->chanid;
            }

            // Taken out of programinfo.cpp just to reduce the number of queries
            int flags = 0;
            flags |= (query.value(10).toInt() == 1) ? FL_COMMFLAG : 0;
            flags |= (query.value(11).toInt() == 1) ? FL_CUTLIST : 0;
            flags |= query.value(12).toInt() ? FL_AUTOEXP : 0;
            flags |= (query.value(14).toInt() == 1) ? FL_BOOKMARK : 0;
            flags |= (query.value(35).toInt() == TRANSCODING_COMPLETE) ?
                      FL_TRANSCODED : 0;
            flags |= (query.value(37).toInt() == 1) ? FL_WATCHED : 0;
            flags |= (query.value(38).toInt() == 1) ? FL_PRESERVED : 0;

            inUseKey = query.value(0).toString() + " " +
                       query.value(1).toDateTime().toString(Qt::ISODate);
            if (inUseMap.contains(inUseKey))
                flags |= inUseMap[inUseKey];

            if (query.value(13).toInt())
            {
                flags |= FL_EDITING;
            }
            else if (query.value(10).toInt() == COMM_FLAG_PROCESSING)
            {
                if (JobQueue::IsJobRunning(JOB_COMMFLAG, proginfo))
                    flags |= FL_EDITING;
                else
                    proginfo->SetCommFlagged(COMM_FLAG_NOT_FLAGGED);
            }

            proginfo->audioproperties = query.value(32).toInt();
            proginfo->videoproperties = query.value(33).toInt();
            proginfo->subtitleType = query.value(34).toInt();

            proginfo->programflags = flags;

            proginfo->category = QString::fromUtf8(query.value(15).toString());

            proginfo->recgroup = QString::fromUtf8(query.value(16).toString());
            proginfo->playgroup = QString::fromUtf8(query.value(27).toString());
            proginfo->storagegroup =
                QString::fromUtf8(query.value(39).toString());

            proginfo->recpriority = query.value(36).toInt();

            proginfo->recstatus = rsRecorded;
            if (proginfo->recendts > rectime)
            {
                for (ri = schedList.begin(); ri != schedList.end(); ri++)
                {
                    if ((*ri) && (*ri)->recstatus == rsRecording &&
                        proginfo->chanid == (*ri)->chanid &&
                        proginfo->recstartts == (*ri)->recstartts)
                    {
                        proginfo->recstatus = rsRecording;
                        break;
                    }
                }
            }

            proginfo->stars = query.value(31).toDouble();

            PlaybackSock *slave = NULL;

            if (proginfo->hostname != gContext->GetHostName())
                slave = getSlaveByHostname(proginfo->hostname);

            if (proginfo->hostname == gContext->GetHostName())
            {
                proginfo->pathname = QString("myth://") + ip + ":" + port
                                     + "/" + proginfo->pathname;
                if (proginfo->filesize == 0)
                {
                    QString tmpURL = GetPlaybackURL(proginfo);
                    QFile checkFile(tmpURL);
                    if (tmpURL != "" && checkFile.exists())
                    {
                        struct stat st;
                        if (stat(tmpURL.ascii(), &st) == 0)
                        {
                            proginfo->filesize = st.st_size;

                            if (proginfo->recendts < QDateTime::currentDateTime())
                                proginfo->SetFilesize(proginfo->filesize);
                        }
                    }
                }
            }
            else if (!slave)
            {
                proginfo->pathname = GetPlaybackURL(proginfo);
                if (proginfo->pathname == "")
                {
                    VERBOSE(VB_IMPORTANT,
                            "MainServer::HandleQueryRecordings()"
                            "\n\t\t\tCouldn't find backend for: " +
                            QString("\n\t\t\t%1 : \"%2\"")
                            .arg(proginfo->title).arg(proginfo->subtitle));

                    proginfo->filesize = 0;
                    proginfo->pathname = "file not found";
                }
            }
            else
            {
                if (proginfo->filesize == 0)
                {
                    slave->FillProgramInfo(proginfo, playbackhost);

                    if (proginfo->recendts < QDateTime::currentDateTime())
                        proginfo->SetFilesize(proginfo->filesize);
                }
                else
                {
                    ProgramInfo *p = proginfo;
                    if (!backendIpMap.contains(p->hostname))
                        backendIpMap[p->hostname] =
                            gContext->GetSettingOnHost("BackendServerIp",
                                                       p->hostname);
                    if (!backendPortMap.contains(p->hostname))
                        backendPortMap[p->hostname] =
                            gContext->GetSettingOnHost("BackendServerPort",
                                                       p->hostname);
                    p->pathname = QString("myth://") +
                                  backendIpMap[p->hostname] + ":" +
                                  backendPortMap[p->hostname] + "/" +
                                  p->pathname;
                }
            }

            if (slave)
                slave->DownRef();

            proginfo->ToStringList(outputlist);

            delete proginfo;
        }
    }

    for (ri = schedList.begin(); ri != schedList.end(); ri++)
        delete (*ri);

    SendResponse(pbssock, outputlist);
}

/**
 * \addingroup myth_network_protocol
 * \par        QUERY_RECORDING BASENAME \e basename
 * \par        QUERY_RECORDING TIMESLOT \e chanid \e starttime
 */
void MainServer::HandleQueryRecording(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QString command = slist[1].upper();
    ProgramInfo *pginfo = NULL;

    if (command == "BASENAME")
    {
        pginfo = ProgramInfo::GetProgramFromBasename(slist[2]);
    }
    else if (command == "TIMESLOT")
    {
        pginfo = ProgramInfo::GetProgramFromRecorded(slist[2], slist[3]);
    }

    QStringList strlist;

    if (pginfo)
    {
        strlist << "OK";
        pginfo->ToStringList(strlist);
        delete pginfo;
    }
    else
    {
        strlist << "ERROR";
    }

    SendResponse(pbssock, strlist);
}

void MainServer::HandleFillProgramInfo(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QString playbackhost = slist[1];

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 2);

    QString lpath = GetPlaybackURL(pginfo);
    QString ip = gContext->GetSetting("BackendServerIP");
    QString port = gContext->GetSetting("BackendServerPort");

    if (playbackhost == gContext->GetHostName())
        pginfo->pathname = lpath;
    else
        pginfo->pathname = QString("myth://") + ip + ":" + port
                           + "/" + pginfo->GetRecordBasename();

    struct stat st;

    long long size = 0;
    if (stat(lpath.ascii(), &st) == 0)
        size = st.st_size;

    pginfo->filesize = size;

    QStringList strlist;

    pginfo->ToStringList(strlist);

    delete pginfo;

    SendResponse(pbssock, strlist);
}

void *MainServer::SpawnDeleteThread(void *param)
{
    DeleteStruct *ds = (DeleteStruct *)param;

    MainServer *ms = ds->ms;
    ms->DoDeleteThread(ds);

    delete ds;

    return NULL;
}

void MainServer::DoDeleteThread(const DeleteStruct *ds)
{
    // sleep a little to let frontends reload the recordings list
    // after deleteing a recording, then we can hammer the DB and filesystem
    sleep(3);
    usleep(rand()%2000);

    deletelock.lock();

    QString logInfo = QString("chanid %1 at %2")
                              .arg(ds->chanid).arg(ds->recstartts.toString());
                             
    QString name = QString("deleteThread%1%2").arg(getpid()).arg(rand());
    QFile checkFile(ds->filename);

    if (!MSqlQuery::testDBConnection())
    {
        QString msg = QString("ERROR opening database connection for Delete "
                              "Thread for chanid %1 recorded at %2.  Program "
                              "will NOT be deleted.")
                              .arg(ds->chanid).arg(ds->recstartts.toString());
        VERBOSE(VB_GENERAL, msg);
        gContext->LogEntry("mythbackend", LP_ERROR, "Delete Recording",
                           QString("Unable to open database connection for %1. "
                                   "Program will NOT be deleted.")
                                   .arg(logInfo));

        deletelock.unlock();
        return;
    }

    ProgramInfo *pginfo;
    pginfo = ProgramInfo::GetProgramFromRecorded(ds->chanid,
                                                 ds->recstartts);
    if (pginfo == NULL)
    {
        QString msg = QString("ERROR retrieving program info when trying to "
                              "delete program for chanid %1 recorded at %2. "
                              "Recording will NOT be deleted.")
                              .arg(ds->chanid).arg(ds->recstartts.toString());
        VERBOSE(VB_GENERAL, msg);
        gContext->LogEntry("mythbackend", LP_ERROR, "Delete Recording",
                           QString("Unable to retrieve program info for %1. "
                                   "Program will NOT be deleted.")
                                   .arg(logInfo));

        deletelock.unlock();
        return;
    }

    // allow deleting files where the recording failed (ie, filesize == 0)
    if ((!checkFile.exists()) &&
        (pginfo->filesize > 0) &&
        (!ds->forceMetadataDelete))
    {
        VERBOSE(VB_IMPORTANT, QString("ERROR when trying to delete file: %1. File "
                                "doesn't exist.  Database metadata"
                                "will not be removed.")
                                .arg(ds->filename));
        gContext->LogEntry("mythbackend", LP_WARNING, "Delete Recording",
                           QString("File %1 does not exist for %2 when trying "
                                   "to delete recording.")
                                   .arg(ds->filename).arg(logInfo));

        pginfo->SetDeleteFlag(false);
        delete pginfo;

        MythEvent me("RECORDING_LIST_CHANGE");
        gContext->dispatch(me);

        deletelock.unlock();
        return;
    }

    JobQueue::DeleteAllJobs(ds->chanid, ds->recstartts);

    LiveTVChain *tvchain = GetChainWithRecording(pginfo);
    if (tvchain)
        tvchain->DeleteProgram(pginfo);

    bool followLinks = gContext->GetNumSetting("DeletesFollowLinks", 0);
    bool slowDeletes = gContext->GetNumSetting("TruncateDeletesSlowly", 0);
    int fd = -1;
    off_t size = 0;
    bool errmsg = false;

    /* Delete recording. */
    if (slowDeletes)
    {
        // Since stat fails after unlinking on some filesystems,
        // get the filesize first
        struct stat st;
        if (stat(ds->filename.ascii(), &st) == 0)
            size = st.st_size;
        fd = DeleteFile(ds->filename, followLinks);

        if ((fd < 0) && checkFile.exists())
            errmsg = true;
    }
    else
    {
        delete_file_immediately(ds->filename, followLinks, false);
        sleep(2);
        if (checkFile.exists())
            errmsg = true;
    }

    if (errmsg)
    {
        VERBOSE(VB_IMPORTANT,
            QString("Error deleting file: %1. Keeping metadata in database.")
                    .arg(ds->filename));
        gContext->LogEntry("mythbackend", LP_WARNING, "Delete Recording",
                           QString("File %1 for %2 could not be deleted.")
                                   .arg(ds->filename).arg(logInfo));

        pginfo->SetDeleteFlag(false);
        delete pginfo;

        MythEvent me("RECORDING_LIST_CHANGE");
        gContext->dispatch(me);

        deletelock.unlock();
        return;
    }

    /* Delete all preview thumbnails. */

    QFileInfo fInfo( ds->filename );
    QString nameFilter = fInfo.fileName() + "*.png";
    // QDir's nameFilter uses spaces or semicolons to separate globs,
    // so replace them with the "match any character" wildcard
    // since mythrename.pl may have included them in filenames
    nameFilter.replace(QRegExp("( |;)"), "?");
    QDir      dir  ( fInfo.dirPath(), nameFilter );

    for (uint nIdx = 0; nIdx < dir.count(); nIdx++)
    {
        QString sFileName = QString( "%1/%2" )
                               .arg( fInfo.dirPath() )
                               .arg( dir[ nIdx ] );

        delete_file_immediately( sFileName, followLinks, true);
    }

    DoDeleteInDB(ds);

    if (pginfo->recgroup != "LiveTV")
        ScheduledRecording::signalChange(0);

    deletelock.unlock();

    if (slowDeletes && fd != -1)
        TruncateAndClose(pginfo, fd, ds->filename, size);

    delete pginfo;
}

void MainServer::DoDeleteInDB(const DeleteStruct *ds)
{
    QString logInfo = QString("chanid %1 at %2")
        .arg(ds->chanid).arg(ds->recstartts.toString());

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("DELETE FROM recorded WHERE chanid = :CHANID AND "
                  "title = :TITLE AND starttime = :STARTTIME;");
    query.bindValue(":CHANID", ds->chanid);
    query.bindValue(":TITLE", ds->title.utf8());
    query.bindValue(":STARTTIME", ds->recstartts);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("Recorded program deletion", query);
        gContext->LogEntry("mythbackend", LP_ERROR, "Delete Recording",
                           QString("Error deleting recorded table for %1.")
                                   .arg(logInfo));
    }

    sleep(1);

    // Notify the frontend so it can requery for Free Space
    QString msg = QString("RECORDING_LIST_CHANGE DELETE %1 %2")
                          .arg(ds->chanid)
                          .arg(ds->recstartts.toString(Qt::ISODate));
    MythEvent me(msg);
    gContext->dispatch(me);

    // sleep a little to let frontends reload the recordings list
    sleep(3);

    query.prepare("DELETE FROM recordedmarkup "
                  "WHERE chanid = :CHANID AND starttime = :STARTTIME;");
    query.bindValue(":CHANID", ds->chanid);
    query.bindValue(":STARTTIME", ds->recstartts);
    query.exec();

    if (!query.isActive())
    {
        MythContext::DBError("Recorded program delete recordedmarkup",
                             query);
        gContext->LogEntry("mythbackend", LP_ERROR, "Delete Recording",
                           QString("Error deleting recordedmarkup for %1.")
                                   .arg(logInfo));
    }

    query.prepare("DELETE FROM recordedseek "
                  "WHERE chanid = :CHANID AND starttime = :STARTTIME;");
    query.bindValue(":CHANID", ds->chanid);
    query.bindValue(":STARTTIME", ds->recstartts);
    query.exec();

    if (!query.isActive())
    {
        MythContext::DBError("Recorded program delete recordedseek",
                             query);
        gContext->LogEntry("mythbackend", LP_ERROR, "Delete Recording",
                           QString("Error deleting recordedseek for %1.")
                                   .arg(logInfo));
    }
}

/**
 *  \brief Deletes links and unlinks the main file and returns the descriptor.
 *
 *  This is meant to be used with TruncateAndClose() to slowly shrink a
 *  large file and then eventually delete the file by closing the file
 *  descriptor.
 *
 *  \return fd for success, negative number for error.
 */
int MainServer::DeleteFile(const QString &filename, bool followLinks)
{
    QFileInfo finfo(filename);
    int fd = -1, err = 0;
    QString linktext = "";

    VERBOSE(VB_FILE, QString("About to unlink/delete file: '%1'")
            .arg(filename));

    QString errmsg = QString("Delete Error '%1'").arg(filename.local8Bit());
    if (finfo.isSymLink())
    {
        linktext = finfo.readLink();
        if (linktext.left(1) != "/")
            linktext = finfo.dirPath(true) + "/" + finfo.readLink();

        errmsg += QString(" -> '%2'").arg(linktext.local8Bit());
    }

    if (followLinks && finfo.isSymLink())
    {
        fd = OpenAndUnlink(linktext);
        if (fd >= 0)
            err = unlink(filename.local8Bit());
    }
    else if (!finfo.isSymLink())
    {
        fd = OpenAndUnlink(filename);
    }
    else // just delete symlinks immediately
    {
        err = unlink(filename.local8Bit());
        if (err == 0)
            return -1; // no error
    }

    if (fd < 0)
        VERBOSE(VB_IMPORTANT, errmsg + ENO);

    return fd;
}

/** \fn MainServer::OpenAndUnlink(const QString&)
 *  \brief Opens a file, unlinks it and returns the file descriptor.
 *
 *  This is used by DeleteFile(const QString&,bool) to delete recordings.
 *  In order to actually delete the file from the filesystem the user of
 *  this function must close the return file descriptor.
 *
 *  \return fd for success, negative number for error.
 */
int MainServer::OpenAndUnlink(const QString &filename)
{
    QString msg = QString("Error deleting '%1'").arg(filename.local8Bit());
    int fd = open(filename.local8Bit(), O_WRONLY);

    if (fd == -1)
    {
        VERBOSE(VB_IMPORTANT, msg + " could not open " + ENO);
        return -1;
    }
    
    if (unlink(filename.local8Bit()))
    {
        VERBOSE(VB_IMPORTANT, msg + " could not unlink " + ENO);
        close(fd);
        return -1;
    }

    return fd;
}

/**
 *  \brief Repeatedly truncate an open file in small increments.
 *
 *   When the file is small enough this closes the file and returns.
 *
 *   NOTE: This aquires a lock so that only one instance of TruncateAndClose()
 *         is running at a time.
 */
bool MainServer::TruncateAndClose(ProgramInfo *pginfo, int fd,
                                  const QString &filename, off_t fsize)
{
    QMutexLocker locker(&truncate_and_close_lock);

    pginfo->pathname = filename;
    pginfo->MarkAsInUse(true, "truncatingdelete");

    int cards = 5;

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT COUNT(cardid) FROM capturecard;");
    if (query.exec() && query.isActive() && query.size() && query.next())
        cards = query.value(0).toInt();

    // Time between truncation steps in milliseconds
    const size_t sleep_time = 500;
    const size_t min_tps    = (size_t) (cards * 1.2 * (19400000LL / 8));
    const size_t increment  = (size_t) (min_tps * (sleep_time * 0.001f));

    // Save this for mythtranscode's use
    gContext->SaveSetting("TruncateIncrement", increment);

    VERBOSE(VB_FILE,
            QString("Truncating '%1' by %2 MB every %3 milliseconds")
            .arg(filename)
            .arg(increment / (1024.0 * 1024.0), 0, 'f', 2)
            .arg(sleep_time));

    int count = 0;
    while (fsize > 0)
    {
        //VERBOSE(VB_FILE, QString("Truncating '%1' to %2 MB")
        //        .arg(filename).arg(fsize / (1024.0 * 1024.0), 0, 'f', 2));

        int err = ftruncate(fd, fsize);
        if (err)
        {
            VERBOSE(VB_IMPORTANT, QString("Error truncating '%1'")
                    .arg(filename) + ENO);
            pginfo->MarkAsInUse(false);
            return 0 == close(fd);
        }

        fsize -= increment;

        if ((count % 100) == 0)
            pginfo->UpdateInUseMark(true);

        count++;

        usleep(sleep_time * 1000);
    }

    bool ok = (0 == close(fd));

    pginfo->MarkAsInUse(false);

    VERBOSE(VB_FILE, QString("Finished truncating '%1'").arg(filename));

    return ok;
}

void MainServer::HandleCheckRecordingActive(QStringList &slist, 
                                            PlaybackSock *pbs)
{
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    int result = 0;

    if (ismaster && pginfo->hostname != gContext->GetHostName())
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);
        if (slave)
        {
            result = slave->CheckRecordingActive(pginfo);
            slave->DownRef();
        }
    }
    else
    {
        QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
        for (; iter != encoderList->end(); ++iter)
        {
            EncoderLink *elink = iter.data();

            if (elink->IsLocal() && elink->MatchesRecording(pginfo))
                result = iter.key();
        }
    }

    QStringList outputlist = QString::number(result);
    if (pbssock)
        SendResponse(pbssock, outputlist);

    delete pginfo;
    return;
}

void MainServer::HandleStopRecording(QStringList &slist, PlaybackSock *pbs)
{
    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    DoHandleStopRecording(pginfo, pbs);
}

void MainServer::DoHandleStopRecording(ProgramInfo *pginfo, PlaybackSock *pbs)
{
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    if (ismaster && pginfo->hostname != gContext->GetHostName())
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);

        int num = -1;

        if (slave)
        {
            num = slave->StopRecording(pginfo);

            if (num > 0)
            {
                (*encoderList)[num]->StopRecording();
                pginfo->recstatus = rsRecorded;
                if (m_sched)
                    m_sched->UpdateRecStatus(pginfo);
            }
            if (pbssock)
            {
                QStringList outputlist = "0";
                SendResponse(pbssock, outputlist);
            }
 
            delete pginfo;
            slave->DownRef();
            return;
        }
        else
        {
            // If the slave is unreachable, we can assume that the 
            // recording has stopped and the status should be updated.
            // Continue so that the master can try to update the endtime
            // of the file is in a shared directory.
            pginfo->recstatus = rsRecorded;
            if (m_sched)
                m_sched->UpdateRecStatus(pginfo);
        }

    }

    int recnum = -1;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (elink->IsLocal() && elink->MatchesRecording(pginfo))
        {
            recnum = iter.key();

            elink->StopRecording();

            while (elink->IsBusyRecording() ||
                   elink->GetState() == kState_ChangingState)
            {
                usleep(100);
            }

            if (ismaster)
            {
                pginfo->recstatus = rsRecorded;
                if (m_sched)
                    m_sched->UpdateRecStatus(pginfo);
            }
        }
    }

    if (pbssock)
    {
        QStringList outputlist = QString::number(recnum);
        SendResponse(pbssock, outputlist);
    }

    delete pginfo;
}

void MainServer::HandleDeleteRecording(QStringList &slist, PlaybackSock *pbs,
                                       bool forceMetadataDelete)
{
    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    DoHandleDeleteRecording(pginfo, pbs, forceMetadataDelete);
}

void MainServer::DoHandleDeleteRecording(ProgramInfo *pginfo, PlaybackSock *pbs,
                                         bool forceMetadataDelete, bool expirer)
{
    int resultCode = -1;
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    bool justexpire = expirer ? false :
            (gContext->GetNumSetting("AutoExpireInsteadOfDelete") &&
            (pginfo->recgroup != "Deleted") && (pginfo->recgroup != "LiveTV"));

    
    if (justexpire && !forceMetadataDelete && pginfo->filesize > (1024 * 1024) )
    {
        pginfo->ApplyRecordRecGroupChange("Deleted");
        pginfo->SetAutoExpire(kDeletedAutoExpire);
        pginfo->UpdateLastDelete(true);
        if (pginfo->recstatus == rsRecording)
            DoHandleStopRecording(pginfo, NULL);
        else
            delete pginfo;
        QStringList outputlist = QString::number(0);
        SendResponse(pbssock, outputlist);
        MythEvent me("RECORDING_LIST_CHANGE");
        gContext->dispatch(me);
        return;
    }

    QString filename = GetPlaybackURL(pginfo, false);

    // If this recording was made by a another recorder, and that
    // recorder is available, tell it to do the deletion.
    if (ismaster && pginfo->hostname != gContext->GetHostName())
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);

        int num = -1;

        if (slave) 
        {
            num = slave->DeleteRecording(pginfo, forceMetadataDelete);

            if (num > 0)
            {
                (*encoderList)[num]->StopRecording();
                pginfo->recstatus = rsRecorded;
                if (m_sched)
                    m_sched->UpdateRecStatus(pginfo);
            }

            if (pbssock)
            {
                QStringList outputlist = QString::number(num);
                SendResponse(pbssock, outputlist);
            }

            delete pginfo;
            slave->DownRef();
            return;
        }
    }

    // Tell all encoders to stop recordering to the file being deleted.
    // Hopefully this is never triggered.

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (elink->IsLocal() && elink->MatchesRecording(pginfo))
        {
            resultCode = iter.key();

            elink->StopRecording();

            while (elink->IsBusyRecording() || 
                   elink->GetState() == kState_ChangingState)
            {
                usleep(100);
            }

            if (ismaster)
            {
                pginfo->recstatus = rsRecorded;
                if (m_sched)
                    m_sched->UpdateRecStatus(pginfo);
            }
        }
    }

    QFile checkFile(filename);
    bool fileExists = checkFile.exists();
    if (!fileExists) 
    {
        QFile checkFileUTF8(QString::fromUtf8(filename));
        if (fileExists = checkFileUTF8.exists())
            filename = QString::fromUtf8(filename);
    }

    // Allow deleting of files where the recording failed meaning size == 0
    // But do not allow deleting of files that appear to be completely absent.
    // The latter condition indicates the filesystem containing the file is
    // most likely absent and deleting the file metadata is unsafe.
    if ((fileExists) || (pginfo->filesize == 0) || (forceMetadataDelete))
    {
        DeleteStruct *ds = new DeleteStruct;
        ds->ms = this;
        ds->filename = filename;
        ds->title = pginfo->title;
        ds->chanid = pginfo->chanid;
        ds->recstartts = pginfo->recstartts;
        ds->recendts = pginfo->recendts;
        ds->forceMetadataDelete = forceMetadataDelete;

        pginfo->SetDeleteFlag(true);

        pthread_t deleteThread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&deleteThread, &attr, SpawnDeleteThread, ds);
        pthread_attr_destroy(&attr);
    }
    else
    {
        QString logInfo = QString("chanid %1 at %2")
                              .arg(pginfo->chanid)
                              .arg(pginfo->recstartts.toString());
        VERBOSE(VB_IMPORTANT,
                QString("ERROR when trying to delete file: %1. File doesn't "
                        "exist.  Database metadata will not be removed.")
                        .arg(filename));
        gContext->LogEntry("mythbackend", LP_WARNING, "Delete Recording",
                           QString("File %1 does not exist for %2 when trying "
                                   "to delete recording.")
                                   .arg(filename).arg(logInfo));
        resultCode = -2;
    }

    if (pbssock)
    {
        QStringList outputlist = QString::number(resultCode);
        SendResponse(pbssock, outputlist);
    }

    // Tell MythTV frontends that the recording list needs to be updated.
    if ((fileExists) || (pginfo->filesize == 0) || (forceMetadataDelete))
    {
        QString msg = QString("RECORDING_LIST_CHANGE DELETE %1 %2")
                              .arg(pginfo->chanid)
                              .arg(pginfo->recstartts.toString(Qt::ISODate));
        MythEvent me(msg);
        gContext->dispatch(me);
    }

    delete pginfo;
}

void MainServer::HandleUndeleteRecording(QStringList &slist, PlaybackSock *pbs)
{
    ProgramInfo *pginfo  = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    DoHandleUndeleteRecording(pginfo, pbs);
}

void MainServer::DoHandleUndeleteRecording(ProgramInfo *pginfo, PlaybackSock *pbs)
{
    bool ret = -1;
    bool undelete_possible = 
            gContext->GetNumSetting("AutoExpireInsteadOfDelete", 0);
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    if (undelete_possible)
    {
        pginfo->ApplyRecordRecGroupChange("Default");
        pginfo->UpdateLastDelete(false);
        pginfo->SetAutoExpire(kDisableAutoExpire);
        delete pginfo;
        ret = 0;
        MythEvent me("RECORDING_LIST_CHANGE");
        gContext->dispatch(me);
    }

    QStringList outputlist = QString::number(ret);
    SendResponse(pbssock, outputlist);
}

void MainServer::HandleRescheduleRecordings(int recordid, PlaybackSock *pbs)
{
    QStringList result;
    if (m_sched)
    {
        m_sched->Reschedule(recordid);
        result = QString::number(1);
    }
    else
        result = QString::number(0);

    if (pbs)
    {
        MythSocket *pbssock = pbs->getSocket();
        if (pbssock)
            SendResponse(pbssock, result);
    }
}

void MainServer::HandleForgetRecording(QStringList &slist, PlaybackSock *pbs)
{
    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    pginfo->ForgetHistory();

    if (pbssock)
    {
        QStringList outputlist = QString::number(0);
        SendResponse(pbssock, outputlist);
    }

    delete pginfo;

}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_FREE_SPACE
 * Returns the free space on this backend, as a list of hostname, directory,
 * 1, -1, total size, used (both in K and 64bit, so two 32bit numbers each).
 * \par        QUERY_FREE_SPACE_LIST
 * Returns the free space on \e all hosts. (each host as above,
 * except that the directory becomes a URL, and a TotalDiskSpace is appended)
 */
void MainServer::HandleQueryFreeSpace(PlaybackSock *pbs, bool allHosts)
{    
    QStringList strlist;

    BackendQueryDiskSpace(strlist, encoderList, allHosts, allHosts);

    SendResponse(pbs->getSocket(), strlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_FREE_SPACE_SUMMARY
 * Summarises the free space on this backend, as list of total size, used
 */
void MainServer::HandleQueryFreeSpaceSummary(PlaybackSock *pbs)
{    
    QStringList fullStrList;
    QStringList strList;

    BackendQueryDiskSpace(fullStrList, encoderList, true, true);

    // The TotalKB and UsedKB are the last two numbers encoded in the list
    unsigned int index = fullStrList.size() - 4;
    strList << fullStrList[index++];
    strList << fullStrList[index++];
    strList << fullStrList[index++];
    strList << fullStrList[index++];

    SendResponse(pbs->getSocket(), strList);
}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_LOAD
 * Returns the Unix load on this backend
 * (three floats - the average over 1, 5 and 15 mins).
 */
void MainServer::HandleQueryLoad(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strlist;

    double loads[3];
    if (getloadavg(loads,3) == -1)
        strlist << "getloadavg() failed";
    else
        strlist << QString::number(loads[0])
                << QString::number(loads[1])
                << QString::number(loads[2]);

    SendResponse(pbssock, strlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_UPTIME
 * Returns the number of seconds this backend's host has been running
 */
void MainServer::HandleQueryUptime(PlaybackSock *pbs)
{
    MythSocket    *pbssock = pbs->getSocket();
    QStringList strlist;
    time_t      uptime;

    if (getUptime(uptime))
        strlist << QString::number(uptime);
    else
        strlist << "Could not determine uptime.";

    SendResponse(pbssock, strlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_MEMSTATS
 * Returns total RAM, free RAM, total VM and free VM (all in MB)
 */
void MainServer::HandleQueryMemStats(PlaybackSock *pbs)
{
    MythSocket    *pbssock = pbs->getSocket();
    QStringList strlist;
    int         totalMB, freeMB, totalVM, freeVM;

    if (getMemStats(totalMB, freeMB, totalVM, freeVM))
        strlist << QString::number(totalMB) << QString::number(freeMB)
                << QString::number(totalVM) << QString::number(freeVM);
    else
        strlist << "Could not determine memory stats.";

    SendResponse(pbssock, strlist);
}

/**
 * \addtogroup myth_network_protocol
 * \par        QUERY_CHECKFILE \e checkslaves \e programinfo
 */
void MainServer::HandleQueryCheckFile(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    bool checkSlaves = slist[1].toInt();

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 2);

    int exists = 0;

    if ((ismaster) &&
        (pginfo->hostname != gContext->GetHostName()) &&
        (checkSlaves))
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);

        if (slave) 
        {
             exists = slave->CheckFile(pginfo);
             slave->DownRef();

             QStringList outputlist = QString::number(exists);
             if (exists)
                 outputlist << pginfo->pathname;
             else
                 outputlist << "";

             SendResponse(pbssock, outputlist);
             delete pginfo;
             return;
        }
    }

    QString pburl = GetPlaybackURL(pginfo);
    QFile checkFile(pburl);

    if (checkFile.exists() == true)
        exists = 1;

    QStringList strlist = QString::number(exists);
    if (exists)
        strlist << pburl;
    else
        strlist << "";
    SendResponse(pbssock, strlist);

    delete pginfo;
}

void MainServer::getGuideDataThrough(QDateTime &GuideDataThrough)
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT MAX(endtime) FROM program WHERE manualid = 0;");

    if (query.exec() && query.isActive() && query.size())
    {
        query.next();
        if (query.isValid())
            GuideDataThrough = QDateTime::fromString(query.value(0).toString(),
                                                     Qt::ISODate);
    }
}

void MainServer::HandleQueryGuideDataThrough(PlaybackSock *pbs)
{
    QDateTime GuideDataThrough;
    MythSocket *pbssock = pbs->getSocket();
    QStringList strlist;

    getGuideDataThrough(GuideDataThrough);

    if (GuideDataThrough.isNull())
        strlist << QString("0000-00-00 00:00");
    else
        strlist << QDateTime(GuideDataThrough).toString("yyyy-MM-dd hh:mm");

    SendResponse(pbssock, strlist);
}

void MainServer::HandleGetPendingRecordings(PlaybackSock *pbs, 
                                            QString tmptable, int recordid)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strList;

    if (m_sched)
    {
        if (tmptable == "")
            m_sched->getAllPending(strList);
        else
        {
            Scheduler *sched = new Scheduler(false, encoderList, 
                                             tmptable, m_sched);
            sched->FillRecordListFromDB(recordid);
            sched->getAllPending(strList);
            delete sched;

            if (recordid > 0)
            {
                MSqlQuery query(MSqlQuery::InitCon());
                query.prepare("SELECT NULL FROM record "
                              "WHERE recordid = :RECID;");
                query.bindValue(":RECID", recordid);

                if (query.exec() && query.isActive() && query.size())
                {
                    ScheduledRecording *record = new ScheduledRecording();
                    record->loadByID(recordid);
                    if (record->getSearchType() == kManualSearch)
                        HandleRescheduleRecordings(recordid, NULL);
                    record->deleteLater();
                }
                query.prepare("DELETE FROM program WHERE manualid = :RECID;");
                query.bindValue(":RECID", recordid);
                query.exec();
            }
        }
    }
    else
    {
        strList << QString::number(0);
        strList << QString::number(0);
    }

    SendResponse(pbssock, strList);
}

void MainServer::HandleGetScheduledRecordings(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strList;

    if (m_sched)
        m_sched->getAllScheduled(strList);
    else
        strList << QString::number(0);

    SendResponse(pbssock, strList);
}

void MainServer::HandleGetConflictingRecordings(QStringList &slist,
                                                PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    QStringList strlist;

    if (m_sched)
        m_sched->getConflicting(pginfo, strlist);
    else
        strlist << QString::number(0);

    SendResponse(pbssock, strlist);

    delete pginfo;
}

void MainServer::HandleGetExpiringRecordings(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strList;

    if (m_expirer)
        m_expirer->GetAllExpiring(strList);
    else
        strList << QString::number(0);

    SendResponse(pbssock, strList);
}

void MainServer::HandleLockTuner(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QString pbshost = pbs->getHostname();

    QStringList strlist;
    int retval;
    
    EncoderLink *encoder = NULL;
    QString enchost;
    
    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (elink->IsLocal())
            enchost = gContext->GetHostName();
        else
            enchost = elink->GetHostName();

        if ((enchost == pbshost) &&
            (elink->IsConnected()) &&
            (!elink->IsBusy()) &&
            (!elink->IsTunerLocked()))
        {
            encoder = elink;
            break;
        }
    }
    
    if (encoder)
    {
        retval = encoder->LockTuner();

        if (retval != -1)
        {
            QString msg = QString("Cardid %1 LOCKed for external use on %2.")
                                  .arg(retval).arg(pbshost);
            VERBOSE(VB_GENERAL, msg);

            MSqlQuery query(MSqlQuery::InitCon());
            query.prepare("SELECT videodevice, audiodevice, "
                          "vbidevice "
                          "FROM capturecard "
                          "WHERE cardid = :CARDID ;");
            query.bindValue(":CARDID", retval);

            if (query.exec() && query.isActive() && query.size())
            {
                // Success
                query.next();
                strlist << QString::number(retval)
                        << query.value(0).toString()
                        << query.value(1).toString()
                        << query.value(2).toString();

                if (m_sched)
                    m_sched->Reschedule(0);

                SendResponse(pbssock, strlist);
                return;
            }
            else
            {
                cerr << "mainserver.o: Failed querying the db for a videodevice"
                     << endl;
            }
        }
        else
        {
            // Tuner already locked
            strlist << "-2" << "" << "" << "";
            SendResponse(pbssock, strlist);
            return;
        }
    }

    strlist << "-1" << "" << "" << "";
    SendResponse(pbssock, strlist);
}

void MainServer::HandleFreeTuner(int cardid, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QStringList strlist;
    EncoderLink *encoder = NULL;
    
    QMap<int, EncoderLink *>::Iterator iter = encoderList->find(cardid);
    if (iter == encoderList->end())
    {
        VERBOSE(VB_IMPORTANT, "MainServer::HandleFreeTuner() " +
                QString("Unknown encoder: %1").arg(cardid));
        strlist << "FAILED";
    }
    else
    {
        encoder = iter.data();
        encoder->FreeTuner();
        
        QString msg = QString("Cardid %1 FREED from external use on %2.")
                              .arg(cardid).arg(pbs->getHostname());
        VERBOSE(VB_GENERAL, msg);

        if (m_sched)
            m_sched->Reschedule(0);
        
        strlist << "OK";
    }
    
    SendResponse(pbssock, strlist);
}

void MainServer::HandleGetFreeRecorder(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QString pbshost = pbs->getHostname();

    QStringList strlist;
    int retval = -1;

    EncoderLink *encoder = NULL;
    QString enchost;

    bool lastcard = false;

    if (gContext->GetSetting("LastFreeCard", "0") == "1")
        lastcard = true;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (!lastcard)
        {
            if (elink->IsLocal())
                enchost = gContext->GetHostName();
            else
                enchost = elink->GetHostName();

            if (enchost == pbshost && elink->IsConnected() &&
                !elink->IsBusy() && !elink->IsTunerLocked())
            {
                encoder = elink;
                retval = iter.key();
                VERBOSE(VB_RECORD, QString("Card %1 is local.")
                        .arg(iter.key()));
                break;
            }
        }

        if ((retval == -1 || lastcard) && elink->IsConnected() &&
            !elink->IsBusy() && !elink->IsTunerLocked())
        {
            encoder = elink;
            retval = iter.key();
        }
        VERBOSE(VB_RECORD, QString("Checking card %1. Best card so far %2")
                .arg(iter.key()).arg(retval));
    }

    strlist << QString::number(retval);
        
    if (encoder)
    {
        if (encoder->IsLocal())
        {
            strlist << gContext->GetSetting("BackendServerIP");
            strlist << gContext->GetSetting("BackendServerPort");
        }
        else
        {
            strlist << gContext->GetSettingOnHost("BackendServerIP", 
                                                  encoder->GetHostName(),
                                                  "nohostname");
            strlist << gContext->GetSettingOnHost("BackendServerPort", 
                                                  encoder->GetHostName(), "-1");
        }
    }
    else
    {
        strlist << "nohost";
        strlist << "-1";
    }

    SendResponse(pbssock, strlist);
}

void MainServer::HandleGetFreeRecorderCount(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strlist;
    int count = 0;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if ((elink->IsConnected()) &&
            (!elink->IsBusy()) &&
            (!elink->IsTunerLocked()))
        {
            count++;
        }
    }

    strlist << QString::number(count);
        
    SendResponse(pbssock, strlist);
}

void MainServer::HandleGetFreeRecorderList(PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QStringList strlist;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if ((elink->IsConnected()) &&
            (!elink->IsBusy()) &&
            (!elink->IsTunerLocked()))
        {
            strlist << QString::number(iter.key());
        }
    }

    if (strlist.size() == 0)
        strlist << "0";

    SendResponse(pbssock, strlist);
}

void MainServer::HandleGetNextFreeRecorder(QStringList &slist, 
                                           PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();
    QString pbshost = pbs->getHostname();

    QStringList strlist;
    int retval = -1;
    int currrec = slist[1].toInt();

    EncoderLink *encoder = NULL;
    QString enchost;

    VERBOSE(VB_RECORD, QString("Getting next free recorder after : %1")
            .arg(currrec));

    // find current recorder
    QMap<int, EncoderLink *>::Iterator iter, curr = encoderList->find(currrec);

    if (currrec > 0 && curr != encoderList->end())
    {
        // cycle through all recorders
        for (iter = curr;;)
        {
            EncoderLink *elink;

            // last item? go back
            if (++iter == encoderList->end())
            {
                iter = encoderList->begin();
            }

            elink = iter.data();

            if ((retval == -1) &&
                (elink->IsConnected()) &&
                (!elink->IsBusy()) &&
                (!elink->IsTunerLocked()))
            {
                encoder = elink;
                retval = iter.key();
            }

            // cycled right through? no more available recorders
            if (iter == curr) 
                break;
        }
    } 
    else 
    {
        HandleGetFreeRecorder(pbs);
        return;
    }


    strlist << QString::number(retval);
        
    if (encoder)
    {
        if (encoder->IsLocal())
        {
            strlist << gContext->GetSetting("BackendServerIP");
            strlist << gContext->GetSetting("BackendServerPort");
        }
        else
        {
            strlist << gContext->GetSettingOnHost("BackendServerIP", 
                                                  encoder->GetHostName(),
                                                  "nohostname");
            strlist << gContext->GetSettingOnHost("BackendServerPort", 
                                                  encoder->GetHostName(), "-1");
        }
    }
    else
    {
        strlist << "nohost";
        strlist << "-1";
    }

    SendResponse(pbssock, strlist);
}

static QString cleanup(const QString &str)
{
    if (str == " ")
        return "";
    return str;
}

static QString make_safe(const QString &str)
{
    if (str.isEmpty())
        return " ";
    return str;
}

void MainServer::HandleRecorderQuery(QStringList &slist, QStringList &commands,
                                     PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int recnum = commands[1].toInt();

    QMap<int, EncoderLink *>::Iterator iter = encoderList->find(recnum);
    if (iter == encoderList->end())
    {
        VERBOSE(VB_IMPORTANT, "MainServer::HandleRecorderQuery() " +
                QString("Unknown encoder: %1").arg(recnum));
        QStringList retlist = "bad";
        SendResponse(pbssock, retlist);
        return;
    }

    QString command = slist[1];

    QStringList retlist;

    EncoderLink *enc = iter.data();
    if (!enc->IsConnected())
    {
        VERBOSE(VB_IMPORTANT," MainServer::HandleRecorderQuery() " +
                QString("Command %1 for unconnected encoder %2")
                .arg(command).arg(recnum));
        retlist << "bad";
        SendResponse(pbssock, retlist);    
        return;
    }

    if (command == "IS_RECORDING")
    {
        retlist << QString::number((int)enc->IsReallyRecording());
    }
    else if (command == "GET_FRAMERATE")
    {
        retlist << QString::number(enc->GetFramerate());
    }
    else if (command == "GET_FRAMES_WRITTEN")
    {
        long long value = enc->GetFramesWritten();
        encodeLongLong(retlist, value);
    }
    else if (command == "GET_FILE_POSITION")
    {
        long long value = enc->GetFilePosition();
        encodeLongLong(retlist, value);
    }
    else if (command == "GET_MAX_BITRATE")
    {
        long long value = enc->GetMaxBitrate();
        encodeLongLong(retlist, value);
    }
    else if (command == "GET_CURRENT_RECORDING")
    {
        ProgramInfo *info = enc->GetRecording();
        info->ToStringList(retlist);
        delete info;
    }
    else if (command == "GET_KEYFRAME_POS")
    {
        long long desired = decodeLongLong(slist, 2);

        long long value = enc->GetKeyframePosition(desired);
        encodeLongLong(retlist, value);
    }
    else if (command == "FILL_POSITION_MAP")
    {
        int start = slist[2].toInt();
        int end   = slist[3].toInt();

        for (int keynum = start; keynum <= end; keynum++)
        {
            long long value = enc->GetKeyframePosition(keynum);
            if (value != -1) 
            {
                encodeLongLong(retlist, keynum);
                encodeLongLong(retlist, value);
            }
        }

        if (!retlist.size())
            retlist << "ok";
    }
    else if (command == "GET_RECORDING")
    {
        ProgramInfo *pginfo = enc->GetRecording();
        if (pginfo)
        {
            pginfo->ToStringList(retlist);
            delete pginfo;
        }
        else
        {
            ProgramInfo dummy;
            dummy.ToStringList(retlist);
        }
    }
    else if (command == "FRONTEND_READY")
    {
        enc->FrontendReady();
        retlist << "ok";
    }
    else if (command == "CANCEL_NEXT_RECORDING")
    {
        QString cancel = slist[2];
        VERBOSE(VB_IMPORTANT, "Received: CANCEL_NEXT_RECORDING "<<cancel);
        enc->CancelNextRecording(cancel == "1");
        retlist << "ok";
    }
    else if (command == "SPAWN_LIVETV")
    {
        QString chainid = slist[2];
        LiveTVChain *chain = GetExistingChain(chainid);
        if (!chain)
        {
            chain = new LiveTVChain();
            chain->LoadFromExistingChain(chainid);
            AddToChains(chain);
        }

        chain->SetHostSocket(pbssock);

        enc->SpawnLiveTV(chain, slist[3].toInt(), slist[4]);
        retlist << "ok";
    }
    else if (command == "STOP_LIVETV")
    {
        QString chainid = enc->GetChainID();
        enc->StopLiveTV();

        LiveTVChain *chain = GetExistingChain(chainid);
        if (chain)
        {
            chain->DelHostSocket(pbssock);
            if (chain->HostSocketCount() == 0)
            {
                DeleteChain(chain);
            }
        }

        retlist << "ok";
    }
    else if (command == "PAUSE")
    {
        enc->PauseRecorder();
        retlist << "ok";
    }
    else if (command == "FINISH_RECORDING")
    {
        enc->FinishRecording();
        retlist << "ok";
    }
    else if (command == "SET_LIVE_RECORDING")
    {
        int recording = slist[2].toInt();
        enc->SetLiveRecording(recording);
        retlist << "ok";
    }
    else if (command == "GET_FREE_INPUTS")
    {
        vector<uint> excluded_cardids;
        for (uint i = 2; i < slist.size(); i++)
            excluded_cardids.push_back(slist[i].toUInt());

        vector<InputInfo> inputs = enc->GetFreeInputs(excluded_cardids);

        if (inputs.empty())
            retlist << "EMPTY_LIST";
        else
        {
            for (uint i = 0; i < inputs.size(); i++)
                inputs[i].ToStringList(retlist);
        }
    }
    else if (command == "GET_INPUT")
    {
        QString ret = enc->GetInput();
        ret = (ret.isEmpty()) ? "UNKNOWN" : ret;
        retlist << ret;
    }
    else if (command == "SET_INPUT")
    {
        QString input = slist[2];
        QString ret   = enc->SetInput(input);
        ret = (ret.isEmpty()) ? "UNKNOWN" : ret;
        retlist << ret;
    }
    else if (command == "TOGGLE_CHANNEL_FAVORITE")
    {
        enc->ToggleChannelFavorite();
        retlist << "ok";
    }
    else if (command == "CHANGE_CHANNEL")
    {
        int direction = slist[2].toInt(); 
        enc->ChangeChannel(direction);
        retlist << "ok";
    }
    else if (command == "SET_CHANNEL")
    {
        QString name = slist[2];
        enc->SetChannel(name);
        retlist << "ok";
    }
    else if (command == "SET_SIGNAL_MONITORING_RATE")
    {
        int rate = slist[2].toInt();
        int notifyFrontend = slist[3].toInt();
        int oldrate = enc->SetSignalMonitoringRate(rate, notifyFrontend);
        retlist << QString::number(oldrate);
    }
    else if (command == "GET_COLOUR")
    {
        int ret = enc->GetPictureAttribute(kPictureAttribute_Colour);
        retlist << QString::number(ret);
    }
    else if (command == "GET_CONTRAST")
    {
        int ret = enc->GetPictureAttribute(kPictureAttribute_Contrast);
        retlist << QString::number(ret);
    }
    else if (command == "GET_BRIGHTNESS")
    {
        int ret = enc->GetPictureAttribute(kPictureAttribute_Brightness);
        retlist << QString::number(ret);
    }
    else if (command == "GET_HUE")
    {
        int ret = enc->GetPictureAttribute(kPictureAttribute_Hue);
        retlist << QString::number(ret);
    }
    else if (command == "CHANGE_COLOUR")
    {
        int  type = slist[2].toInt();
        bool up   = slist[3].toInt(); 
        int  ret = enc->ChangePictureAttribute(
            (PictureAdjustType) type, kPictureAttribute_Colour, up);
        retlist << QString::number(ret);
    }
    else if (command == "CHANGE_CONTRAST")
    {
        int  type = slist[2].toInt();
        bool up   = slist[3].toInt(); 
        int  ret = enc->ChangePictureAttribute(
            (PictureAdjustType) type, kPictureAttribute_Contrast, up);
        retlist << QString::number(ret);
    }
    else if (command == "CHANGE_BRIGHTNESS")
    {
        int  type= slist[2].toInt();
        bool up  = slist[3].toInt(); 
        int  ret = enc->ChangePictureAttribute(
            (PictureAdjustType) type, kPictureAttribute_Brightness, up);
        retlist << QString::number(ret);
    }
    else if (command == "CHANGE_HUE")
    {
        int  type= slist[2].toInt();
        bool up  = slist[3].toInt();
        int  ret = enc->ChangePictureAttribute(
            (PictureAdjustType) type, kPictureAttribute_Hue, up);
        retlist << QString::number(ret);
    }
    else if (command == "CHECK_CHANNEL")
    {
        QString name = slist[2];
        retlist << QString::number((int)(enc->CheckChannel(name)));
    }
    else if (command == "SHOULD_SWITCH_CARD")
    {
        QString chanid = slist[2];
        retlist << QString::number((int)(enc->ShouldSwitchToAnotherCard(chanid)));
    }
    else if (command == "CHECK_CHANNEL_PREFIX")
    {
        QString needed_spacer = QString::null;
        QString prefix        = slist[2];
        uint    is_complete_valid_channel_on_rec = 0;
        bool    is_extra_char_useful             = false;

        bool match = enc->CheckChannelPrefix(
            prefix, is_complete_valid_channel_on_rec,
            is_extra_char_useful, needed_spacer);

        retlist << QString::number((int)match);
        retlist << QString::number(is_complete_valid_channel_on_rec);
        retlist << QString::number((int)is_extra_char_useful);
        retlist << ((needed_spacer.isEmpty()) ? QString("X") : needed_spacer);
    }
    else if (command == "GET_NEXT_PROGRAM_INFO")
    {
        QString channelname = slist[2];
        QString chanid = slist[3];
        int direction = slist[4].toInt();
        QString starttime = slist[5];

        QString title = "", subtitle = "", desc = "", category = "";
        QString endtime = "", callsign = "", iconpath = "";
        QString seriesid = "", programid = "";

        enc->GetNextProgram(direction,
                            title, subtitle, desc, category, starttime,
                            endtime, callsign, iconpath, channelname, chanid,
                            seriesid, programid);

        retlist << make_safe(title);
        retlist << make_safe(subtitle);
        retlist << make_safe(desc);
        retlist << make_safe(category);
        retlist << make_safe(starttime);
        retlist << make_safe(endtime);
        retlist << make_safe(callsign);
        retlist << make_safe(iconpath);
        retlist << make_safe(channelname);
        retlist << make_safe(chanid);
        retlist << make_safe(seriesid);
        retlist << make_safe(programid);
    }
    else if (command == "GET_CHANNEL_INFO")
    {
        uint chanid = slist[2].toUInt();
        uint sourceid = 0;
        QString callsign = "", channum = "", channame = "", xmltv = "";

        enc->GetChannelInfo(chanid, sourceid,
                            callsign, channum, channame, xmltv);

        retlist << QString::number(chanid);
        retlist << QString::number(sourceid);
        retlist << make_safe(callsign);
        retlist << make_safe(channum);
        retlist << make_safe(channame);
        retlist << make_safe(xmltv);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, QString("Unknown command: %1").arg(command));
        retlist << "ok";
    }

    SendResponse(pbssock, retlist);    
}

void MainServer::HandleSetNextLiveTVDir(QStringList &commands,
                                        PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int recnum = commands[1].toInt();

    QMap<int, EncoderLink *>::Iterator iter = encoderList->find(recnum);
    if (iter == encoderList->end())
    {
        VERBOSE(VB_IMPORTANT, "MainServer::HandleSetNextLiveTVDir() " +
                QString("Unknown encoder: %1").arg(recnum));
        QStringList retlist = "bad";
        SendResponse(pbssock, retlist);
        return;
    }

    EncoderLink *enc = iter.data();
    enc->SetNextLiveTVDir(commands[2]);

    QStringList retlist = "OK";
    SendResponse(pbssock, retlist);
}

void MainServer::HandleSetChannelInfo(QStringList &slist, PlaybackSock *pbs)
{
    bool        ok       = true;
    MythSocket *pbssock  = pbs->getSocket();
    uint        chanid   = slist[1].toUInt();
    uint        sourceid = slist[2].toUInt();
    QString     oldcnum  = cleanup(slist[3]);
    QString     callsign = cleanup(slist[4]);
    QString     channum  = cleanup(slist[5]);
    QString     channame = cleanup(slist[6]);
    QString     xmltv    = cleanup(slist[7]);

    QStringList retlist;
    if (!chanid || !sourceid)
    {
        retlist << "0";
        SendResponse(pbssock, retlist);
        return;
    }

    QMap<int, EncoderLink *>::iterator it = encoderList->begin();
    for (; it != encoderList->end(); ++it)
    {
        if (*it)
        {
            ok &= (*it)->SetChannelInfo(chanid, sourceid, oldcnum,
                                        callsign, channum, channame, xmltv);
        }
    }

    retlist << ((ok) ? "1" : "0");
    SendResponse(pbssock, retlist);
}

void MainServer::HandleRemoteEncoder(QStringList &slist, QStringList &commands,
                                     PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int recnum = commands[1].toInt();
    QStringList retlist;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->find(recnum);
    if (iter == encoderList->end())
    {
        VERBOSE(VB_IMPORTANT, "MainServer: " +
                QString("HandleRemoteEncoder(cmd %1) ").arg(slist[1]) +
                QString("Unknown encoder: %1").arg(recnum));
        retlist << QString::number((int) kState_Error);
        SendResponse(pbssock, retlist);
        return;
    }

    EncoderLink *enc = iter.data();

    QString command = slist[1];

    if (command == "GET_STATE")
    {
        retlist << QString::number((int)enc->GetState());
    }
    else if (command == "GET_FLAGS")
    {
        retlist << QString::number(enc->GetFlags());
    }
    else if (command == "IS_BUSY")
    {
        int time_buffer = (slist.size() >= 3) ? slist[2].toInt() : 5;
        TunedInputInfo busy_input;
        retlist << QString::number((int)enc->IsBusy(&busy_input, time_buffer));
        busy_input.ToStringList(retlist);
    }
    else if (command == "MATCHES_RECORDING")
    {
        ProgramInfo *pginfo = new ProgramInfo();
        pginfo->FromStringList(slist, 2);

        retlist << QString::number((int)enc->MatchesRecording(pginfo));

        delete pginfo;
    }
    else if (command == "START_RECORDING")
    {
        ProgramInfo *pginfo = new ProgramInfo();
        pginfo->FromStringList(slist, 2);
 
        retlist << QString::number(enc->StartRecording(pginfo));

        delete pginfo;
    }
    else if (command == "RECORD_PENDING")
    {
        ProgramInfo *pginfo = new ProgramInfo();
        int secsleft = slist[2].toInt();
        int haslater = slist[3].toInt();
        pginfo->FromStringList(slist, 4);

        enc->RecordPending(pginfo, secsleft, haslater);
 
        retlist << "OK";
        delete pginfo;
    }
    else if (command == "CANCEL_NEXT_RECORDING")
    {
        bool cancel = (bool) slist[2].toInt();
        enc->CancelNextRecording(cancel);
        retlist << "OK";
    }
    else if (command == "STOP_RECORDING")
    {
        enc->StopRecording();
        retlist << "OK";
    }
    else if (command == "GET_MAX_BITRATE")
    {
        long long value = enc->GetMaxBitrate();
        encodeLongLong(retlist, value);
    }
    else if (command == "GET_CURRENT_RECORDING")
    {
        ProgramInfo *info = enc->GetRecording();
        info->ToStringList(retlist);
        delete info;
    }
    else if (command == "GET_FREE_INPUTS")
    {
        vector<uint> excluded_cardids;
        for (uint i = 2; i < slist.size(); i++)
            excluded_cardids.push_back(slist[i].toUInt());

        vector<InputInfo> inputs = enc->GetFreeInputs(excluded_cardids);

        if (inputs.empty())
            retlist << "EMPTY_LIST";
        else
        {
            for (uint i = 0; i < inputs.size(); i++)
                inputs[i].ToStringList(retlist);
        }
    }

    SendResponse(pbssock, retlist);
}

void MainServer::HandleIsActiveBackendQuery(QStringList &slist,
                                            PlaybackSock *pbs)
{
    QStringList retlist;
    QString queryhostname = slist[1];
    
    if (gContext->GetHostName() != queryhostname)
    {
        PlaybackSock *slave = getSlaveByHostname(queryhostname);
        if (slave != NULL)
        {
            retlist << "TRUE";
            slave->DownRef();
        }
        else
            retlist << "FALSE";
    }
    else
        retlist << "TRUE";

    SendResponse(pbs->getSocket(), retlist);
}

// Helper function for the guts of HandleCommBreakQuery + HandleCutListQuery
void MainServer::HandleCutMapQuery(const QString &chanid, 
                                   const QString &starttime,
                                   PlaybackSock *pbs, bool commbreak)
{
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    QMap<long long, int> markMap;
    QMap<long long, int>::Iterator it;
    QDateTime startdt;
    startdt.setTime_t((uint)atoi(starttime));
    QStringList retlist;
    int rowcnt = 0;

    ProgramInfo *pginfo = ProgramInfo::GetProgramFromRecorded(chanid,
                                                              startdt);

    if (pginfo)
    {
        if (commbreak)
            pginfo->GetCommBreakList(markMap);
        else
            pginfo->GetCutList(markMap);

        for (it = markMap.begin(); it != markMap.end(); ++it)
        {
            rowcnt++;
            QString intstr = QString("%1").arg(it.data());
            retlist << intstr;
            encodeLongLong(retlist,it.key());
        }
    }

    if (rowcnt > 0)
        retlist.prepend(QString("%1").arg(rowcnt));
    else
        retlist << "-1";

    if (pbssock)
        SendResponse(pbssock, retlist);

    return;
}

void MainServer::HandleCommBreakQuery(const QString &chanid,
                                      const QString &starttime,
                                      PlaybackSock *pbs)
{
// Commercial break query
// Format:  QUERY_COMMBREAK <chanid> <starttime>
// chanid is chanid, starttime is startime of prorgram in
//   # of seconds since Jan 1, 1970, in UTC time.  Same format as in
//   a ProgramInfo structure in a string list.
// Return structure is [number of rows] followed by a triplet of values:
//   each triplet : [type] [long portion 1] [long portion 2]
// type is the value in the map, right now 4 = commbreak start, 5= end
    return HandleCutMapQuery(chanid, starttime, pbs, true);
}

void MainServer::HandleCutlistQuery(const QString &chanid,
                                    const QString &starttime,
                                    PlaybackSock *pbs)
{
// Cutlist query
// Format:  QUERY_CUTLIST <chanid> <starttime>
// chanid is chanid, starttime is startime of prorgram in
//   # of seconds since Jan 1, 1970, in UTC time.  Same format as in
//   a ProgramInfo structure in a string list.
// Return structure is [number of rows] followed by a triplet of values:
//   each triplet : [type] [long portion 1] [long portion 2]
// type is the value in the map, right now 0 = commbreak start, 1 = end
    return HandleCutMapQuery(chanid, starttime, pbs, false);
}


void MainServer::HandleBookmarkQuery(const QString &chanid,
                                     const QString &starttime,
                                     PlaybackSock *pbs)
// Bookmark query
// Format:  QUERY_BOOKMARK <chanid> <starttime>
// chanid is chanid, starttime is startime of prorgram in
//   # of seconds since Jan 1, 1970, in UTC time.  Same format as in
//   a ProgramInfo structure in a string list.
// Return value is a long-long encoded as two separate values
{
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    QDateTime startdt;
    startdt.setTime_t((uint)atoi(starttime));
    QStringList retlist;
    long long bookmark = 0;

    ProgramInfo *pginfo = ProgramInfo::GetProgramFromRecorded(chanid,
                                                              startdt);
    if (pginfo)
        bookmark = pginfo->GetBookmark();

    encodeLongLong(retlist,bookmark);

    if (pbssock)
        SendResponse(pbssock, retlist);

    return;
}


void MainServer::HandleSetBookmark(QStringList &tokens,
                                   PlaybackSock *pbs)
{
// Bookmark query
// Format:  SET_BOOKMARK <chanid> <starttime> <long part1> <long part2>
// chanid is chanid, starttime is startime of prorgram in
//   # of seconds since Jan 1, 1970, in UTC time.  Same format as in
//   a ProgramInfo structure in a string list.  The two longs are the two
//   portions of the bookmark value to set.

    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    QString chanid = tokens[1];
    QString starttime = tokens[2];
    QStringList bookmarklist;
    bookmarklist << tokens[3];
    bookmarklist << tokens[4];

    QDateTime startdt;
    startdt.setTime_t((uint)atoi(starttime));
    QStringList retlist;
    long long bookmark = decodeLongLong(bookmarklist, 0);

    ProgramInfo *pginfo = ProgramInfo::GetProgramFromRecorded(chanid,
                                                              startdt);
    if (pginfo)
    {
        pginfo->SetBookmark(bookmark);
        retlist << "OK";
    }
    else
        retlist << "FAILED";

    if (pbssock)
        SendResponse(pbssock, retlist);

    return;
}

void MainServer::HandleSettingQuery(QStringList &tokens, PlaybackSock *pbs)
{
// Format: QUERY_SETTING <hostname> <setting>
// Returns setting value as a string

    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    QString hostname = tokens[1];
    QString setting = tokens[2];
    QStringList retlist;

    QString retvalue = gContext->GetSettingOnHost(setting, hostname, "-1");

    retlist << retvalue;
    if (pbssock)
        SendResponse(pbssock, retlist);

    return;
}

void MainServer::HandleSetSetting(QStringList &tokens,
                                  PlaybackSock *pbs)
{
// Format: SET_SETTING <hostname> <setting> <value>
    MythSocket *pbssock = NULL;
    if (pbs)
        pbssock = pbs->getSocket();

    QString hostname = tokens[1];
    QString setting = tokens[2];
    QString svalue = tokens[3];
    QStringList retlist;

    if (gContext->SaveSettingOnHost(setting, svalue, hostname))
        retlist << "OK";
    else
        retlist << "ERROR";

    if (pbssock)
        SendResponse(pbssock, retlist);

    return;
}

void MainServer::HandleFileTransferQuery(QStringList &slist,
                                         QStringList &commands,
                                         PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int recnum = commands[1].toInt();

    QStringList retlist;
    FileTransfer *ft = getFileTransferByID(recnum);
    if (!ft)
    {
        VERBOSE(VB_IMPORTANT, QString("Unknown file transfer socket: %1")
                               .arg(recnum));
        retlist << QString("ERROR: Unknown file transfer socket: %1")
                           .arg(recnum);
        SendResponse(pbssock, retlist);
        return;
    }

    QString command = slist[1];

    ft->UpRef();

    if (command == "IS_OPEN")
    {
        bool isopen = ft->isOpen();

        retlist << QString::number(isopen);
    }
    else if (command == "DONE")
    {
        ft->Stop();
        retlist << "ok";
    }
    else if (command == "REQUEST_BLOCK")
    {
        int size = slist[2].toInt();

        retlist << QString::number(ft->RequestBlock(size));
    }
    else if (command == "SEEK")
    {
        long long pos = decodeLongLong(slist, 2);
        int whence = slist[4].toInt();
        long long curpos = decodeLongLong(slist, 5);

        long long ret = ft->Seek(curpos, pos, whence);
        encodeLongLong(retlist, ret);
    }
    else if (command == "SET_TIMEOUT")
    {
        bool fast = slist[2].toInt();
        ft->SetTimeout(fast);
        retlist << "ok";
    }
    else 
    {
        VERBOSE(VB_IMPORTANT, QString("Unknown command: %1").arg(command));
        retlist << "ok";
    }

    ft->DownRef();

    SendResponse(pbssock, retlist);
}

void MainServer::HandleGetRecorderNum(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int retval = -1;

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);

    EncoderLink *encoder = NULL;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (elink->IsConnected() && elink->MatchesRecording(pginfo))
        {
            retval = iter.key();
            encoder = elink;
        }
    }
    
    QStringList strlist = QString::number(retval);

    if (encoder)
    {
        if (encoder->IsLocal())
        {
            strlist << gContext->GetSetting("BackendServerIP");
            strlist << gContext->GetSetting("BackendServerPort");
        }
        else
        {
            strlist << gContext->GetSettingOnHost("BackendServerIP",
                                                  encoder->GetHostName(),
                                                  "nohostname");
            strlist << gContext->GetSettingOnHost("BackendServerPort",
                                                  encoder->GetHostName(), "-1");
        }
    }
    else
    {
        strlist << "nohost";
        strlist << "-1";
    }

    SendResponse(pbssock, strlist);    
    delete pginfo;
}

void MainServer::HandleGetRecorderFromNum(QStringList &slist, 
                                          PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    int recordernum = slist[1].toInt();
    EncoderLink *encoder = NULL;
    QStringList strlist;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->find(recordernum);

    if (iter != encoderList->end())
        encoder =  iter.data();

    if (encoder && encoder->IsConnected())
    {
        if (encoder->IsLocal())
        {
            strlist << gContext->GetSetting("BackendServerIP");
            strlist << gContext->GetSetting("BackendServerPort");
        }
        else
        {
            strlist << gContext->GetSettingOnHost("BackendServerIP",
                                                  encoder->GetHostName(),
                                                  "nohostname");
            strlist << gContext->GetSettingOnHost("BackendServerPort",
                                                  encoder->GetHostName(), "-1");
        }
    }
    else
    {
        strlist << "nohost";
        strlist << "-1";
    }

    SendResponse(pbssock, strlist);
}

void MainServer::HandleMessage(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    QString message = slist[1];

    MythEvent me(message);
    gContext->dispatch(me);

    QStringList retlist = "OK";

    SendResponse(pbssock, retlist);
}

void MainServer::HandleIsRecording(QStringList &slist, PlaybackSock *pbs)
{
    (void)slist;

    MythSocket *pbssock = pbs->getSocket();
    int RecordingsInProgress = 0;
    int LiveTVRecordingsInProgress = 0;
    QStringList retlist;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();
        if (elink->IsBusyRecording()) {
            RecordingsInProgress++;

            ProgramInfo *info = elink->GetRecording();
            if (info && info->recgroup == "LiveTV")
                LiveTVRecordingsInProgress++;

            delete info;
        }
    }

    retlist << QString::number(RecordingsInProgress);
    retlist << QString::number(LiveTVRecordingsInProgress);

    SendResponse(pbssock, retlist);
}

void MainServer::HandleGenPreviewPixmap(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    bool      time_fmt_sec   = true;
    long long time           = -1;
    QString   outputfile     = QString::null;
    int       width          = -1;
    int       height         = -1;
    bool      has_extra_data = false;

    QStringList::const_iterator it = slist.at(1);
    QStringList::const_iterator end = slist.end();
    ProgramInfo *pginfo = new ProgramInfo();
    bool ok = pginfo->FromStringList(it, end);
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, "MainServer: Failed to parse pixmap request.");
        QStringList outputlist = "BAD";
        outputlist += "ERROR_INVALID_REQUEST";
        SendResponse(pbssock, outputlist);
    }
    if (it != slist.end())
        (time_fmt_sec = ((*it).lower() == "s")), it++;
    if (it != slist.end())
        time = decodeLongLong(slist, it);
    if (it != slist.end())
        (outputfile = *it), it++;
    outputfile = (outputfile == "<EMPTY>") ? QString::null : outputfile;
    if (it != slist.end())
    {
        width = (*it).toInt(&ok); it++;
        width = (ok) ? width : -1;
    }
    if (it != slist.end())
    {
        height = (*it).toInt(&ok); it++;
        height = (ok) ? height : -1;
        has_extra_data = true;
    }
    QSize outputsize = QSize(width, height);

    if (has_extra_data)
    {
        VERBOSE(VB_PLAYBACK, "HandleGenPreviewPixmap got extra data\n\t\t\t"
                << QString("%1%2 %3x%4 '%5'")
                .arg(time).arg(time_fmt_sec?"s":"f")
                .arg(width).arg(height).arg(outputfile));
    }

    pginfo->pathname = GetPlaybackURL(pginfo);

    if ((ismaster) &&
        (pginfo->hostname != gContext->GetHostName()) &&
        ((!masterBackendOverride) ||
         (pginfo->pathname.left(1) != "/")))
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);

        if (slave)
        {
            QStringList outputlist = "OK";
            if (has_extra_data)
            {
                outputlist = slave->GenPreviewPixmap(
                    pginfo, time_fmt_sec, time, outputfile, outputsize);
            }
            else
            {
                outputlist = slave->GenPreviewPixmap(pginfo);
            }

            slave->DownRef();
            SendResponse(pbssock, outputlist);
            delete pginfo;
            return;
        }
        VERBOSE(VB_IMPORTANT, "MainServer::HandleGenPreviewPixmap()"
                "\n\t\t\tCouldn't find backend for: " +
                QString("\n\t\t\t%1 : \"%2\"")
                .arg(pginfo->title).arg(pginfo->subtitle));
    }

    if (pginfo->pathname.left(1) != "/")
    {
        VERBOSE(VB_IMPORTANT, "MainServer: HandleGenPreviewPixmap: Unable to "
                "find file locally, unable to make preview image.");
        QStringList outputlist = "BAD";
        outputlist += "ERROR_NOFILE";
        SendResponse(pbssock, outputlist);
        delete pginfo;
        return;
    }

    PreviewGenerator *previewgen = new PreviewGenerator(pginfo);
    if (has_extra_data)
    {
        previewgen->SetOutputSize(outputsize);
        previewgen->SetOutputFilename(outputfile);
        previewgen->SetPreviewTime(time, time_fmt_sec);
    }
    ok = previewgen->Run();
    previewgen->deleteLater();

    if (ok)
    {
        QStringList outputlist = "OK";
        if (!outputfile.isEmpty())
            outputlist += outputfile;
        SendResponse(pbssock, outputlist);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "MainServer: Failed to make preview image.");
        QStringList outputlist = "BAD";
        outputlist += "ERROR_UNKNOWN";
        SendResponse(pbssock, outputlist);
    } 

    delete pginfo;
}

void MainServer::HandlePixmapLastModified(QStringList &slist, PlaybackSock *pbs)
{
    MythSocket *pbssock = pbs->getSocket();

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->FromStringList(slist, 1);
    pginfo->pathname = GetPlaybackURL(pginfo);

    QDateTime lastmodified;
    QStringList strlist;

    if ((ismaster) &&
        (pginfo->hostname != gContext->GetHostName()) &&
        ((!masterBackendOverride) ||
         (pginfo->pathname.left(1) != "/")))
    {
        PlaybackSock *slave = getSlaveByHostname(pginfo->hostname);

        if (slave) 
        {
             QDateTime slavetime = slave->PixmapLastModified(pginfo);
             slave->DownRef();

             strlist = (slavetime.isValid()) ?
                 QString::number(slavetime.toTime_t()) : "BAD";

             SendResponse(pbssock, strlist);
             delete pginfo;
             return;
        }
        else
        {
            VERBOSE(VB_IMPORTANT, QString("MainServer::HandlePixmapLastModified()"
                    " - Couldn't find backend for: %1 : \"%2\"").arg(pginfo->title)
                    .arg(pginfo->subtitle));
        }
    }

    if (pginfo->pathname.left(1) != "/")
    {
        VERBOSE(VB_IMPORTANT, "MainServer: HandlePixmapLastModified: Unable to "
                "find file locally, unable to get last modified date.");
        QStringList outputlist = "BAD";
        SendResponse(pbssock, outputlist);
        delete pginfo;
        return;
    }

    QString filename = pginfo->pathname + ".png";

    QFileInfo finfo(filename);

    if (finfo.exists())
    {
        lastmodified = finfo.lastModified();
        strlist = QString::number(lastmodified.toTime_t());
    }
    else
        strlist = "BAD";   
 
    SendResponse(pbssock, strlist);
    delete pginfo;
}

void MainServer::HandleBackendRefresh(MythSocket *socket)
{
    gContext->RefreshBackendConfig();

    QStringList retlist = "OK";
    SendResponse(socket, retlist);    
}

void MainServer::HandleBlockShutdown(bool blockShutdown, PlaybackSock *pbs)
{            
    pbs->setBlockShutdown(blockShutdown);
    
    MythSocket *socket = pbs->getSocket();
    QStringList retlist = "OK";
    SendResponse(socket, retlist);    
}

void MainServer::deferredDeleteSlot(void)
{
    QMutexLocker lock(&deferredDeleteLock);

    if (deferredDeleteList.size() == 0)
        return;

    DeferredDeleteStruct dds = deferredDeleteList.front();
    while (dds.ts.secsTo(QDateTime::currentDateTime()) > 30)
    {
        delete dds.sock;
        deferredDeleteList.pop_front();
        if (deferredDeleteList.size() == 0)
            return;
        dds = deferredDeleteList.front();
    }
}

void MainServer::DeletePBS(PlaybackSock *sock)
{
    DeferredDeleteStruct dds;
    dds.sock = sock;
    dds.ts = QDateTime::currentDateTime();

    QMutexLocker lock(&deferredDeleteLock);
    deferredDeleteList.push_back(dds);
}

void MainServer::connectionClosed(MythSocket *socket)
{
    sockListLock.lock();

    vector<PlaybackSock *>::iterator it = playbackList.begin();
    for (; it != playbackList.end(); ++it)
    {
        PlaybackSock *pbs = (*it);
        MythSocket *sock = pbs->getSocket();
        if (sock == socket && pbs == masterServer)
        {
            playbackList.erase(it);
            sockListLock.unlock();
            masterServer->DownRef();
            masterServer = NULL;
            masterServerReconnect->start(1000, true);
            return;
        }
        else if (sock == socket)
        {
            if (ismaster && pbs->isSlaveBackend())
            {
                VERBOSE(VB_IMPORTANT,QString("Slave backend: %1 no longer connected")
                                       .arg(pbs->getHostname()));

                QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
                for (; iter != encoderList->end(); ++iter)
                {
                    EncoderLink *elink = iter.data();
                    if (elink->GetSocket() == pbs)
                    {
                        elink->SetSocket(NULL);
                        if (m_sched)
                            m_sched->SlaveDisconnected(elink->GetCardID());
                    }
                }
                if (m_sched)
                    m_sched->Reschedule(0);

                QString message = QString("LOCAL_SLAVE_BACKEND_OFFLINE %2")
                                          .arg(pbs->getHostname());
                MythEvent me(message);
                gContext->dispatch(me);
            }

            LiveTVChain *chain;
            if ((chain = GetExistingChain(sock)))
            {
                chain->DelHostSocket(sock);
                if (chain->HostSocketCount() == 0)
                {
                    QMap<int, EncoderLink *>::iterator i = encoderList->begin();
                    for (; i != encoderList->end(); ++i)
                    {
                        EncoderLink *enc = i.data();
                        if (enc->IsLocal())
                        {
                            while (enc->GetState() == kState_ChangingState)
                                usleep(500);

                            if (enc->IsBusy() && 
                                enc->GetChainID() == chain->GetID())
                            {
                                enc->StopLiveTV();
                            }
                        }
                    }
                    DeleteChain(chain);
                }
            }

            pbs->SetDisconnected();
            playbackList.erase(it);
            sockListLock.unlock();

            PlaybackSock *testsock = getPlaybackBySock(socket);
            if (testsock)
                VERBOSE(VB_IMPORTANT, "Playback sock still exists?");

            pbs->DownRef();
            return;
        }
    }

    vector<FileTransfer *>::iterator ft = fileTransferList.begin();
    for (; ft != fileTransferList.end(); ++ft)
    {
        MythSocket *sock = (*ft)->getSocket();
        if (sock == socket)
        {
            (*ft)->DownRef();
            fileTransferList.erase(ft);
            sockListLock.unlock();
            return;
        }
    }

    sockListLock.unlock();

    VERBOSE(VB_IMPORTANT, "Unknown socket closing");
}

PlaybackSock *MainServer::getSlaveByHostname(QString &hostname)
{
    if (!ismaster)
        return NULL;

    sockListLock.lock();

    vector<PlaybackSock *>::iterator iter = playbackList.begin();
    for (; iter != playbackList.end(); iter++)
    {
        PlaybackSock *pbs = (*iter);
        if (pbs->isSlaveBackend() && 
            ((pbs->getHostname() == hostname) || (pbs->getIP() == hostname)))
        {
            sockListLock.unlock();
            pbs->UpRef();
            return pbs;
        }
    }

    sockListLock.unlock();

    return NULL;
}

PlaybackSock *MainServer::getPlaybackBySock(MythSocket *sock)
{
    PlaybackSock *retval = NULL;

    sockListLock.lock();

    vector<PlaybackSock *>::iterator it = playbackList.begin();
    for (; it != playbackList.end(); ++it)
    {
        if (sock == (*it)->getSocket())
        {
            retval = (*it);
            break;
        }
    }

    sockListLock.unlock();

    return retval;
}

FileTransfer *MainServer::getFileTransferByID(int id)
{
    FileTransfer *retval = NULL;

    sockListLock.lock();

    vector<FileTransfer *>::iterator it = fileTransferList.begin();
    for (; it != fileTransferList.end(); ++it)
    {
        if (id == (*it)->getSocket()->socket())
        {
            retval = (*it);
            break;
        }
    }

    sockListLock.unlock();

    return retval;
}

FileTransfer *MainServer::getFileTransferBySock(MythSocket *sock)
{
    FileTransfer *retval = NULL;

    sockListLock.lock();

    vector<FileTransfer *>::iterator it = fileTransferList.begin();
    for (; it != fileTransferList.end(); ++it)
    {
        if (sock == (*it)->getSocket())
        {
            retval = (*it);
            break;
        }
    }

    sockListLock.unlock();

    return retval;
}

LiveTVChain *MainServer::GetExistingChain(QString id)
{
    QMutexLocker lock(&liveTVChainsLock);

    LiveTVChain *chain;

    QPtrListIterator<LiveTVChain> it(liveTVChains);
    while ((chain = it.current()) != 0)
    {
        ++it;
        if (chain->GetID() == id)
            return chain;
    }

    return NULL;
}

LiveTVChain *MainServer::GetExistingChain(MythSocket *sock)
{
    QMutexLocker lock(&liveTVChainsLock);

    LiveTVChain *chain;

    QPtrListIterator<LiveTVChain> it(liveTVChains);
    while ((chain = it.current()) != 0)
    {
        ++it;
        if (chain->IsHostSocket(sock))
            return chain;
    }

    return NULL;
}

LiveTVChain *MainServer::GetChainWithRecording(ProgramInfo *pginfo)
{
    QMutexLocker lock(&liveTVChainsLock);

    LiveTVChain *chain;

    QPtrListIterator<LiveTVChain> it(liveTVChains);
    while ((chain = it.current()) != 0)
    {
        ++it;
        if (chain->ProgramIsAt(pginfo) >= 0)
            return chain;
    }

    return NULL;
}

void MainServer::AddToChains(LiveTVChain *chain)
{
    liveTVChains.append(chain);
}

void MainServer::DeleteChain(LiveTVChain *chain)
{
    QMutexLocker lock(&liveTVChainsLock);

    while (liveTVChains.removeRef(chain))
        ;

    delete chain;
}

QString MainServer::LocalFilePath(QUrl &url)
{
    QString lpath = url.path();

    if (lpath.section('/', -2, -2) == "channels")
    {
        // This must be an icon request. Check channel.icon to be safe.
        QString querytext;

        QString file = lpath.section('/', -1);
        lpath = "";

        MSqlQuery query(MSqlQuery::InitCon());
        query.prepare("SELECT icon FROM channel WHERE icon LIKE :FILENAME ;");
        query.bindValue(":FILENAME", QString("%") + file);

        if (query.exec() && query.isActive() && query.size())
        {
            query.next();
            lpath = query.value(0).toString();
        }
        else
        {
            MythContext::DBError("Icon path", query);
        }
    }
    else
    {
        lpath = lpath.section('/', -1);

        QString fpath = lpath;
        if (fpath.right(4) == ".png")
            fpath = fpath.left(fpath.length() - 4);

        ProgramInfo *pginfo = ProgramInfo::GetProgramFromBasename(fpath);
        if (pginfo)
        {
            QString pburl = GetPlaybackURL(pginfo);
            if (pburl.left(1) == "/")
            {
                lpath = pburl.section('/', 0, -2) + "/" + lpath;
                VERBOSE(VB_FILE, QString("Local file path: %1").arg(lpath));
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("ERROR: LocalFilePath unable to find local "
                                "path for '%1', found '%2' instead.")
                                .arg(lpath).arg(pburl));
                lpath = "";
            }

            delete pginfo;
        }
        else if (!lpath.isEmpty())
        {
            // For securities sake, make sure filename is really the pathless.
            QString opath = lpath;
            lpath = QFileInfo(lpath).fileName();
            StorageGroup sgroup;
            QString tmpFile = sgroup.FindRecordingFile(lpath);
            if (!tmpFile.isEmpty())
            {
                lpath = tmpFile;
                VERBOSE(VB_FILE,
                        QString("LocalFilePath(%1 '%2')").arg(url).arg(opath)
                        <<", found file through exhaustive search "
                        <<QString("at '%1'").arg(lpath));
            }
            else
            {
                VERBOSE(VB_IMPORTANT, "ERROR: LocalFilePath "
                        <<QString("unable to find local path for '%1'.")
                        .arg(opath));
                lpath = "";
            }
        }
        else
        {
            lpath = "";
        }
    }

    return QDeepCopy<QString>(lpath);
}

void MainServer::reconnectTimeout(void)
{
    MythSocket *masterServerSock = new MythSocket();

    QString server = gContext->GetSetting("MasterServerIP", "127.0.0.1");
    int port = gContext->GetNumSetting("MasterServerPort", 6543);

    VERBOSE(VB_IMPORTANT, QString("Connecting to master server: %1:%2")
                           .arg(server).arg(port));

    if (!masterServerSock->connect(server, port))
    {
        VERBOSE(VB_IMPORTANT, "Connection to master server timed out.");
        masterServerReconnect->start(1000, true);
        masterServerSock->DownRef();
        return;
    }

    if (masterServerSock->state() != MythSocket::Connected)
    {
        VERBOSE(VB_IMPORTANT, "Could not connect to master server.");
        masterServerReconnect->start(1000, true);
        masterServerSock->DownRef();
        return;
    }

    VERBOSE(VB_IMPORTANT, "Connected successfully");

    QString str = QString("ANN SlaveBackend %1 %2")
                          .arg(gContext->GetHostName())
                          .arg(gContext->GetSetting("BackendServerIP"));

    masterServerSock->Lock();

    QStringList strlist = str;

    QMap<int, EncoderLink *>::Iterator iter = encoderList->begin();
    for (; iter != encoderList->end(); ++iter)
    {
        EncoderLink *elink = iter.data();
        elink->CancelNextRecording(true);
        ProgramInfo *pinfo = elink->GetRecording();
        pinfo->ToStringList(strlist);
        delete pinfo;
    }

    masterServerSock->writeStringList(strlist);
    masterServerSock->readStringList(strlist);
    masterServerSock->setCallbacks(this);

    masterServer = new PlaybackSock(this, masterServerSock, server, true);
    sockListLock.lock();
    playbackList.push_back(masterServer);
    sockListLock.unlock();

    masterServerSock->Unlock();

    autoexpireUpdateTimer->start(1000, true);
}

// returns true, if a client (slavebackends are not counted!)
// is connected by checking the lists.
bool MainServer::isClientConnected()
{
    bool foundClient = false;

    sockListLock.lock();

    foundClient |= (fileTransferList.size() > 0);

    if ((playbackList.size() > 0) && !foundClient)
    {
        vector<PlaybackSock *>::iterator it = playbackList.begin();
        for (; !foundClient && (it != playbackList.end()); ++it)
        {
            // we simply ignore slaveBackends!
            // and clients that don't want to block shutdown
            if (!(*it)->isSlaveBackend() && (*it)->getBlockShutdown())
                foundClient = true;
        }
    }

    sockListLock.unlock();
    
    return (foundClient);
}

// sends the Slavebackends the request to shut down using haltcmd
void MainServer::ShutSlaveBackendsDown(QString &haltcmd)
{
    QStringList bcast = "SHUTDOWN_NOW";
    bcast << haltcmd;

    sockListLock.lock();
    
    if (playbackList.size() > 0)
    {
        vector<PlaybackSock *>::iterator it = playbackList.begin();
        for (; it != playbackList.end(); ++it)
        {
            if ((*it)->isSlaveBackend())
                (*it)->getSocket()->writeStringList(bcast); 
        }
    }

    sockListLock.unlock();
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
