#include <cstdlib>
#include <unistd.h>
#include <signal.h>


#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <qregexp.h>
#include <qstring.h>
#include <qdatetime.h>
#include <qfileinfo.h>

#include <iostream>
#include <algorithm>
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

#include "autoexpire.h"
#include "programinfo.h"
#include "libmyth/mythcontext.h"
#include "libmyth/mythdbcon.h"
#include "libmyth/util.h"
#include "libmyth/storagegroup.h"
#include "libmythtv/remoteutil.h"
#include "libmythtv/remoteencoder.h"
#include "encoderlink.h"
#include "backendutil.h"
#include "compat.h"

#define LOC QString("AutoExpire: ")

extern AutoExpire *expirer;

/** If calculated desired space for 10 min freq is > SPACE_TOO_BIG_KB
 *  then we use 5 min expire frequency.
 */
#define SPACE_TOO_BIG_KB 3*1024*1024

/** \class AutoExpire
 *  \brief Used to expire recordings to make space for new recordings.
 */

/** \fn AutoExpire::AutoExpire(QMap<int, EncoderLink *> *tvList)
 *  \brief Creates AutoExpire class, starting the thread.
 *
 *  \param tvList    EncoderLink list of all recorders
 */
AutoExpire::AutoExpire(QMap<int, EncoderLink *> *tvList)
{
    encoderList = tvList;
    expire_thread_running = true;

    Init();

    pthread_create(&expire_thread, NULL, ExpirerThread, this);
    gContext->addListener(this);
}

/** \fn AutoExpire::AutoExpire()
 *  \brief Creates AutoExpire class
 */
AutoExpire::AutoExpire(void)
{
    encoderList = NULL;
    expire_thread_running = false;
    Init();
}

/** \fn AutoExpire::Init()
 *  \brief Inits member vars
 */
void AutoExpire::Init(void)
{
    desired_freq       = 15;
    update_pending     = false;
}

/** \fn AutoExpire::~AutoExpire()
 *  \brief AutoExpire destructor stops auto delete thread if it is running.
 */
AutoExpire::~AutoExpire()
{
    instance_lock.lock();
    while (update_pending)
        instance_cond.wait(&instance_lock);
    instance_lock.unlock();

    if (expire_thread_running)
    {
        gContext->removeListener(this);
        expire_thread_running = false;
        pthread_kill(expire_thread, SIGALRM); // try to speed up join..
        VERBOSE(VB_IMPORTANT, LOC + "Warning: Stopping auto expire thread "
                "can take several seconds. Please be patient.");
        pthread_join(expire_thread, NULL);
    }
}

/** \fn AutoExpire::GetDesiredSpace(int fsID)
 *   returns the desired free space for each file system
 *   used by the scheduler to select the next recording dir
 */

size_t AutoExpire::GetDesiredSpace(int fsID) const
{
    if (desired_space.contains(fsID))
        return desired_space[fsID];
    return 0;
}

/** \fn AutoExpire::CalcParams()
 *   Calcualtes how much space needs to be cleared, and how often.
 */
void AutoExpire::CalcParams()
{
    VERBOSE(VB_FILE, LOC + "CalcParams()");

    vector<FileSystemInfo> fsInfos;
    GetFilesystemInfos(encoderList, fsInfos);

    if (fsInfos.size() == 0)
    {
        QString msg = "ERROR: Filesystem Info cache is empty, unable to "
                      "calculate necessary parameters.";
        VERBOSE(VB_IMPORTANT, LOC + msg);
        gContext->LogEntry("mythbackend", LP_WARNING,
                           "Autoexpire CalcParams", msg);

        return;
    }

    size_t maxKBperMin = 0;
    size_t extraKB = gContext->GetNumSetting("AutoExpireExtraSpace", 0) << 20;

    QMap<int, uint64_t> fsMap;
    QMap<int, vector<int> > fsEncoderMap;

    // we use this copying on purpose. The used_encoders map ensures
    // that every encoder writes only to one fs.
    // Copying the data minizes the time the lock is held
    instance_lock.lock();
    QMap<int, int>::const_iterator ueit = used_encoders.begin();
    while (ueit != used_encoders.end())
    {
        fsEncoderMap[ueit.data()].push_back(ueit.key());
        ++ueit;
    }
    instance_lock.unlock();

    vector<FileSystemInfo>::iterator fsit;
    for (fsit = fsInfos.begin(); fsit != fsInfos.end(); fsit++)
    {
        if (fsMap.contains(fsit->fsID))
            continue;

        fsMap[fsit->fsID] = 0;
        size_t thisKBperMin = 0;

        // append unkown recordings to all fsIDs
        vector<int>::iterator unknownfs_it = fsEncoderMap[-1].begin();
        for (; unknownfs_it != fsEncoderMap[-1].end(); ++unknownfs_it)
            fsEncoderMap[fsit->fsID].push_back(*unknownfs_it);

        if (fsEncoderMap.contains(fsit->fsID))
        {
            VERBOSE(VB_FILE, QString(
                "fsID #%1: Total: %2 GB   Used: %3 GB   Free: %4 GB")
                .arg(fsit->fsID)
                .arg(fsit->totalSpaceKB / 1024.0 / 1024.0, 7, 'f', 1)
                .arg(fsit->usedSpaceKB / 1024.0 / 1024.0, 7, 'f', 1)
                .arg(fsit->freeSpaceKB / 1024.0 / 1024.0, 7, 'f', 1));


            vector<int>::iterator encit = fsEncoderMap[fsit->fsID].begin();
            for (; encit != fsEncoderMap[fsit->fsID].end(); ++encit)
            {
                EncoderLink *enc = *(encoderList->find(*encit));

                if (!enc->IsConnected() || !enc->IsBusy())
                {
                    // remove the encoder since it can't write to any file system
                    VERBOSE(VB_FILE, LOC
                            + QString("Cardid %1: is not recoding, removing it "
                                      "from used list.").arg(*encit));
                    instance_lock.lock();
                    used_encoders.erase(*encit);
                    instance_lock.unlock();
                    continue;
                }

                long long maxBitrate = enc->GetMaxBitrate();
                if (maxBitrate<=0)
                    maxBitrate = 19500000LL;
                thisKBperMin += (((size_t)maxBitrate)*((size_t)15))>>11;
                VERBOSE(VB_FILE, QString("    Cardid %1: max bitrate "
                        "%2 Kb/sec, fsID %3 max is now %4 KB/min")
                        .arg(enc->GetCardID())
                        .arg(enc->GetMaxBitrate() >> 10)
                        .arg(fsit->fsID)
                        .arg(thisKBperMin));
            }
        }
        fsMap[fsit->fsID] = thisKBperMin;

        if (thisKBperMin > maxKBperMin)
        {
            VERBOSE(VB_FILE,
                    QString("  Max of %1 KB/min for fsID %2 is higher "
                    "than the existing Max of %3 so we'll use this Max instead")
                    .arg(thisKBperMin).arg(fsit->fsID).arg(maxKBperMin));
            maxKBperMin = thisKBperMin;
        }
    }

    // Determine frequency to run autoexpire so it doesn't have to free
    // too much space
    uint expireFreq = 15;
    if (maxKBperMin > 0)
    {
        expireFreq = SPACE_TOO_BIG_KB / (maxKBperMin + maxKBperMin/3);
        expireFreq = max(3U, min(expireFreq, 15U));
    }

    double expireMinGB = ((maxKBperMin + maxKBperMin/3)
                          * expireFreq + extraKB) >> 20;
    VERBOSE(VB_IMPORTANT, LOC +
            QString("CalcParams(): Max required Free Space: %2 GB w/freq: "
                    "%2 min").arg(expireMinGB, 0, 'f', 1).arg(expireFreq));

    // lock class and save these parameters.
    instance_lock.lock();
    desired_freq = expireFreq;
    // write per file system needed space back, use safety of 33%
    QMap<int, uint64_t>::iterator it = fsMap.begin();
    while (it != fsMap.end())
    {
        desired_space[it.key()] = (it.data() + it.data()/3) * expireFreq + extraKB;
        ++it;
    }
    instance_lock.unlock();
}

/** \fn AutoExpire::RunExpirer()
 *  \brief This contains the main loop for the auto expire process.
 *
 *   Responsible for cleanup of old LiveTV programs as well as deleting as
 *   many expireable recordings as necessary to maintain enough free space
 *   on all directories in MythTV Storage Groups.  The thread deletes short
 *   LiveTV programs every 2 minutes and long LiveTV and regular programs
 *   as needed every "desired_freq" minutes.
 */
void AutoExpire::RunExpirer(void)
{
    QTime timer;
    QDateTime curTime;
    QDateTime next_expire = QDateTime::currentDateTime().addSecs(60);

    // wait a little for main server to come up and things to settle down
    sleep(20);

    timer.start();

    while (expire_thread_running)
    {
        curTime = QDateTime::currentDateTime();
        // recalculate auto expire parametes
        if (curTime >= next_expire)
            CalcParams();

        timer.restart();

        instance_lock.lock();

        UpdateDontExpireSet();

        // Expire Short LiveTV files for this backend every 2 minutes
        if ((curTime.time().minute() % 2) == 0)
            ExpireLiveTV(emShortLiveTVPrograms);

        // Expire normal recordings depending on frequency calculated
        if (curTime >= next_expire)
        {
            VERBOSE(VB_FILE, LOC + "Running now!");
            next_expire =
                QDateTime::currentDateTime().addSecs(desired_freq * 60);

            ExpireLiveTV(emNormalLiveTVPrograms);

            if (gContext->GetNumSetting("DeletedMaxAge", 0))
                ExpireOldDeleted();

            ExpireEpisodesOverMax();

            ExpireRecordings();
        }

        instance_lock.unlock();

        Sleep(60 - (timer.elapsed() / 1000));
    }
} 

/** \fn AutoExpire::Sleep(int sleepTime)
 *  \brief Sleeps for sleepTime minutes; unless the expire thread
 *         is told to quit, then stops sleeping within 5 seconds.
 */
void AutoExpire::Sleep(int sleepTime)
{
    int minSleep = 5, timeExpended = 0;
    while (expire_thread_running && timeExpended < sleepTime)
    {
        if (timeExpended > (sleepTime - minSleep))
            minSleep = sleepTime - timeExpended;
        timeExpended += minSleep - (int)sleep(minSleep);
    }
}

/** \fn AutoExpire::ExpireLiveTV(int type)
 *  \brief This expires LiveTV programs.
 */
void AutoExpire::ExpireLiveTV(int type)
{
    pginfolist_t expireList;

    VERBOSE(VB_FILE, LOC + QString("ExpireLiveTV(%1)").arg(type));
    FillDBOrdered(expireList, type);
    SendDeleteMessages(expireList);
    ClearExpireList(expireList);
}

/** \fn AutoExpire::ExpireOldDeleted(void)
 *  \brief This expires deleted programs older than DeletedMaxAge.
 */
void AutoExpire::ExpireOldDeleted(void)
{
    pginfolist_t expireList;

    VERBOSE(VB_FILE, LOC + QString("ExpireOldDeleted()"));
    FillDBOrdered(expireList, emOldDeletedPrograms);
    SendDeleteMessages(expireList);
    ClearExpireList(expireList);
}

/** \fn AutoExpire::ExpireRecordings()
 *  \brief This expires normal recordings.
 *
 */
void AutoExpire::ExpireRecordings(void)
{
    pginfolist_t expireList;
    pginfolist_t deleteList;
    vector<FileSystemInfo> fsInfos;
    vector<FileSystemInfo>::iterator fsit;

    VERBOSE(VB_FILE, LOC + "ExpireRecordings()");

    GetFilesystemInfos(encoderList, fsInfos);

    if (fsInfos.size() == 0)
    {
        QString msg = "ERROR: Filesystem Info cache is empty, unable to "
                      "determine what Recordings to expire";
        VERBOSE(VB_IMPORTANT, LOC + msg);
        gContext->LogEntry("mythbackend", LP_WARNING,
                           "Autoexpire Recording", msg);

        return;
    }

    FillExpireList(expireList);

    QMap <int, bool> truncateMap;
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT DISTINCT rechost, recdir "
                  "FROM inuseprograms "
                  "WHERE recusage = 'truncatingdelete' "
                   "AND lastupdatetime > DATE_ADD(NOW(), INTERVAL -2 MINUTE);");

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            QString rechost = query.value(0).toString();
            QString recdir  = query.value(1).toString();

            VERBOSE(VB_FILE, LOC + QString(
                    "%1:%2 has an in-progress truncating delete.")
                    .arg(rechost).arg(recdir));

            for (fsit = fsInfos.begin(); fsit != fsInfos.end(); fsit++)
            {
                if ((fsit->hostname == rechost) &&
                    (fsit->directory == recdir))
                {
                    truncateMap[fsit->fsID] = true;
                    break;
                }
            }
        }
    }

    QMap <int, bool> fsMap;
    for (fsit = fsInfos.begin(); fsit != fsInfos.end(); fsit++)
    {
        if (fsMap.contains(fsit->fsID))
            continue;

        fsMap[fsit->fsID] = true;

        VERBOSE(VB_FILE, QString(
                "fsID #%1: Total: %2 GB   Used: %3 GB   Free: %4 GB")
                .arg(fsit->fsID)
                .arg(fsit->totalSpaceKB / 1024.0 / 1024.0, 7, 'f', 1)
                .arg(fsit->usedSpaceKB / 1024.0 / 1024.0, 7, 'f', 1)
                .arg(fsit->freeSpaceKB / 1024.0 / 1024.0, 7, 'f', 1));

        if (truncateMap.contains(fsit->fsID))
        {
            VERBOSE(VB_FILE, QString(
                "    fsid %1 has a truncating delete in progress,  AutoExpire "
                "can not run for this filesystem until the delete has "
                "finished.  Continuing on to next...").arg(fsit->fsID));
            continue;
        }

        if ((size_t)max(0LL, fsit->freeSpaceKB) < desired_space[fsit->fsID])
        {
            VERBOSE(VB_FILE,
                    QString("    Not Enough Free Space!  We want %1 MB")
                            .arg(desired_space[fsit->fsID] / 1024));

            QMap<QString, int> dirList;
            vector<FileSystemInfo>::iterator fsit2;

            VERBOSE(VB_FILE, QString("    Directories on filesystem ID %1:")
                    .arg(fsit->fsID));

            for (fsit2 = fsInfos.begin(); fsit2 != fsInfos.end(); fsit2++)
            {
                if (fsit2->fsID == fsit->fsID)
                {
                    VERBOSE(VB_FILE, QString("        %1:%2")
                            .arg(fsit2->hostname).arg(fsit2->directory));
                    dirList[fsit2->hostname + ":" + fsit2->directory] = 1;
                }
            }

            VERBOSE(VB_FILE,
                    "    Searching for expireable files in these directories");
            QString myHostName = gContext->GetHostName();
            pginfolist_t::iterator it = expireList.begin();
            while ((it != expireList.end()) && 
                   ((size_t)max(0LL, fsit->freeSpaceKB) < desired_space[fsit->fsID]))
            {
                ProgramInfo *p = *it;
                it++;

                VERBOSE(VB_FILE, QString("        Checking %1 @ %2 => %3")
                        .arg(p->chanid).arg(p->recstartts.toString(Qt::ISODate))
                        .arg(p->title));

                if (p->pathname.left(1) != "/")
                {
                    bool foundFile = false;
                    QMap<int, EncoderLink *>::Iterator eit =
                         encoderList->begin();
                    while (eit != encoderList->end())
                    {
                        EncoderLink *el = eit.data();
                        eit++;

                        if ((p->hostname == el->GetHostName()) ||
                            ((p->hostname == myHostName) &&
                             (el->IsLocal())))
                        {
                            if (el->IsConnected())
                                foundFile = el->CheckFile(p);

                            eit = encoderList->end();
                        }
                    }

                    if (!foundFile && (p->hostname != myHostName))
                    {
                        // Wasn't found so check locally
                        QString file = GetPlaybackURL(p);
                        
                        if (file.left(1) == "/")
                        {
                            p->pathname = file;
                            p->hostname = myHostName;
                            foundFile = true;
                        }
                    }

                    if (!foundFile)
                    {
                        VERBOSE(VB_FILE, QString("        ERROR: Can't find "
                                "file for %1 @ %2").arg(p->chanid)
                                .arg(p->recstartts.toString(Qt::ISODate)));
                        continue;
                    }
                }

                QFileInfo vidFile(p->pathname);
                if (dirList.contains(p->hostname + ":" + vidFile.dirPath()))
                {
                    fsit->freeSpaceKB += (p->filesize / 1024);
                    deleteList.push_back(p);

                    VERBOSE(VB_FILE, QString("        FOUND Expireable file. "
                            "%1 @ %2 is located at %3 which is on fsID #%4. "
                            "Adding to deleteList.  After deleting we should "
                            "have %5 MB free on this filesystem.")
                            .arg(p->chanid)
                            .arg(p->recstartts.toString(Qt::ISODate))
                            .arg(p->pathname).arg(fsit->fsID)
                            .arg(fsit->freeSpaceKB / 1024));
                }
            }
        }
    }

    SendDeleteMessages(deleteList);

    ClearExpireList(deleteList, false);
    ClearExpireList(expireList);
}

/**
 *  \brief This sends delete message to main event thread.
 */
void AutoExpire::SendDeleteMessages(pginfolist_t &deleteList)
{
    QString msg;

    if (deleteList.size() == 0)
    {
        VERBOSE(VB_FILE, LOC + "SendDeleteMessages. Nothing to expire.");
        return;
    }

    VERBOSE(VB_FILE, LOC + "SendDeleteMessages, cycling through deleteList.");
    pginfolist_t::iterator it = deleteList.begin();
    while (it != deleteList.end())
    {
        QString titlestr = (*it)->title;
        if (!(*it)->subtitle.isEmpty())
            titlestr += " \"" + (*it)->subtitle + "\"";
        msg = QString("Expiring %1 MBytes for %2 @ %3 => %4")
            .arg((int)((*it)->filesize >> 20))
            .arg((*it)->chanid).arg((*it)->startts.toString())
            .arg(titlestr);

        if (print_verbose_messages & VB_IMPORTANT)
            VERBOSE(VB_IMPORTANT, msg);
        else
            VERBOSE(VB_FILE, QString("    ") +  msg);

        gContext->LogEntry("autoexpire", LP_NOTICE,
                           "Expiring Program", msg);                

        // send auto expire message to backend's event thread.
        MythEvent me(QString("AUTO_EXPIRE %1 %2").arg((*it)->chanid)
                     .arg((*it)->recstartts.toString(Qt::ISODate)));
        gContext->dispatch(me);

        ++it; // move on to next program
    }
}

/** \fn AutoExpire::ExpirerThread(void *)
 *  \brief This calls RunExpirer() from within a new pthread.
 */
void *AutoExpire::ExpirerThread(void *param)
{
    AutoExpire *expirer = (AutoExpire *)param;
    expirer->RunExpirer();
 
    return NULL;
}

/** \fn AutoExpire::ExpireEpisodesOverMax()
 *  \brief This deletes programs exceeding the maximum
 *         number of episodes of that program desired.
 *         Excludes recordings in the LiveTV Recording Group.
 */
void AutoExpire::ExpireEpisodesOverMax(void)
{
    QMap<QString, int> maxEpisodes;
    QMap<QString, int>::Iterator maxIter;
    QMap<QString, int> episodeParts;
    QString episodeKey;

    QString fileprefix = gContext->GetFilePrefix();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT recordid, maxepisodes, title "
                  "FROM record WHERE maxepisodes > 0 "
                  "ORDER BY recordid ASC, maxepisodes DESC");

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        VERBOSE(VB_FILE, LOC + QString("Found %1 record profiles using max "
                                 "episode expiration")
                                 .arg(query.size()));
        while (query.next())
        {
            VERBOSE(VB_FILE, QString("    %1 (%2 for rec id %3)")
                                     .arg(query.value(2).toString())
                                     .arg(query.value(1).toInt())
                                     .arg(query.value(0).toInt()));
            maxEpisodes[query.value(0).toString()] = query.value(1).toInt();
        }
    }

    VERBOSE(VB_FILE, LOC + "Checking episode count for each recording "
                           "profile using max episodes");
    for (maxIter = maxEpisodes.begin(); maxIter != maxEpisodes.end(); maxIter++)
    {
        query.prepare("SELECT chanid, starttime, title, progstart, progend, "
                          "filesize, duplicate "
                      "FROM recorded "
                      "WHERE recordid = :RECID AND preserve = 0 "
                      "AND recgroup <> 'LiveTV' "
                      "ORDER BY starttime DESC;");
        query.bindValue(":RECID", maxIter.key());

        if (!query.exec() || !query.isActive())
        {
            MythContext::DBError("AutoExpire query failed!", query);
            continue;
        }

        VERBOSE(VB_FILE, QString("    Recordid %1 has %2 recordings.")
                                 .arg(maxIter.key())
                                 .arg(query.size()));
        if (query.size() > 0)
        {
            int found = 1;
            while (query.next())
            {
                QString chanid = query.value(0).toString();
                QDateTime startts = query.value(1).toDateTime();
                QString title = QString::fromUtf8(query.value(2).toString());
                QDateTime progstart = query.value(3).toDateTime();
                QDateTime progend = query.value(4).toDateTime();
                int duplicate = query.value(6).toInt();

                episodeKey = QString("%1_%2_%3")
                             .arg(chanid)
                             .arg(progstart.toString(Qt::ISODate))
                             .arg(progend.toString(Qt::ISODate));

                if ((!IsInDontExpireSet(chanid, startts)) && 
                    (!episodeParts.contains(episodeKey)) &&
                    (found > maxIter.data()))
                {
                    long long spaceFreed =
                        stringToLongLong(query.value(5).toString()) >> 20;
                    QString msg =
                        QString("Expiring %1 MBytes for %2 @ %3 => %4.  Too "
                                "many episodes, we only want to keep %5.")
                            .arg(spaceFreed)
                            .arg(chanid).arg(startts.toString())
                            .arg(title).arg(maxIter.data());

                    if (print_verbose_messages & VB_IMPORTANT)
                        VERBOSE(VB_IMPORTANT, msg);
                    else
                        VERBOSE(VB_FILE, QString("    ") +  msg);

                    gContext->LogEntry("autoexpire", LP_NOTICE,
                                       "Expired program", msg);

                    msg = QString("AUTO_EXPIRE %1 %2")
                                  .arg(chanid)
                                  .arg(startts.toString(Qt::ISODate));

                    MythEvent me(msg);
                    gContext->dispatchNow(me);
                }
                else
                {
                    // keep track of shows we haven't expired so we can
                    // make sure we don't expire another part of the same
                    // episode.
                    if (episodeParts.contains(episodeKey))
                    {
                        episodeParts[episodeKey] = episodeParts[episodeKey] + 1;
                    }
                    else
                    {
                        episodeParts[episodeKey] = 1;
                        if( duplicate )
                            found++;
                    }
                }
            }
        }
    }
}

/** \fn AutoExpire::FillExpireList(pginfolist_t&)
 *  \brief Uses the "AutoExpireMethod" setting in the database to
 *         fill the list of files that are deletable.
 */
void AutoExpire::FillExpireList(pginfolist_t &expireList)
{
    int expMethod = gContext->GetNumSetting("AutoExpireMethod", 1);

    ClearExpireList(expireList);

    FillDBOrdered(expireList, emNormalDeletedPrograms);

    switch(expMethod)
    {
        case emOldestFirst:
        case emLowestPriorityFirst:
        case emWeightedTimePriority:
                FillDBOrdered(expireList, expMethod);
                break;
        // default falls through so list is empty so no AutoExpire
    }
}

/** \fn AutoExpire::PrintExpireList(QString)
 *  \brief Prints a summary of the files that can be deleted.
 */
void AutoExpire::PrintExpireList(QString expHost)
{
    pginfolist_t expireList;

    FillExpireList(expireList);

    cout << "MythTV AutoExpire List ";
    if (expHost != "ALL")
        cout << "for '" << expHost << "' ";
    cout << "(programs listed in order of expiration)\n";

    pginfolist_t::iterator i = expireList.begin();
    for (; i != expireList.end(); i++)
    {
        ProgramInfo *first = (*i);

        if (expHost != "ALL" && first->hostname != expHost)
            continue;

        QString title = first->title;

        if (first->subtitle != "")
            title += ": \"" + first->subtitle + "\"";

        cout << title.local8Bit().leftJustify(39, ' ', true) << " "
             << QString("%1").arg(first->filesize >> 20).local8Bit()
                .rightJustify(5, ' ', true) << "MB  "
             << first->startts.toString().local8Bit().leftJustify(24, ' ', true)
             << " [" << QString("%1").arg(first->recpriority).local8Bit()
                .rightJustify(3, ' ', true) << "]"
             << endl;
    }

    ClearExpireList(expireList);
}

/** \fn AutoExpire::GetAllExpiring(QStringList&)
 *  \brief Gets the full list of programs that can expire in expiration order
 */
void AutoExpire::GetAllExpiring(QStringList &strList)
{
    QMutexLocker lockit(&instance_lock);
    pginfolist_t expireList;

    UpdateDontExpireSet();

    FillDBOrdered(expireList, emShortLiveTVPrograms);
    FillDBOrdered(expireList, emNormalLiveTVPrograms);
    FillDBOrdered(expireList, emNormalDeletedPrograms);
    FillDBOrdered(expireList, gContext->GetNumSetting("AutoExpireMethod",
                  emOldestFirst));

    strList << QString::number(expireList.size());

    pginfolist_t::iterator it = expireList.begin();
    for (; it != expireList.end(); it++)
        (*it)->ToStringList(strList);

    ClearExpireList(expireList);
}

/** \fn AutoExpire::GetAllExpiring(pginfolist_t&)
 *  \brief Gets the full list of programs that can expire in expiration order
 */
void AutoExpire::GetAllExpiring(pginfolist_t &list)
{
    QMutexLocker lockit(&instance_lock);
    pginfolist_t expireList;

    UpdateDontExpireSet();

    FillDBOrdered(expireList, emShortLiveTVPrograms);
    FillDBOrdered(expireList, emNormalLiveTVPrograms);
    FillDBOrdered(expireList, emNormalDeletedPrograms);
    FillDBOrdered(expireList, gContext->GetNumSetting("AutoExpireMethod",
                  emOldestFirst));

    pginfolist_t::iterator it = expireList.begin();
    for (; it != expireList.end(); it++)
        list.push_back( new ProgramInfo( *(*it) ));

    ClearExpireList(expireList);
}

/** \fn AutoExpire::ClearExpireList(pginfolist_t&, bool)
 *  \brief Clears expireList, freeing any ProgramInfo's if necessary.
 */
void AutoExpire::ClearExpireList(pginfolist_t &expireList, bool deleteProg)
{
    ProgramInfo *pginfo = NULL;
    while (expireList.size() > 0)
    {
        if (deleteProg)
            pginfo = expireList.back();

        expireList.pop_back();

        if (deleteProg)
            delete pginfo;
    }
}

/** \fn AutoExpire::FillDBOrdered(pginfolist_t&, int)
 *  \brief Creates a list of programs to delete using the database to 
 *         order list.
 */
void AutoExpire::FillDBOrdered(pginfolist_t &expireList, int expMethod)
{
    QString where;
    QString orderby;
    QString msg;
    int maxAge;

    switch (expMethod)
    {
        default:
        case emOldestFirst:
            msg = "Adding expirable programs in Oldest First order";
            where = "autoexpire > 0";
            if (gContext->GetNumSetting("AutoExpireWatchedPriority", 0))
                orderby = "recorded.watched DESC, ";
            orderby += "starttime ASC";
            break;
        case emLowestPriorityFirst:
            msg = "Adding expirable programs in Lowest Priority First order";
            where = "autoexpire > 0";
            if (gContext->GetNumSetting("AutoExpireWatchedPriority", 0))
                orderby = "recorded.watched DESC, ";
            orderby += "recorded.recpriority ASC, starttime ASC";
            break;
        case emWeightedTimePriority:
            msg = "Adding expirable programs in Weighted Time Priority order";
            where = "autoexpire > 0";
            if (gContext->GetNumSetting("AutoExpireWatchedPriority", 0))
                orderby = "recorded.watched DESC, ";
            orderby += QString("DATE_ADD(starttime, INTERVAL '%1' * "
                                        "recorded.recpriority DAY) ASC")
                      .arg(gContext->GetNumSetting("AutoExpireDayPriority", 3));
            break;
        case emShortLiveTVPrograms:
            msg = "Adding Short LiveTV programs in starttime order";
            where = "recgroup = 'LiveTV' "
                    "AND endtime < DATE_ADD(starttime, INTERVAL '2' MINUTE) "
                    "AND endtime <= DATE_ADD(NOW(), INTERVAL '-1' MINUTE) ";
            orderby = "starttime ASC";
            break;
        case emNormalLiveTVPrograms:
            msg = "Adding LiveTV programs in starttime order";
            where = QString("recgroup = 'LiveTV' "
                    "AND endtime <= DATE_ADD(NOW(), INTERVAL '-%1' DAY) ")
                    .arg(gContext->GetNumSetting("AutoExpireLiveTVMaxAge", 1));
            orderby = "starttime ASC";
            break;
        case emOldDeletedPrograms:
            if ((maxAge = gContext->GetNumSetting("DeletedMaxAge", 0)) == 0)
                return;
            msg = QString("Adding programs deleted more than %1 days ago")
                          .arg(maxAge);
            where = QString("recgroup = 'Deleted' "
                    "AND lastmodified <= DATE_ADD(NOW(), INTERVAL '-%1' DAY) ")
                    .arg(maxAge);
            orderby = "starttime ASC";
            break;
        case emNormalDeletedPrograms:
            if (gContext->GetNumSetting("DeletedFifoOrder", 0) == 0)
                return;
            msg = "Adding deleted programs in FIFO order";
            where = "recgroup = 'Deleted'";
            orderby = "lastmodified ASC";
            break;
    }

    VERBOSE(VB_FILE, LOC + "FillDBOrdered: " + msg);

    MSqlQuery query(MSqlQuery::InitCon());
    QString querystr = QString(
               "SELECT recorded.chanid, starttime,   endtime,     "
               "       title,           subtitle,    description, "
               "       hostname,        channum,     name,        "
               "       callsign,        seriesid,    programid,   "
               "       recorded.recpriority,         progstart,   "
               "       progend,         filesize,    recgroup,    "
               "       storagegroup,    basename "
               "FROM recorded "
               "LEFT JOIN channel ON recorded.chanid = channel.chanid "
               "WHERE %1 AND deletepending = 0 "
               "ORDER BY autoexpire DESC, %2").arg(where).arg(orderby);

    query.prepare(querystr);

    if (!query.exec() || !query.isActive() || !query.size())
        return;

    while (query.next())
    {
        QString m_chanid = query.value(0).toString();
        QDateTime m_recstartts = query.value(1).toDateTime();

        if (IsInDontExpireSet(m_chanid, m_recstartts))
        {
            VERBOSE(VB_FILE, LOC + QString("    Skipping "
                             "%1 @ %2 because it is in Don't Expire List")
                             .arg(m_chanid).arg(m_recstartts.toString()));
            continue;
        }
        else if (IsInExpireList(expireList, m_chanid, m_recstartts))
        {
            VERBOSE(VB_FILE, LOC + QString("    Skipping "
                             "%1 @ %2 because it is already in Expire List")
                             .arg(m_chanid) .arg(m_recstartts.toString()));
            continue;
        }

        ProgramInfo *proginfo = new ProgramInfo;

        proginfo->chanid = m_chanid;
        proginfo->startts = query.value(13).toDateTime();
        proginfo->endts = query.value(14).toDateTime();
        proginfo->recstartts = m_recstartts;
        proginfo->recendts = query.value(2).toDateTime();
        proginfo->title = QString::fromUtf8(query.value(3).toString());
        proginfo->subtitle = QString::fromUtf8(query.value(4).toString());
        proginfo->description = QString::fromUtf8(query.value(5).toString());
        proginfo->hostname = query.value(6).toString();

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

        proginfo->seriesid = query.value(10).toString();
        proginfo->programid = query.value(11).toString();
        proginfo->recpriority = query.value(12).toInt();
        proginfo->filesize = stringToLongLong(query.value(15).toString());
        proginfo->recgroup = QString::fromUtf8(query.value(16).toString());
        proginfo->storagegroup = QString::fromUtf8(query.value(17).toString());
        proginfo->pathname = query.value(18).toString();

        VERBOSE(VB_FILE, LOC + QString("    Adding   "
                                       "%1 @ %2")
                                       .arg(proginfo->chanid)
                                       .arg(proginfo->recstartts.toString()));
        expireList.push_back(proginfo);
    }
}

/** \fn SpawnUpdateThread(void*)
 *  \brief This is used by Update(QMap<int, EncoderLink*> *, bool)
 *         to run CalcParams(vector<EncoderLink*>).
 *
 *  \param autoExpireInstance AutoExpire instance on which to call CalcParams.
 */
void *SpawnUpdateThread(void *autoExpireInstance)
{
    sleep(5);
    AutoExpire *ae = (AutoExpire*) autoExpireInstance;
    ae->CalcParams();
    ae->instance_lock.lock();
    ae->update_pending = false;
    ae->instance_cond.wakeAll();
    ae->instance_lock.unlock();
    return NULL;
}

/**
 *  \brief This is used to update the global AutoExpire instance "expirer".
 *
 *  \param encoder     This recorder starts a recording now
 *  \param fsID        file system ID of the writing directory
 *  \param immediately If true CalcParams() is called directly.
 *                     If false, a thread is spawned to call CalcParams(),
 *                     this is for use in the MainServer event thread
 *                     where calling CalcParams() directly
 *                     would deadlock the event thread.
 */
void AutoExpire::Update(int encoder, int fsID, bool immediately)
{
    if (!expirer)
        return;

    // make sure there is only one update pending
    expirer->instance_lock.lock();
    while (expirer->update_pending)
        expirer->instance_cond.wait(&expirer->instance_lock);
    expirer->update_pending = true;

    if (encoder > 0)
    {
        QString msg = QString("Cardid %1: is starting a recording on").arg(encoder);
        if (fsID == -1)
            msg.append(" an unknown fsID soon.");
        else
            msg.append(QString(" fsID %2 soon.").arg(fsID));

        VERBOSE(VB_FILE, LOC + msg);
        expirer->used_encoders[encoder] = fsID;
    }

    expirer->instance_lock.unlock();

    // do it..
    if (immediately)
    {
        expirer->CalcParams();
        expirer->instance_lock.lock();
        expirer->update_pending = false;
        expirer->instance_cond.wakeAll();
        expirer->instance_lock.unlock();
    }
    else
    {
        // create thread to do work
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&expirer->update_thread, &attr,
                       SpawnUpdateThread, expirer);
        pthread_attr_destroy(&attr);
    }
}

void AutoExpire::UpdateDontExpireSet(void)
{
    dont_expire_set.clear();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT chanid, starttime, lastupdatetime, recusage, "
                  " hostname "
                  "FROM inuseprograms;");

    if (!query.exec() || !query.isActive() || !query.size())
        return;

    QDateTime curTime = QDateTime::currentDateTime();

    VERBOSE(VB_FILE, LOC + "Adding Programs to 'Do Not Expire' List");
    while (query.next())
    {
        QString chanid = query.value(0).toString();
        QDateTime startts = query.value(1).toDateTime();
        QDateTime lastupdate = query.value(2).toDateTime();

        if (lastupdate.secsTo(curTime) < 2 * 60 * 60)
        {
            QString key = chanid + startts.toString(Qt::ISODate);
            dont_expire_set.insert(key);
            VERBOSE(VB_FILE, QString("    %1 @ %2 in use by %3 on %4")
                                     .arg(chanid)
                                     .arg(startts.toString(Qt::ISODate))
                                     .arg(query.value(3).toString())
                                     .arg(query.value(4).toString()));
        }
    }
}

bool AutoExpire::IsInDontExpireSet(QString chanid, QDateTime starttime)
{
    QString key = chanid + starttime.toString(Qt::ISODate);

    return (dont_expire_set.count(key));
}

bool AutoExpire::IsInExpireList(pginfolist_t &expireList, QString chanid,
                                QDateTime starttime)
{
    pginfolist_t::iterator it;
    
    for (it = expireList.begin(); it != expireList.end(); ++it)
    {
        if (((*it)->chanid == chanid) &&
            ((*it)->recstartts == starttime))
            return true;
    }
    return false;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
