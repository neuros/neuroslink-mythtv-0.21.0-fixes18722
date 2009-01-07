#include <unistd.h>
#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <qregexp.h>
#include <qstring.h>
#include <qdatetime.h>

#include <iostream>
#include <algorithm>
using namespace std;

#ifdef __linux__
#  include <sys/vfs.h>
#else // if !__linux__
#  include <sys/param.h>
#  ifndef USING_MINGW
#    include <sys/mount.h>
#  endif // USING_MINGW
#endif // !__linux__

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "scheduler.h"
#include "encoderlink.h"
#include "mainserver.h"
#include "remoteutil.h"
#include "backendutil.h"
#include "libmyth/util.h"
#include "libmyth/exitcodes.h"
#include "libmyth/mythcontext.h"
#include "libmyth/mythdbcon.h"
#include "libmyth/compat.h"
#include "libmyth/storagegroup.h"
#include "libmythtv/programinfo.h"
#include "libmythtv/scheduledrecording.h"
#include "libmythtv/cardutil.h"

#define LOC QString("Scheduler: ")
#define LOC_ERR QString("Scheduler, Error: ")

Scheduler::Scheduler(bool runthread, QMap<int, EncoderLink *> *tvList,
                     QString tmptable, Scheduler *master_sched) :
    livetvTime(QDateTime())
{
    m_tvList = tvList;
    specsched = false;
    schedulingEnabled = true;

    expirer = NULL;

    if (master_sched)
    {
        specsched = true;
        master_sched->getAllPending(&reclist);
    }

    // Only the master scheduler should use SchedCon()
    if (runthread)
        dbConn = MSqlQuery::SchedCon();
    else
        dbConn = MSqlQuery::DDCon();

    recordTable = tmptable;
    priorityTable = "powerpriority";

    if (tmptable == "powerpriority_tmp")
    {
        priorityTable = tmptable;
        recordTable = "record";
    }

    m_mainServer = NULL;

    m_isShuttingDown = false;
    resetIdleTime = false;

    verifyCards();

    threadrunning = runthread;

    fsInfoCacheFillTime = QDateTime::currentDateTime().addSecs(-1000);

    reclist_lock = new QMutex(true);

    if (runthread)
    {
        int err = pthread_create(&schedThread, NULL, SchedulerThread, this);
        if (err != 0)
        {
            VERBOSE(VB_IMPORTANT, 
                    QString("Failed to start scheduler thread: error %1")
                    .arg(err));
            threadrunning = false;
        }
    }
}

Scheduler::~Scheduler()
{
    while (reclist.size() > 0)
    {
        ProgramInfo *pginfo = reclist.back();
        delete pginfo;
        reclist.pop_back();
    }

    while (worklist.size() > 0)
    {
        ProgramInfo *pginfo = worklist.back();
        delete pginfo;
        worklist.pop_back();
    }

    if (threadrunning)
    {
        pthread_cancel(schedThread);
        pthread_join(schedThread, NULL);
    }

    delete reclist_lock;
}

void Scheduler::SetMainServer(MainServer *ms)
{
    m_mainServer = ms;
}

void Scheduler::ResetIdleTime(void)
{
    resetIdleTime_lock.lock();
    resetIdleTime = true;
    resetIdleTime_lock.unlock();
}

void Scheduler::verifyCards(void)
{
    QString thequery;

    MSqlQuery query(dbConn);
    query.prepare("SELECT NULL FROM capturecard;");

    int numcards = -1;
    if (query.exec() && query.isActive())
        numcards = query.size();

    if (numcards <= 0)
    {
        cerr << "ERROR: no capture cards are defined in the database.\n";
        cerr << "Perhaps you should read the installation instructions?\n";
        exit(BACKEND_BUGGY_EXIT_NO_CAP_CARD);
    }

    query.prepare("SELECT sourceid,name FROM videosource ORDER BY sourceid;");

    int numsources = -1;
    if (query.exec() && query.isActive())
    {
        numsources = query.size();

        int source = 0;

        while (query.next())
        {
            source = query.value(0).toInt();
            MSqlQuery subquery(dbConn);

            subquery.prepare("SELECT cardinputid FROM cardinput WHERE "
                             "sourceid = :SOURCEID ORDER BY cardinputid;");
            subquery.bindValue(":SOURCEID", source);
            subquery.exec();
            
            if (!subquery.isActive() || subquery.size() <= 0)
                cerr << query.value(1).toString() << " is defined, but isn't "
                     << "attached to a cardinput.\n";
        }
    }

    if (numsources <= 0)
    {
        VERBOSE(VB_IMPORTANT, "ERROR: No channel sources "
                "defined in the database");
        exit(BACKEND_BUGGY_EXIT_NO_CHAN_DATA);
    }
}

static inline bool Recording(const ProgramInfo *p)
{
    return (p->recstatus == rsRecording || p->recstatus == rsWillRecord);
}

static bool comp_overlap(ProgramInfo *a, ProgramInfo *b)
{
    if (a->startts != b->startts)
        return a->startts < b->startts;
    if (a->endts != b->endts)
        return a->endts < b->endts;

    // Note: the PruneOverlaps logic depends on the following
    if (a->title != b->title)
        return a->title < b->title;
    if (a->chanid != b->chanid)
        return a->chanid < b->chanid;
    if (a->inputid != b->inputid)
        return a->inputid < b->inputid;

    // In cases where two recording rules match the same showing, one
    // of them needs to take precedence.  Penalize any entry that
    // won't record except for those from kDontRecord rules.  This
    // will force them to yield to a rule that might record.
    // Otherwise, more specific record type beats less specific.
    int apri = RecTypePriority(a->rectype);
    if (a->recstatus != rsUnknown && a->recstatus != rsDontRecord)
        apri += 100;
    int bpri = RecTypePriority(b->rectype);
    if (b->recstatus != rsUnknown && b->recstatus != rsDontRecord)
        bpri += 100;
    if (apri != bpri)
        return apri < bpri;

    if (a->findid != b->findid)
        return a->findid > b->findid;
    return a->recordid < b->recordid;
}

static bool comp_redundant(ProgramInfo *a, ProgramInfo *b)
{
    if (a->startts != b->startts)
        return a->startts < b->startts;
    if (a->endts != b->endts)
        return a->endts < b->endts;

    // Note: the PruneRedundants logic depends on the following
    if (a->title != b->title)
        return a->title < b->title;
    if (a->recordid != b->recordid)
        return a->recordid < b->recordid;
    if (a->chansign != b->chansign)
        return a->chansign < b->chansign;
    return a->recstatus < b->recstatus;
}

static bool comp_recstart(ProgramInfo *a, ProgramInfo *b)
{
    if (a->recstartts != b->recstartts)
        return a->recstartts < b->recstartts;
    if (a->recendts != b->recendts)
        return a->recendts < b->recendts;
    if (a->chansign != b->chansign)
        return a->chansign < b->chansign;
    return a->recstatus < b->recstatus;
}

static QDateTime schedTime;

static bool comp_priority(ProgramInfo *a, ProgramInfo *b)
{
    int arec = (a->recstatus != rsRecording);
    int brec = (b->recstatus != rsRecording);

    if (arec != brec)
        return arec < brec;

    if (a->recpriority != b->recpriority)
        return a->recpriority > b->recpriority;

    if (a->recpriority2 != b->recpriority2)
        return a->recpriority2 > b->recpriority2;

    int apast = (a->recstartts < schedTime.addSecs(-30) && !a->reactivate);
    int bpast = (b->recstartts < schedTime.addSecs(-30) && !b->reactivate);

    if (apast != bpast)
        return apast < bpast;

    int apri = RecTypePriority(a->rectype);
    int bpri = RecTypePriority(b->rectype);

    if (apri != bpri)
        return apri < bpri;

    if (a->recstartts != b->recstartts)
    {
        if (apast)
            return a->recstartts > b->recstartts;
        else
            return a->recstartts < b->recstartts;
    }

    if (a->inputid != b->inputid)
        return a->inputid < b->inputid;

    return a->recordid < b->recordid;
}

static bool comp_timechannel(ProgramInfo *a, ProgramInfo *b)
{
    if (a->recstartts != b->recstartts)
        return a->recstartts < b->recstartts;
    if (a->chanstr == b->chanstr)
        return a->chanid < b->chanid;
    if (a->chanstr.toInt() > 0 && b->chanstr.toInt() > 0)
        return a->chanstr.toInt() < b->chanstr.toInt();
    return a->chanstr < b->chanstr;
}

bool Scheduler::FillRecordList(void)
{
    schedMoveHigher = (bool)gContext->GetNumSetting("SchedMoveHigher");
    schedTime = QDateTime::currentDateTime();

    VERBOSE(VB_SCHEDULE, "BuildWorkList...");
    BuildWorkList();
    VERBOSE(VB_SCHEDULE, "AddNewRecords...");
    AddNewRecords();
    VERBOSE(VB_SCHEDULE, "AddNotListed...");
    AddNotListed();

    VERBOSE(VB_SCHEDULE, "Sort by time...");
    SORT_RECLIST(worklist, comp_overlap);
    VERBOSE(VB_SCHEDULE, "PruneOverlaps...");
    PruneOverlaps();

    VERBOSE(VB_SCHEDULE, "Sort by priority...");
    SORT_RECLIST(worklist, comp_priority);
    VERBOSE(VB_SCHEDULE, "BuildListMaps...");
    BuildListMaps();
    VERBOSE(VB_SCHEDULE, "SchedNewRecords...");
    SchedNewRecords();
    VERBOSE(VB_SCHEDULE, "SchedPreserveLiveTV...");
    SchedPreserveLiveTV();
    VERBOSE(VB_SCHEDULE, "ClearListMaps...");
    ClearListMaps();

    VERBOSE(VB_SCHEDULE, "Sort by time...");
    SORT_RECLIST(worklist, comp_redundant);
    VERBOSE(VB_SCHEDULE, "PruneRedundants...");
    PruneRedundants();

    VERBOSE(VB_SCHEDULE, "Sort by time...");
    SORT_RECLIST(worklist, comp_recstart);
    VERBOSE(VB_SCHEDULE, "ClearWorkList...");
    bool res = ClearWorkList();

    return res;
}

/** \fn Scheduler::FillRecordListFromDB(int)
 *  \param recordid Record ID of recording that has changed,
 *                  or -1 if anything might have been changed.
 */
void Scheduler::FillRecordListFromDB(int recordid)
{
    struct timeval fillstart, fillend;
    float matchTime, placeTime;

    MSqlQuery query(dbConn);
    QString thequery;
    QString where = "";

    // This will cause our temp copy of recordmatch to be empty
    if (recordid == -1)
        where = "WHERE recordid IS NULL ";

    thequery = QString("CREATE TEMPORARY TABLE recordmatch ") +
                           "SELECT * FROM recordmatch " + where + "; ";

    query.prepare(thequery);
    recordmatchLock.lock();
    query.exec();
    recordmatchLock.unlock();
    if (!query.isActive())
    {
        MythContext::DBError("FillRecordListFromDB", query);
        return;
    }

    thequery = "ALTER TABLE recordmatch ADD INDEX (recordid);";
    query.prepare(thequery);
    query.exec();
    if (!query.isActive())
    {
        MythContext::DBError("FillRecordListFromDB", query);
        return;
    }

    gettimeofday(&fillstart, NULL);
    UpdateMatches(recordid);
    gettimeofday(&fillend, NULL);
    matchTime = ((fillend.tv_sec - fillstart.tv_sec ) * 1000000 +
                 (fillend.tv_usec - fillstart.tv_usec)) / 1000000.0;

    gettimeofday(&fillstart, NULL);
    FillRecordList();
    gettimeofday(&fillend, NULL);
    placeTime = ((fillend.tv_sec - fillstart.tv_sec ) * 1000000 +
                 (fillend.tv_usec - fillstart.tv_usec)) / 1000000.0;

    MSqlQuery queryDrop(dbConn);
    queryDrop.prepare("DROP TABLE recordmatch;");
    queryDrop.exec();
    if (!queryDrop.isActive())
    {
        MythContext::DBError("FillRecordListFromDB", queryDrop);
        return;
    }

    QString msg;
    msg.sprintf("Speculative scheduled %d items in "
                "%.1f = %.2f match + %.2f place", (int)reclist.size(),
                matchTime + placeTime, matchTime, placeTime);
    VERBOSE(VB_GENERAL, msg);
}

void Scheduler::FillRecordListFromMaster(void)
{
    ProgramList schedList(false);
    schedList.FromScheduler();

    QMutexLocker lockit(reclist_lock);

    ProgramInfo *p;
    for (p = schedList.first(); p; p = schedList.next())
        reclist.push_back(p);
}

void Scheduler::PrintList(RecList &list, bool onlyFutureRecordings)
{
    if ((print_verbose_messages & VB_SCHEDULE) == 0)
        return;

    QDateTime now = QDateTime::currentDateTime();

    cout << "--- print list start ---\n";
    cout << "Title - Subtitle                Ch Station "
        "Day Start  End   S C I  T N   Pri" << endl;

    RecIter i = list.begin();
    for ( ; i != list.end(); i++)
    {
        ProgramInfo *first = (*i);

        if (onlyFutureRecordings &&
            ((first->recendts < now && first->endts < now) ||
             (first->recstartts < now && !Recording(first))))
            continue;

        PrintRec(first);
    }

    cout << "---  print list end  ---\n";
}

void Scheduler::PrintRec(const ProgramInfo *p, const char *prefix)
{
    if ((print_verbose_messages & VB_SCHEDULE) == 0)
        return;

    QString episode;

    if (prefix)
        cout << prefix;

    if (p->subtitle > " ")
        episode = QString("%1 - \"%2\"").arg(p->title.local8Bit())
            .arg(p->subtitle.local8Bit());
    else
        episode = p->title.local8Bit();

    cout << episode.leftJustify(30, ' ', true) << " "
         << p->chanstr.rightJustify(4, ' ') << " " 
         << p->chansign.leftJustify(7, ' ', true) << " "
         << p->recstartts.toString("dd hh:mm-").local8Bit()
         << p->recendts.toString("hh:mm  ").local8Bit()
         << p->sourceid << " " << p->cardid << " " << p->inputid << "  " 
         << p->RecTypeChar() << " " << p->RecStatusChar() << " "
         << (QString::number(p->recpriority) + "/" + 
             QString::number(p->recpriority2)).rightJustify(5, ' ')
         << endl;
}

void Scheduler::UpdateRecStatus(ProgramInfo *pginfo)
{
    QMutexLocker lockit(reclist_lock);

    RecIter dreciter = reclist.begin();
    for (; dreciter != reclist.end(); ++dreciter)
    {
        ProgramInfo *p = *dreciter;
        if (p->IsSameProgramTimeslot(*pginfo))
        {
            if (p->recstatus != pginfo->recstatus)
            {
                p->recstatus = pginfo->recstatus;
                reclist_changed = true;
                p->AddHistory(true);
            }
            return;
        }
    }
}

void Scheduler::UpdateRecStatus(int cardid, const QString &chanid, 
                                const QDateTime &startts, 
                                RecStatusType recstatus, 
                                const QDateTime &recendts)
{
    QMutexLocker lockit(reclist_lock);

    RecIter dreciter = reclist.begin();
    for (; dreciter != reclist.end(); ++dreciter)
    {
        ProgramInfo *p = *dreciter;
        if (p->cardid == cardid &&
            p->chanid == chanid &&
            p->startts == startts)
        {
            p->recendts = recendts;

            if (p->recstatus != recstatus)
            {
                p->recstatus = recstatus;
                reclist_changed = true;
                p->AddHistory(true);
            }
            return;
        }
    }
}

bool Scheduler::ChangeRecordingEnd(ProgramInfo *oldp, ProgramInfo *newp)
{
    QMutexLocker lockit(reclist_lock);

    if (reclist_changed)
        return false;

    RecordingType oldrectype = oldp->rectype;
    int oldrecordid = oldp->recordid;
    QDateTime oldrecendts = oldp->recendts;

    oldp->rectype = newp->rectype;
    oldp->recordid = newp->recordid;
    oldp->recendts = newp->recendts;

    if (specsched)
    {
        if (newp->recendts < QDateTime::currentDateTime())
        {
            oldp->recstatus = rsRecorded;
            newp->recstatus = rsRecorded;
            return false;
        }
        else
            return true;
    }

    EncoderLink *tv = (*m_tvList)[oldp->cardid];
    RecStatusType rs = tv->StartRecording(oldp);
    if (rs != rsRecording)
    {
        VERBOSE(VB_IMPORTANT, QString("Failed to change end time on "
                                      "card %1 to %2")
                .arg(oldp->cardid).arg(oldp->recendts.toString()));
        oldp->rectype = oldrectype;
        oldp->recordid = oldrecordid;
        oldp->recendts = oldrecendts;
    }
    else
    {
        RecIter i = reclist.begin();
        for (; i != reclist.end(); i++)
        {
            ProgramInfo *recp = *i;
            if (recp->IsSameTimeslot(*oldp))
            {
                *recp = *oldp;
                break;
            }
        }
    }

    return rs == rsRecording;
}

void Scheduler::SlaveConnected(ProgramList &slavelist)
{
    QMutexLocker lockit(reclist_lock);

    ProgramInfo *sp;
    for (sp = slavelist.first(); sp; sp = slavelist.next())
    {
        bool found = false;

        RecIter ri = reclist.begin();
        for ( ; ri != reclist.end(); ri++)
        {
            ProgramInfo *rp = *ri;

            if (sp->inputid &&
                sp->startts == rp->startts &&
                sp->chansign == rp->chansign &&
                sp->title == rp->title)
            {
                if (sp->cardid == rp->cardid)
                {
                    found = true;
                    rp->recstatus = rsRecording;
                    reclist_changed = true;
                    rp->AddHistory(false);
                    VERBOSE(VB_IMPORTANT, QString("setting %1/%2/\"%3\" as "
                                                  "recording")
                            .arg(sp->cardid).arg(sp->chansign).arg(sp->title));
                }
                else
                {
                    VERBOSE(VB_IMPORTANT, QString("%1/%2/\"%3\" is already "
                                                  "recording on card %4")
                            .arg(sp->cardid).arg(sp->chansign).arg(sp->title)
                            .arg(rp->cardid));
                }
            }
            else if (sp->cardid == rp->cardid &&
                     rp->recstatus == rsRecording)
            {
                rp->recstatus = rsAborted;
                reclist_changed = true;
                rp->AddHistory(false);
                VERBOSE(VB_IMPORTANT, QString("setting %1/%2/\"%3\" as aborted")
                        .arg(rp->cardid).arg(rp->chansign).arg(rp->title));
            }
        }

        if (sp->inputid && !found)
        {
            reclist.push_back(new ProgramInfo(*sp));
            reclist_changed = true;
            sp->AddHistory(false);
            VERBOSE(VB_IMPORTANT, QString("adding %1/%2/\"%3\" as recording")
                    .arg(sp->cardid).arg(sp->chansign).arg(sp->title));
        }
    }
}

void Scheduler::SlaveDisconnected(int cardid)
{
    QMutexLocker lockit(reclist_lock);

    RecIter ri = reclist.begin();
    for ( ; ri != reclist.end(); ri++)
    {
        ProgramInfo *rp = *ri;

        if (rp->cardid == cardid &&
            rp->recstatus == rsRecording)
        {
            rp->recstatus = rsAborted;
            reclist_changed = true;
            rp->AddHistory(false);
            VERBOSE(VB_IMPORTANT, QString("setting %1/%2/\"%3\" as aborted")
                    .arg(rp->cardid).arg(rp->chansign).arg(rp->title));
        }
    }
}

void Scheduler::BuildWorkList(void)
{
    QMutexLocker lockit(reclist_lock);
    reclist_changed = false;

    RecIter i = reclist.begin();
    for (; i != reclist.end(); i++)
    {
        ProgramInfo *p = *i;
        if (p->recstatus == rsRecording)
            worklist.push_back(new ProgramInfo(*p));
    }
}

bool Scheduler::ClearWorkList(void)
{
    QMutexLocker lockit(reclist_lock);

    ProgramInfo *p;

    if (reclist_changed)
    {
        while (worklist.size() > 0)
        {
            p = worklist.front();
            delete p;
            worklist.pop_front();
        }

        return false;
    }

    while (reclist.size() > 0)
    {
        p = reclist.front();
        delete p;
        reclist.pop_front();
    }

    while (worklist.size() > 0)
    {
        p = worklist.front();
        reclist.push_back(p);
        worklist.pop_front();
    }

    return true;
}

static void erase_nulls(RecList &reclist)
{
    RecIter it = reclist.begin();
    uint dst = 0;
    for (it = reclist.begin(); it != reclist.end(); ++it)
    {
        if (*it)
        {
            reclist[dst] = *it;
            dst++;
        }
    }
    reclist.resize(dst);
}

void Scheduler::PruneOverlaps(void)
{
    ProgramInfo *lastp = NULL;

    RecIter dreciter = worklist.begin();
    while (dreciter != worklist.end())
    {
        ProgramInfo *p = *dreciter;
        if (lastp == NULL || lastp->recordid == p->recordid ||
            !lastp->IsSameTimeslot(*p))
        {
            lastp = p;
            dreciter++;
        }
        else
        {
            delete p;
            *(dreciter++) = NULL;
        }
    }

    erase_nulls(worklist);
}

void Scheduler::BuildListMaps(void)
{
    RecIter i = worklist.begin();
    for ( ; i != worklist.end(); i++)
    {
        ProgramInfo *p = *i;
        if (p->recstatus == rsRecording || 
            p->recstatus == rsWillRecord ||
            p->recstatus == rsUnknown)
        {
            cardlistmap[p->cardid].push_back(p);
            titlelistmap[p->title].push_back(p);
            recordidlistmap[p->recordid].push_back(p);
        }
    }
}

void Scheduler::ClearListMaps(void)
{
    cardlistmap.clear();
    titlelistmap.clear();
    recordidlistmap.clear();
    cache_is_same_program.clear();
}

bool Scheduler::IsSameProgram(
    const ProgramInfo *a, const ProgramInfo *b) const
{
    IsSameKey X(a,b);
    IsSameCacheType::const_iterator it = cache_is_same_program.find(X);
    if (it != cache_is_same_program.end())
        return *it;

    IsSameKey Y(b,a);
    it = cache_is_same_program.find(Y);
    if (it != cache_is_same_program.end())
        return *it;

    return cache_is_same_program[X] = a->IsSameProgram(*b);
}

bool Scheduler::FindNextConflict(
    const RecList     &cardlist,
    const ProgramInfo *p,
    RecConstIter      &j,
    bool               openEnd) const
{
    bool is_conflict_dbg = false;

    for ( ; j != cardlist.end(); j++)
    {
        const ProgramInfo *q = *j;

        if (p == q)
            continue;

        if (!Recording(q))
            continue;

        if (is_conflict_dbg)
            cout << QString("\n  comparing with '%1' ").arg(q->title);

        if (p->cardid != 0 && (p->cardid != q->cardid) &&
            !igrp.GetSharedInputGroup(p->inputid, q->inputid))
        {
            if (is_conflict_dbg)
                cout << "  cardid== ";
            continue;
        }

        if (openEnd && p->chanid != q->chanid)
        {
            if (p->recendts < q->recstartts || p->recstartts > q->recendts)
            {
                if (is_conflict_dbg)
                    cout << "  no-overlap ";
                continue;
            }
        }
        else
        {
            if (p->recendts <= q->recstartts || p->recstartts >= q->recendts)
            {
                if (is_conflict_dbg)
                    cout << "  no-overlap ";
                continue;
            }
        }

        if (is_conflict_dbg)
            cout << "\n" <<
                QString("  cardid's: %1, %2 ").arg(p->cardid).arg(q->cardid) +
                QString("Shared input group: %1 ")
                .arg(igrp.GetSharedInputGroup(p->inputid, q->inputid)) +
                QString("mplexid's: %1, %2")
                .arg(p->GetMplexID()).arg(q->GetMplexID());

        // if two inputs are in the same input group we have a conflict
        // unless the programs are on the same multiplex.
        if (p->cardid && (p->cardid != q->cardid) &&
            igrp.GetSharedInputGroup(p->inputid, q->inputid) &&
            p->GetMplexID() && (p->GetMplexID() == q->GetMplexID()))
        {
            continue;
        }

        if (is_conflict_dbg)
            cout << "\n  Found conflict" << endl;

        return true;
    }

    if (is_conflict_dbg)
        cout << "\n  No conflict" << endl;

    return false;
}

const ProgramInfo *Scheduler::FindConflict(
    const QMap<int, RecList> &reclists,
    const ProgramInfo        *p,
    bool openend) const
{
    bool is_conflict_dbg = false;

    QMap<int, RecList>::const_iterator it = reclists.begin();
    for (; it != reclists.end(); ++it)
    {
        if (is_conflict_dbg)
        {
            cout << QString("Checking '%1' for conflicts on cardid %2")
                .arg(p->title).arg(it.key());
        }

        const RecList &cardlist = *it;
        RecConstIter k = cardlist.begin();
        if (FindNextConflict(cardlist, p, k, openend))
        {
            return *k;
        }
    }

    return NULL;
}

void Scheduler::MarkOtherShowings(ProgramInfo *p)
{
    RecList *showinglist = &titlelistmap[p->title];

    MarkShowingsList(*showinglist, p);

    if (p->rectype == kFindOneRecord || 
        p->rectype == kFindDailyRecord ||
        p->rectype == kFindWeeklyRecord)
    {
        showinglist = &recordidlistmap[p->recordid];
        MarkShowingsList(*showinglist, p);
    }

    if (p->rectype == kOverrideRecord && p->findid > 0)
    {
        showinglist = &recordidlistmap[p->parentid];
        MarkShowingsList(*showinglist, p);
    }
}

void Scheduler::MarkShowingsList(RecList &showinglist, ProgramInfo *p)
{
    RecIter i = showinglist.begin();
    for ( ; i != showinglist.end(); i++)
    {
        ProgramInfo *q = *i;
        if (q == p)
            continue;
        if (q->recstatus != rsUnknown && 
            q->recstatus != rsWillRecord &&
            q->recstatus != rsEarlierShowing &&
            q->recstatus != rsLaterShowing)
            continue;
        if (q->IsSameTimeslot(*p))
            q->recstatus = rsLaterShowing;
        else if (q->rectype != kSingleRecord && 
                 q->rectype != kOverrideRecord && 
                 IsSameProgram(q,p))
        {
            if (q->recstartts < p->recstartts)
                q->recstatus = rsLaterShowing;
            else
                q->recstatus = rsEarlierShowing;
        }
    }
}

void Scheduler::BackupRecStatus(void)
{
    RecIter i = worklist.begin();
    for ( ; i != worklist.end(); i++)
    {
        ProgramInfo *p = *i;
        p->savedrecstatus = p->recstatus;
    }
}

void Scheduler::RestoreRecStatus(void)
{
    RecIter i = worklist.begin();
    for ( ; i != worklist.end(); i++)
    {
        ProgramInfo *p = *i;
        p->recstatus = p->savedrecstatus;
    }
}

bool Scheduler::TryAnotherShowing(ProgramInfo *p, bool samePriority,
                                   bool preserveLive)
{
    PrintRec(p, "     >");

    if (p->recstatus == rsRecording)
        return false;

    RecList *showinglist = &titlelistmap[p->title];

    if (p->rectype == kFindOneRecord || 
        p->rectype == kFindDailyRecord ||
        p->rectype == kFindWeeklyRecord)
        showinglist = &recordidlistmap[p->recordid];

    RecStatusType oldstatus = p->recstatus;
    p->recstatus = rsLaterShowing;

    bool hasLaterShowing = false;

    RecIter j = showinglist->begin();
    for ( ; j != showinglist->end(); j++)
    {
        ProgramInfo *q = *j;
        if (q == p)
            continue;

        if (samePriority && (q->recpriority != p->recpriority))
            continue;

        hasLaterShowing = false;

        if (q->recstatus != rsEarlierShowing &&
            q->recstatus != rsLaterShowing &&
            q->recstatus != rsUnknown)
            continue;

        if (!p->IsSameTimeslot(*q))
        {
            if (!IsSameProgram(p,q))
                continue;
            if ((p->rectype == kSingleRecord || 
                 p->rectype == kOverrideRecord))
                continue;
            if (q->recstartts < schedTime && p->recstartts >= schedTime)
                continue;

            hasLaterShowing |= preserveLive;
        }
 
        if (samePriority)
            PrintRec(q, "     %");
        else
            PrintRec(q, "     #");

        bool failedLiveCheck = false;
        if (preserveLive)
        {
            failedLiveCheck |=
                (!livetvpriority ||
                 p->recpriority - prefinputpri > q->recpriority);

            // It is pointless to preempt another livetv session.
            // (the retrylist contains dummy livetv pginfo's)
            RecConstIter k = retrylist.begin();
            if (FindNextConflict(retrylist, q, k))
            {
                PrintRec(*k, "       L!");
                continue;
            }
        }

        const ProgramInfo *conflict = FindConflict(cardlistmap, q);
        if (conflict)
        {
            PrintRec(conflict, "        !");
            continue;
        }

        if (hasLaterShowing)
        {
            QString id = p->schedulerid;
            hasLaterList[id] = true;
            continue;
        }

        if (failedLiveCheck)
        {
            // Failed the priority check or "Move scheduled shows to
            // avoid LiveTV feature" is turned off.
            // However, there is no conflict so if this alternate showing
            // is on an equivalent virtual card, allow the move.
            bool equiv = (p->sourceid == q->sourceid &&
                          igrp.GetSharedInputGroup(p->inputid, q->inputid));

            if (!equiv)
                continue;
        }

        if (preserveLive)
        {
            QString msg = QString(
                "Moved \"%1\" on chanid: %2 from card: %3 to %4 "
                "to avoid LiveTV conflict")
                .arg(p->title.local8Bit()).arg(p->chanid)
                .arg(p->cardid).arg(q->cardid);
            VERBOSE(VB_SCHEDULE, msg);
        }

        q->recstatus = rsWillRecord;
        MarkOtherShowings(q);
        PrintRec(p, "     -");
        PrintRec(q, "     +");
        return true;
    }

    p->recstatus = oldstatus;
    return false;
}

void Scheduler::SchedNewRecords(void)
{
    VERBOSE(VB_SCHEDULE, "Scheduling:");

    bool openEnd = (bool)gContext->GetNumSetting("SchedOpenEnd", 0);

    RecIter i = worklist.begin();
    while (i != worklist.end())
    {
        ProgramInfo *p = *i;
        if (p->recstatus == rsRecording)
            MarkOtherShowings(p);
        else if (p->recstatus == rsUnknown)
        {
            const ProgramInfo *conflict = FindConflict(cardlistmap, p, openEnd);
            if (!conflict)
            {
                p->recstatus = rsWillRecord;

                if (p->recstartts < schedTime.addSecs(90))
                {
                    QString id = p->schedulerid;
                    if (!recPendingList.contains(id))
                        recPendingList[id] = false;

                    livetvTime = (livetvTime < schedTime) ?
                        schedTime : livetvTime;
                }
  
                MarkOtherShowings(p);
                PrintRec(p, "  +");
            }
            else
            {
                retrylist.push_front(p);
                PrintRec(p, "  #");
                PrintRec(conflict, "     !");
            }
        }

        int lastpri = p->recpriority;
        i++;
        if (i == worklist.end() || lastpri != (*i)->recpriority)
        {
            MoveHigherRecords();
            retrylist.clear();
        }
    }
}

void Scheduler::MoveHigherRecords(bool move_this)
{
    RecIter i = retrylist.begin();
    for ( ; move_this && i != retrylist.end(); i++)
    {
        ProgramInfo *p = *i;
        if (p->recstatus != rsUnknown)
            continue;

        PrintRec(p, "  /");

        BackupRecStatus();
        p->recstatus = rsWillRecord;
        MarkOtherShowings(p);

        RecList cardlist;
        QMap<int, RecList>::const_iterator it = cardlistmap.begin();
        for (; it != cardlistmap.end(); ++it)
        {
            RecConstIter it2 = (*it).begin();
            for (; it2 != (*it).end(); ++it2)
                cardlist.push_back(*it2);
        }
        RecConstIter k = cardlist.begin();
        for ( ; FindNextConflict(cardlist, p, k ); k++)
        {
            if (p->recpriority != (*k)->recpriority ||
                !TryAnotherShowing(*k, true))
            {
                RestoreRecStatus();
                break;
            }
        }

        if (p->recstatus == rsWillRecord)
            PrintRec(p, "  +");
    }

    i = retrylist.begin();
    for ( ; i != retrylist.end(); i++)
    {
        ProgramInfo *p = *i;
        if (p->recstatus != rsUnknown)
            continue;

        PrintRec(p, "  ?");

        if (move_this && TryAnotherShowing(p, false))
            continue;

        BackupRecStatus();
        p->recstatus = rsWillRecord;
        if (move_this)
            MarkOtherShowings(p);

        RecList cardlist;
        QMap<int, RecList>::const_iterator it = cardlistmap.begin();
        for (; it != cardlistmap.end(); ++it)
        {
            RecConstIter it2 = (*it).begin();
            for (; it2 != (*it).end(); ++it2)
                cardlist.push_back(*it2);
        }
        
        RecConstIter k = cardlist.begin();
        for ( ; FindNextConflict(cardlist, p, k); k++)
        {
            if ((p->recpriority < (*k)->recpriority && !schedMoveHigher &&
                move_this) || !TryAnotherShowing(*k, false, !move_this))
            {
                RestoreRecStatus();
                break;
            }
        }

        if (move_this && p->recstatus == rsWillRecord)
            PrintRec(p, "  +");
    }
}

void Scheduler::PruneRedundants(void)
{
    ProgramInfo *lastp = NULL;

    RecIter i = worklist.begin();
    while (i != worklist.end())
    {
        ProgramInfo *p = *i;

        // Delete anything that has already passed since we can't
        // change history, can we?
        if (p->recstatus != rsRecording &&
            p->endts < schedTime &&
            p->recendts < schedTime)
        {
            delete p;
            *(i++) = NULL;
            continue;
        }

        // Check for rsConflict
        if (p->recstatus == rsUnknown)
            p->recstatus = rsConflict;
        
        // Restore the old status for some select cases that won't record.
        if (p->recstatus != rsWillRecord && 
            p->oldrecstatus != rsUnknown &&
            !p->reactivate)
            p->recstatus = p->oldrecstatus;

        if (!Recording(p))
        {
            p->cardid = 0;
            p->inputid = 0;
        }

        // Check for redundant against last non-deleted
        if (lastp == NULL || lastp->recordid != p->recordid ||
            !lastp->IsSameTimeslot(*p))
        {
            lastp = p;
            i++;
        }
        else
        {
            delete p;
            *(i++) = NULL;
        }
    }

    erase_nulls(worklist);
}

void Scheduler::UpdateNextRecord(void)
{
    if (specsched)
        return;

    QMap<int, QDateTime> nextRecMap;

    RecIter i = reclist.begin();
    while (i != reclist.end())
    {
        ProgramInfo *p = *i;
        if (p->recstatus == rsWillRecord && nextRecMap[p->recordid].isNull())
            nextRecMap[p->recordid] = p->recstartts;

        if (p->rectype == kOverrideRecord && p->parentid > 0 &&
            p->recstatus == rsWillRecord && nextRecMap[p->parentid].isNull())
            nextRecMap[p->parentid] = p->recstartts;
        i++;
    }

    MSqlQuery query(dbConn);
    query.prepare("SELECT recordid, next_record FROM record;");

    if (query.exec() && query.isActive())
    {
        MSqlQuery subquery(dbConn);

        while (query.next())
        {
            int recid = query.value(0).toInt();
            QDateTime next_record = query.value(1).toDateTime();

            if (next_record == nextRecMap[recid])
                continue;

            if (nextRecMap[recid].isNull() && next_record.isValid())
            {
                subquery.prepare("UPDATE record "
                                 "SET next_record = '0000-00-00T00:00:00' "
                                 "WHERE recordid = :RECORDID;");
                subquery.bindValue(":RECORDID", recid);
            }
            else
            {
                subquery.prepare("UPDATE record SET next_record = :NEXTREC "
                                 "WHERE recordid = :RECORDID;");
                subquery.bindValue(":RECORDID", recid);
                subquery.bindValue(":NEXTREC", nextRecMap[recid]);
            }
            subquery.exec();
            if (!subquery.isActive())
                MythContext::DBError("Update next_record", subquery);
            else
                VERBOSE(VB_SCHEDULE, LOC + 
                        QString("Update next_record for %1").arg(recid));
        }
    }
}

void Scheduler::getConflicting(ProgramInfo *pginfo, QStringList &strlist)
{
    RecList retlist;
    getConflicting(pginfo, &retlist);

    strlist << QString::number(retlist.size());

    while (retlist.size() > 0)
    {
        ProgramInfo *p = retlist.front();
        p->ToStringList(strlist);
        delete p;
        retlist.pop_front();
    }
}
 
void Scheduler::getConflicting(ProgramInfo *pginfo, RecList *retlist)
{
    QMutexLocker lockit(reclist_lock);

    RecConstIter i = reclist.begin();
    for (; FindNextConflict(reclist, pginfo, i); i++)
    {
        const ProgramInfo *p = *i;
        retlist->push_back(new ProgramInfo(*p));
    }
}

bool Scheduler::getAllPending(RecList *retList)
{
    QMutexLocker lockit(reclist_lock);

    bool hasconflicts = false;

    RecIter i = reclist.begin();
    for (; i != reclist.end(); i++)
    {
        ProgramInfo *p = *i;
        if (p->recstatus == rsConflict)
            hasconflicts = true;
        retList->push_back(new ProgramInfo(*p));
    }

    SORT_RECLIST(*retList, comp_timechannel);

    return hasconflicts;
}

void Scheduler::getAllPending(QStringList &strList)
{
    RecList retlist;
    bool hasconflicts = getAllPending(&retlist);

    strList << QString::number(hasconflicts);
    strList << QString::number(retlist.size());

    while (retlist.size() > 0)
    {
        ProgramInfo *p = retlist.front();
        p->ToStringList(strList);
        delete p;
        retlist.pop_front();
    }
}

void Scheduler::getAllScheduled(QStringList &strList)
{
    RecList schedlist;

    findAllScheduledPrograms(schedlist);

    strList << QString::number(schedlist.size());

    while (schedlist.size() > 0)
    {
        ProgramInfo *pginfo = schedlist.front();
        pginfo->ToStringList(strList);
        delete pginfo;
        schedlist.pop_front();
    }
}

void Scheduler::Reschedule(int recordid) { 
    reschedLock.lock(); 
    if (recordid == -1)
        reschedQueue.clear();
    if (recordid != 0 || !reschedQueue.size())
        reschedQueue.append(recordid);
    reschedWait.wakeOne();
    reschedLock.unlock();
}

void Scheduler::AddRecording(const ProgramInfo &pi)
{
    QMutexLocker lockit(reclist_lock);

    VERBOSE(VB_GENERAL, LOC + "AddRecording() recid: " << pi.recordid);

    for (RecIter it = reclist.begin(); it != reclist.end(); ++it)
    {
        ProgramInfo *p = *it;
        if (p->recstatus == rsRecording && p->IsSameProgramTimeslot(pi))
        {
            VERBOSE(VB_IMPORTANT, LOC + "Not adding recording, " +
                    QString("'%1' is already in reclist.").arg(pi.title));
            return;
        }
    }

    VERBOSE(VB_SCHEDULE, LOC + 
            QString("Adding '%1' to reclist.").arg(pi.title));

    ProgramInfo * new_pi = new ProgramInfo(pi);
    reclist.push_back(new_pi);
    reclist_changed = true;

    // Save rsRecording recstatus to DB
    // This allows recordings to resume on backend restart
    new_pi->AddHistory(false);

    // Make sure we have a ScheduledRecording instance
    new_pi->GetScheduledRecording();

    // Trigger reschedule..
    ScheduledRecording::signalChange(pi.recordid);
}

bool Scheduler::IsBusyRecording(const ProgramInfo *rcinfo)
{
    if (!m_tvList || !rcinfo)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "IsBusyRecording() -> true, "
                "no tvList or no rcinfo");

        return true;
    }

    EncoderLink *rctv = (*m_tvList)[rcinfo->cardid];
    // first check the card we will be recording on...
    if (!rctv || rctv->IsBusyRecording())
        return true;

    // now check other cards in the same input group as the recording.
    TunedInputInfo busy_input;
    uint inputid = rcinfo->inputid;
    vector<uint> cardids = CardUtil::GetConflictingCards(
        inputid, rcinfo->cardid);
    for (uint i = 0; i < cardids.size(); i++)
    {
        rctv = (*m_tvList)[cardids[i]];
        if (!rctv)
        {
            VERBOSE(VB_SCHEDULE, LOC_ERR + "IsBusyRecording() -> true, "
                    "rctv("<<rctv<<"==NULL) for card "<<cardids[i]);

            return true;
        }

        if (rctv->IsBusy(&busy_input, -1) &&
            igrp.GetSharedInputGroup(busy_input.inputid, inputid))
        {
            return true;
        }
    }

    return false;
}

void Scheduler::RunScheduler(void)
{
    int prerollseconds = 0;

    int secsleft;
    EncoderLink *nexttv = NULL;

    ProgramInfo *nextRecording = NULL;
    QDateTime nextrectime;
    QString schedid;

    QDateTime curtime;
    QDateTime lastupdate = QDateTime::currentDateTime().addDays(-1);

    RecIter startIter = reclist.begin();

    bool blockShutdown = gContext->GetNumSetting("blockSDWUwithoutClient", 1);
    QDateTime idleSince = QDateTime();
    int idleTimeoutSecs = 0;
    int idleWaitForRecordingTime = 0;
    bool firstRun = true;

    struct timeval fillstart, fillend;
    float matchTime, placeTime;

    // Mark anything that was recording as aborted.  We'll fix it up.
    // if possible, after the slaves connect and we start scheduling.
    MSqlQuery query(dbConn);
    query.prepare("UPDATE oldrecorded SET recstatus = :RSABORTED "
                  "  WHERE recstatus = :RSRECORDING");
    query.bindValue(":RSABORTED", rsAborted);
    query.bindValue(":RSRECORDING", rsRecording);
    query.exec();
    if (!query.isActive())
        MythContext::DBError("UpdateAborted", query);

    // wait for slaves to connect
    sleep(3);

    Reschedule(-1);

    while (1)
    {
        curtime = QDateTime::currentDateTime();
        bool statuschanged = false;

      if ((startIter != reclist.end() &&
           curtime.secsTo((*startIter)->recstartts) < 30))
          sleep(1);
      else
      {
        reschedLock.lock();
        if (!reschedQueue.count())
            reschedWait.wait(&reschedLock, 1000);
        reschedLock.unlock();
            
        if (reschedQueue.count())
        {
            // We might have been inactive for a long time, so make
            // sure our DB connection is fresh before continuing.
            dbConn = MSqlQuery::SchedCon();

            gettimeofday(&fillstart, NULL);
            QString msg;
            while (reschedQueue.count())
            {
                int recordid = reschedQueue.front();
                reschedQueue.pop_front();
                msg.sprintf("Reschedule requested for id %d.", recordid);
                VERBOSE(VB_GENERAL, msg);
                if (recordid != 0)
                {
                    if (recordid == -1)
                        reschedQueue.clear();
                    recordmatchLock.lock();
                    UpdateMatches(recordid);
                    recordmatchLock.unlock();
                }
            }
            gettimeofday(&fillend, NULL);

            matchTime = ((fillend.tv_sec - fillstart.tv_sec ) * 1000000 +
                         (fillend.tv_usec - fillstart.tv_usec)) / 1000000.0;

            gettimeofday(&fillstart, NULL);
            bool worklistused = FillRecordList();
            gettimeofday(&fillend, NULL);
            if (worklistused)
            {
                UpdateNextRecord();
                PrintList();
            }
            else
            {
                VERBOSE(VB_GENERAL, "Reschedule interrupted, will retry");
                Reschedule(0);
                continue;
            }

            placeTime = ((fillend.tv_sec - fillstart.tv_sec ) * 1000000 +
                         (fillend.tv_usec - fillstart.tv_usec)) / 1000000.0;

            msg.sprintf("Scheduled %d items in "
                        "%.1f = %.2f match + %.2f place", (int)reclist.size(),
                        matchTime + placeTime, matchTime, placeTime);

            VERBOSE(VB_GENERAL, msg);
            gContext->LogEntry("scheduler", LP_INFO, "Scheduled items", msg);

            fsInfoCacheFillTime = QDateTime::currentDateTime().addSecs(-1000);

            lastupdate = curtime;
            startIter = reclist.begin();
            statuschanged = true;

            // Determine if the user wants us to start recording early
            // and by how many seconds
            prerollseconds = gContext->GetNumSetting("RecordPreRoll");

            idleTimeoutSecs = gContext->GetNumSetting("idleTimeoutSecs", 0);
            idleWaitForRecordingTime =
                       gContext->GetNumSetting("idleWaitForRecordingTime", 15);

            if (firstRun)
            {
                //the parameter given to the startup_cmd. "user" means a user
                // started the BE, 'auto' means it was started automatically
                QString startupParam = "user";

                // find the first recording that WILL be recorded
                RecIter firstRunIter = reclist.begin();
                for ( ; firstRunIter != reclist.end(); firstRunIter++)
                    if ((*firstRunIter)->recstatus == rsWillRecord)
                        break;

                // have we been started automatically?
                if (WasStartedAutomatically() ||
                    ((firstRunIter != reclist.end()) &&
                     ((curtime.secsTo((*firstRunIter)->recstartts) - prerollseconds)
                        < (idleWaitForRecordingTime * 60))))
                {
                    VERBOSE(VB_IMPORTANT, "AUTO-Startup assumed");
                    startupParam = "auto";

                    // Since we've started automatically, don't wait for
                    // client to connect before allowing shutdown.
                    blockShutdown = false;
                }
                else
                {
                    VERBOSE(VB_IMPORTANT, "Seem to be woken up by USER");
                }

                QString startupCommand = gContext->GetSetting("startupCommand",
                                                              "");
                if (!startupCommand.isEmpty())
                {
                    startupCommand.replace("$status", startupParam);
                    myth_system(startupCommand.ascii());
                }
                firstRun = false;
            }
        }
      }

        for ( ; startIter != reclist.end(); startIter++)
            if ((*startIter)->recstatus != (*startIter)->oldrecstatus)
                break;

        curtime = QDateTime::currentDateTime();

        RecIter recIter = startIter;
        for ( ; recIter != reclist.end(); recIter++)
        {
            QString msg, details;
            int fsID = -1;

            nextRecording = *recIter;

            if (nextRecording->recstatus != rsWillRecord)
            {
                if (nextRecording->recstatus != nextRecording->oldrecstatus &&
                    nextRecording->recstartts <= curtime)
                    nextRecording->AddHistory(false);
                continue;
            }

            nextrectime = nextRecording->recstartts;
            secsleft = curtime.secsTo(nextrectime);
            schedid = nextRecording->schedulerid;

            if (secsleft - prerollseconds < 60)
            {
                if (!recPendingList.contains(schedid))
                {
                    recPendingList[schedid] = false;

                    livetvTime = (livetvTime < nextrectime) ?
                        nextrectime : livetvTime;

                    Reschedule(0);
                }
            }

            if (secsleft - prerollseconds > 35)
                break;

            if (m_tvList->find(nextRecording->cardid) == m_tvList->end())
            {
                msg = QString("invalid cardid (%1) for %2")
                    .arg(nextRecording->cardid)
                    .arg(nextRecording->title);
                VERBOSE(VB_GENERAL, msg);

                QMutexLocker lockit(reclist_lock);
                nextRecording->recstatus = rsTunerBusy;
                nextRecording->AddHistory(true);
                statuschanged = true;
                continue;
            }

            nexttv = (*m_tvList)[nextRecording->cardid];
            // cerr << "nexttv = " << nextRecording->cardid;
            // cerr << " title: " << nextRecording->title << endl;

            if (nexttv->IsTunerLocked())
            {
                msg = QString("SUPPRESSED recording \"%1\" on channel: "
                              "%2 on cardid: %3, sourceid %4. Tuner "
                              "is locked by an external application.")
                    .arg(nextRecording->title.local8Bit())
                    .arg(nextRecording->chanid)
                    .arg(nextRecording->cardid)
                    .arg(nextRecording->sourceid);
                VERBOSE(VB_GENERAL, msg);

                QMutexLocker lockit(reclist_lock);
                nextRecording->recstatus = rsTunerBusy;
                nextRecording->AddHistory(true);
                statuschanged = true;
                continue;
            }

            if (!IsBusyRecording(nextRecording))
            {
                // Will use pre-roll settings only if no other
                // program is currently being recorded
                secsleft -= prerollseconds;
            }

            //VERBOSE(VB_GENERAL, secsleft << " seconds until " << nextRecording->title);

            if (secsleft > 30)
                continue;

            if (nextRecording->pathname == "")
            {
                QMutexLocker lockit(reclist_lock);
                fsID = FillRecordingDir(nextRecording, reclist);
            }

            if (!recPendingList[schedid])
            {
                nexttv->RecordPending(nextRecording, max(secsleft, 0),
                                      hasLaterList.contains(schedid));
                recPendingList[schedid] = true;
            }

            if (secsleft > -2)
                continue;

            nextRecording->recstartts = 
                mythCurrentDateTime().addSecs(30);
            nextRecording->recstartts.setTime(QTime(
                nextRecording->recstartts.time().hour(),
                nextRecording->recstartts.time().minute()));

            QMutexLocker lockit(reclist_lock);

            QString subtitle = nextRecording->subtitle.isEmpty() ? "" :
                QString(" \"%1\"").arg(nextRecording->subtitle);

            details = QString("%1%2: "
                "channel %3 on cardid %4, sourceid %5")
                .arg(nextRecording->title)
                .arg(subtitle)
                .arg(nextRecording->chanid)
                .arg(nextRecording->cardid)
                .arg(nextRecording->sourceid);

            if (schedulingEnabled)
            {
                nextRecording->recstatus =
                    nexttv->StartRecording(nextRecording);

                nextRecording->AddHistory(false);
                if (expirer)
                {
                    // activate auto expirer
                    expirer->Update(nextRecording->cardid, fsID, true);
                }
            }
            else
                nextRecording->recstatus = rsOffLine;
            bool doSchedAfterStart = 
                nextRecording->recstatus != rsRecording ||
                schedAfterStartMap[nextRecording->recordid] ||
                (nextRecording->parentid && 
                 schedAfterStartMap[nextRecording->parentid]);
            nextRecording->AddHistory(doSchedAfterStart);

            statuschanged = true;

            bool is_rec = (nextRecording->recstatus == rsRecording);
            msg = is_rec ? "Started recording" : "Canceled recording (" +
                nextRecording->RecStatusText() + ")"; 

            VERBOSE(VB_GENERAL, msg << ": " << details);
            gContext->LogEntry("scheduler", LP_NOTICE, msg, details);

            if (is_rec)
                UpdateNextRecord();

            if (nextRecording->recstatus == rsFailed)
            {
                MythEvent me(QString("FORCE_DELETE_RECORDING %1 %2")
                         .arg(nextRecording->chanid)
                         .arg(nextRecording->recstartts.toString(Qt::ISODate)));
                gContext->dispatch(me);
            }
        }

        if (statuschanged)
        {
            MythEvent me("SCHEDULE_CHANGE");
            gContext->dispatch(me);
            idleSince = QDateTime();
        }

        // if idletimeout is 0, the user disabled the auto-shutdown feature
        if ((idleTimeoutSecs > 0) && (m_mainServer != NULL)) 
        {
            // we release the block when a client connects
            if (blockShutdown)
                blockShutdown &= !m_mainServer->isClientConnected();
            else
            {
                // find out, if we are currently recording (or LiveTV)
                bool recording = false;
                QMap<int, EncoderLink *>::Iterator it;
                for (it = m_tvList->begin(); (it != m_tvList->end()) && 
                     !recording; ++it)
                {
                    if (it.data()->IsBusy())
                        recording = true;
                }

                if (!(m_mainServer->isClientConnected()) && !recording)
                {
                    // have we received a RESET_IDLETIME message?
                    resetIdleTime_lock.lock();
                    if (resetIdleTime)
                    {
                        // yes - so reset the idleSince time
                        idleSince = QDateTime();
                        resetIdleTime = false;
                    }
                    resetIdleTime_lock.unlock();

                    if (!idleSince.isValid())
                    {
                        RecIter idleIter = reclist.begin();
                        for ( ; idleIter != reclist.end(); idleIter++)
                            if ((*idleIter)->recstatus == rsWillRecord)
                                break;

                        if (idleIter != reclist.end())
                        {
                            if (curtime.secsTo((*idleIter)->recstartts) - 
                                prerollseconds > idleWaitForRecordingTime * 60)
                            {
                                idleSince = curtime;
                            }
                        }
                        else
                            idleSince = curtime;
                    } 
                    else 
                    {
                        // is the machine already idling the timeout time?
                        if (idleSince.addSecs(idleTimeoutSecs) < curtime)
                        {
                            // are we waiting for shutdown?
                            if (m_isShuttingDown)
                            {
                                // if we have been waiting more that 60secs then assume
                                // something went wrong so reset and try again
                                if (idleSince.addSecs(idleTimeoutSecs + 60) < curtime)
                                {
                                    VERBOSE(VB_IMPORTANT, "Waited more than 60" 
                                            " seconds for shutdown to complete"
                                            " - resetting idle time");
                                    idleSince = QDateTime();
                                    m_isShuttingDown = false;
                                }
                            }
                            else if (!m_isShuttingDown &&
                                CheckShutdownServer(prerollseconds, idleSince,
                                                    blockShutdown))
                            {
                                ShutdownServer(prerollseconds, idleSince);
                            }
                        }
                        else
                        {
                            int itime = idleSince.secsTo(curtime);
                            QString msg;
                            if (itime == 1)
                            {
                                msg = QString("I\'m idle now... shutdown will "
                                              "occur in %1 seconds.")
                                             .arg(idleTimeoutSecs);
                                VERBOSE(VB_IMPORTANT, msg);
                                MythEvent me(QString("SHUTDOWN_COUNTDOWN %1")
                                             .arg(idleTimeoutSecs));
                                gContext->dispatch(me);
                            }
                            else if (itime % 10 == 0)
                            {
                                msg = QString("%1 secs left to system "
                                              "shutdown!")
                                             .arg(idleTimeoutSecs - itime);
                                VERBOSE(VB_IDLE, msg);
                                MythEvent me(QString("SHUTDOWN_COUNTDOWN %1")
                                             .arg(idleTimeoutSecs - itime));
                                gContext->dispatch(me);
                            }
                        }
                    }
                }
                else
                {
                    // not idle, make the time invalid
                    if (idleSince.isValid())
                    {
                        MythEvent me(QString("SHUTDOWN_COUNTDOWN -1"));
                        gContext->dispatch(me);
                    }
                    idleSince = QDateTime();
                }
            }
        }
    }
} 

//returns true, if the shutdown is not blocked
bool Scheduler::CheckShutdownServer(int prerollseconds, QDateTime &idleSince, 
                                    bool &blockShutdown)
{
    (void)prerollseconds;
    bool retval = false;
    QString preSDWUCheckCommand = gContext->GetSetting("preSDWUCheckCommand", 
                                                       "");

    int state = 0;
    if (!preSDWUCheckCommand.isEmpty())
    {
        state = myth_system(preSDWUCheckCommand.ascii());

        if (GENERIC_EXIT_NOT_OK != state)
        {
            retval = false;
            switch(state)
            {
                case 0:
                    VERBOSE(VB_GENERAL, "CheckShutdownServer returned - OK to shutdown");
                    retval = true;
                    break;
                case 1:
                    VERBOSE(VB_IDLE, "CheckShutdownServer returned - Not OK to shutdown");
                    // just reset idle'ing on retval == 1
                    idleSince = QDateTime();
                    break;
                case 2:
                    VERBOSE(VB_IDLE, "CheckShutdownServer returned - Not OK to shutdown, need reconnect");
                    // reset shutdown status on retval = 2
                    // (needs a clientconnection again,
                    // before shutdown is executed)
                    blockShutdown
                             = gContext->GetNumSetting("blockSDWUwithoutClient",
                                                       1);
                    idleSince = QDateTime();
                    break;
                // case 3:
                //    //disable shutdown routine generally
                //    m_noAutoShutdown = true;
                //    break;
                default:
                    break;
            }
        }
    }
    else
        retval = true; // allow shutdown if now command is set.

    return retval;
}

void Scheduler::ShutdownServer(int prerollseconds, QDateTime &idleSince)
{
    m_isShuttingDown = true;

    RecIter recIter = reclist.begin();
    for ( ; recIter != reclist.end(); recIter++)
        if ((*recIter)->recstatus == rsWillRecord)
            break;

    // set the wakeuptime if needed
    if (recIter != reclist.end())
    {
        ProgramInfo *nextRecording = (*recIter);
        QDateTime restarttime = nextRecording->recstartts.addSecs((-1) * 
                                                               prerollseconds);

        int add = gContext->GetNumSetting("StartupSecsBeforeRecording", 240);
        if (add)
            restarttime = restarttime.addSecs((-1) * add);

        QString wakeup_timeformat = gContext->GetSetting("WakeupTimeFormat",
                                                         "hh:mm yyyy-MM-dd");
        QString setwakeup_cmd = gContext->GetSetting("SetWakeuptimeCommand",
                                                     "echo \'Wakeuptime would "
                                                     "be $time if command "
                                                     "set.\'");

        if (wakeup_timeformat == "time_t")
        {
            QString time_ts;
            setwakeup_cmd.replace("$time", 
                                  time_ts.setNum(restarttime.toTime_t()));
        }
        else
            setwakeup_cmd.replace("$time", 
                                  restarttime.toString(wakeup_timeformat));

        VERBOSE(VB_GENERAL, QString("Running the command to set the next "
                                    "scheduled wakeup time :-\n\t\t\t\t\t\t") + 
                                    setwakeup_cmd);

        // now run the command to set the wakeup time
        if (!setwakeup_cmd.isEmpty())
            myth_system(setwakeup_cmd.ascii());
    }

    // tell anyone who is listening the master server is going down now
    MythEvent me(QString("SHUTDOWN_NOW"));
    gContext->dispatch(me);

    QString halt_cmd = gContext->GetSetting("ServerHaltCommand",
                                            "sudo /sbin/halt -p");

    if (!halt_cmd.isEmpty())
    {
        // now we shut the slave backends down...
        m_mainServer->ShutSlaveBackendsDown(halt_cmd);

        VERBOSE(VB_GENERAL, QString("Running the command to shutdown "
                                    "this computer :-\n\t\t\t\t\t\t") + halt_cmd);

        // and now shutdown myself
        myth_system(halt_cmd.ascii());
    }

    // If we make it here then either the shutdown failed
    // OR we suspended or hibernated the OS instead
    idleSince = QDateTime();
    m_isShuttingDown = false;
}

void *Scheduler::SchedulerThread(void *param)
{
    // Lower scheduling priority, to avoid problems with recordings.
    if (setpriority(PRIO_PROCESS, 0, 9))
        VERBOSE(VB_IMPORTANT, LOC + "Setting priority failed." + ENO);
    Scheduler *sched = (Scheduler *)param;
    sched->RunScheduler();
 
    return NULL;
}

void Scheduler::UpdateManuals(int recordid)
{
    MSqlQuery query(dbConn);

    query.prepare(QString("SELECT type,title,station,startdate,starttime, "
                  " enddate,endtime "
                  "FROM %1 WHERE recordid = :RECORDID").arg(recordTable));
    query.bindValue(":RECORDID", recordid);
    query.exec();
    if (!query.isActive() || query.size() != 1)
    {
        MythContext::DBError("UpdateManuals", query);
        return;
    }

    query.next();
    RecordingType rectype = RecordingType(query.value(0).toInt());
    QString title = query.value(1).toString();
    QString station = query.value(2).toString() ;
    QDateTime startdt = QDateTime(query.value(3).asDate(),
                                  query.value(4).asTime());
    int duration = startdt.secsTo(QDateTime(query.value(5).asDate(),
                                            query.value(6).asTime())) / 60;

    query.prepare("SELECT chanid from channel "
                  "WHERE callsign = :STATION");
    query.bindValue(":STATION", station);
    query.exec();
    if (!query.isActive())
    {
        MythContext::DBError("UpdateManuals", query);
        return;
    }

    QValueList<int> chanidlist;
    while (query.next())
        chanidlist.append(query.value(0).toInt());

    int progcount;
    int skipdays;
    bool weekday;
    int weeksoff;

    switch (rectype)
    {
    case kSingleRecord:
    case kOverrideRecord:
    case kDontRecord:
        progcount = 1;
        skipdays = 1;
        weekday = false;
        break;
    case kTimeslotRecord:
        progcount = 13;
        skipdays = 1;
        if (startdt.date().dayOfWeek() < 6)
            weekday = true;
        else
            weekday = false;
        startdt.setDate(QDate::currentDate());
        break;
    case kWeekslotRecord:
        progcount = 2;
        skipdays = 7;
        weekday = false;
        weeksoff = (startdt.date().daysTo(QDate::currentDate()) + 6) / 7;
        startdt = startdt.addDays(weeksoff * 7);
        break;
    default:
        VERBOSE(VB_IMPORTANT, QString("Invalid rectype for manual "
                                      "recordid %1").arg(recordid));
        return;
    }

    while (progcount--)
    {
        for (int i = 0; i < (int)chanidlist.size(); i++)
        {
            if (weekday && startdt.date().dayOfWeek() >= 6)
                continue;

            query.prepare("REPLACE INTO program (chanid,starttime,endtime,"
                          " title,subtitle,manualid) "
                          "VALUES (:CHANID,:STARTTIME,:ENDTIME,:TITLE,"
                          " :SUBTITLE,:RECORDID)");
            query.bindValue(":CHANID", chanidlist[i]);
            query.bindValue(":STARTTIME", startdt);
            query.bindValue(":ENDTIME", startdt.addSecs(duration * 60));
            query.bindValue(":TITLE", title);
            query.bindValue(":SUBTITLE", startdt.toString());
            query.bindValue(":RECORDID", recordid);
            query.exec();
            if (!query.isActive())
            {
                MythContext::DBError("UpdateManuals", query);
                return;
            }
        }
        startdt = startdt.addDays(skipdays);
    }
}

void Scheduler::BuildNewRecordsQueries(int recordid, QStringList &from, 
                                       QStringList &where, 
                                       MSqlBindings &bindings)
{
    MSqlQuery result(dbConn);
    QString query;
    QString qphrase;

    query = QString("SELECT recordid,search,subtitle,description "
                    "FROM %1 WHERE search <> %2 AND "
                    "(recordid = %3 OR %4 = -1) ")
        .arg(recordTable).arg(kNoSearch).arg(recordid).arg(recordid);

    result.prepare(query);

    if (!result.exec() || !result.isActive())
    {
        MythContext::DBError("BuildNewRecordsQueries", result);
        return;
    }

    int count = 0;
    while (result.next())
    {
        QString prefix = QString(":NR%1").arg(count);
        qphrase = QString::fromUtf8(result.value(3).toString());

        RecSearchType searchtype = RecSearchType(result.value(1).toInt());

        if (qphrase == "" && searchtype != kManualSearch)
        {
            VERBOSE(VB_IMPORTANT, QString("Invalid search key in recordid %1")
                                         .arg(result.value(0).toString()));
            continue;
        }

        QString bindrecid = prefix + "RECID";
        QString bindphrase = prefix + "PHRASE";
        QString bindlikephrase = prefix + "LIKEPHRASE";

        bindings[bindrecid] = result.value(0).toString();
        bindings[bindphrase] = qphrase.utf8();
        bindings[bindlikephrase] = QString(QString("%") + qphrase + "%").utf8();

        switch (searchtype)
        {
        case kPowerSearch:
            qphrase.remove(QRegExp("^\\s*AND\\s+", false));
            qphrase.remove(";", false);
            from << result.value(2).toString();
            where << (QString("%1.recordid = ").arg(recordTable) + bindrecid +
                      QString(" AND program.manualid = 0 AND ( %2 )")
                      .arg(qphrase));
            break;
        case kTitleSearch:
            from << "";
            where << (QString("%1.recordid = ").arg(recordTable) + bindrecid + " AND "
                      "program.manualid = 0 AND "
                      "program.title LIKE " + bindlikephrase);
            break;
        case kKeywordSearch:
            from << "";
            where << (QString("%1.recordid = ").arg(recordTable) + bindrecid +
                      " AND program.manualid = 0"
                      " AND (program.title LIKE " + bindlikephrase +
                      " OR program.subtitle LIKE " + bindlikephrase +
                      " OR program.description LIKE " + bindlikephrase + ")");
            break;
        case kPeopleSearch:
            from << ", people, credits";
            where << (QString("%1.recordid = ").arg(recordTable) + bindrecid + " AND "
                      "program.manualid = 0 AND "
                      "people.name LIKE " + bindphrase + " AND "
                      "credits.person = people.person AND "
                      "program.chanid = credits.chanid AND "
                      "program.starttime = credits.starttime");
            break;
        case kManualSearch:
            UpdateManuals(result.value(0).toInt());
            from << "";
            where << ((QString("%1.recordid = ").arg(recordTable)) + bindrecid + " AND " +
                              QString("program.manualid = %1.recordid ").arg(recordTable));
            break;
        default:
            VERBOSE(VB_IMPORTANT, QString("Unknown RecSearchType "
                                         "(%1) for recordid %2")
                                         .arg(result.value(1).toInt())
                                         .arg(result.value(0).toString()));
            break;
        }

        count++;
    }

    if (recordid == -1 || from.count() == 0)
    {
        QString recidmatch = "";
        if (recordid != -1)
            recidmatch = "RECTABLE.recordid = :NRRECORDID AND ";
        QString s = recidmatch + 
            "RECTABLE.search = :NRST AND "
            "program.manualid = 0 AND "
            "program.title = RECTABLE.title ";

        while (1)
        {
            int i = s.find("RECTABLE");
            if (i == -1) break;
            s = s.replace(i, strlen("RECTABLE"), recordTable);
        }

        from << "";
        where << s;
        bindings[":NRST"] = kNoSearch;
        bindings[":NRRECORDID"] = recordid;
    }
}

void Scheduler::UpdateMatches(int recordid) {
    struct timeval dbstart, dbend;

    if (recordid == 0)
        return;

    MSqlQuery query(dbConn);

    if (recordid == -1)
        query.prepare("DELETE FROM recordmatch");
    else
    {
        query.prepare("DELETE FROM recordmatch WHERE recordid = :RECORDID");
        query.bindValue(":RECORDID", recordid);
    }

    query.exec();
    if (!query.isActive())
    {
        MythContext::DBError("UpdateMatches", query);
        return;
    }

    if (recordid == -1)
        query.prepare("DELETE FROM program WHERE manualid <> 0");
    else
    {
        query.prepare("DELETE FROM program WHERE manualid = :RECORDID");
        query.bindValue(":RECORDID", recordid);
    }
    query.exec();
    if (!query.isActive())
    {
        MythContext::DBError("UpdateMatches", query);
        return;
    }

    // Make sure all FindOne rules have a valid findid before scheduling.
    query.prepare("SELECT NULL from record "
                  "WHERE type = :FINDONE AND findid <= 0;");
    query.bindValue(":FINDONE", kFindOneRecord);
    query.exec();
    if (!query.isActive())
    {
        MythContext::DBError("UpdateMatches", query);
        return;
    }
    else if (query.size())
    {
        QDate epoch = QDate::QDate (1970, 1, 1);
        int findtoday =  epoch.daysTo(QDate::currentDate()) + 719528;
        query.prepare("UPDATE record set findid = :FINDID "
                      "WHERE type = :FINDONE AND findid <= 0;");
        query.bindValue(":FINDID", findtoday);
        query.bindValue(":FINDONE", kFindOneRecord);
        query.exec();
    }

    unsigned clause;
    QStringList fromclauses, whereclauses;
    MSqlBindings bindings;

    BuildNewRecordsQueries(recordid, fromclauses, whereclauses, bindings);

    if (print_verbose_messages & VB_SCHEDULE)
    {
        for (clause = 0; clause < fromclauses.count(); clause++)
            cout << "Query " << clause << ": " << fromclauses[clause] 
                 << "/" << whereclauses[clause] << endl;
    }

    for (clause = 0; clause < fromclauses.count(); clause++)
    {
        QString query = QString(
"INSERT INTO recordmatch (recordid, chanid, starttime, manualid) "
"SELECT RECTABLE.recordid, program.chanid, program.starttime, "
" IF(search = %1, RECTABLE.recordid, 0) ").arg(kManualSearch) + QString(
"FROM (RECTABLE, program INNER JOIN channel "
"      ON channel.chanid = program.chanid) ") + fromclauses[clause] + QString(
" WHERE ") + whereclauses[clause] + 
    QString(" AND (NOT ((RECTABLE.dupin & %1) AND program.previouslyshown)) "
            " AND (NOT ((RECTABLE.dupin & %2) AND program.generic > 0)) "
            " AND (NOT ((RECTABLE.dupin & %2) AND (program.previouslyshown "
            "                                      OR program.first = 0))) ")
            .arg(kDupsExRepeats).arg(kDupsExGeneric).arg(kDupsFirstNew) +
    QString(" AND channel.visible = 1 AND "
"((RECTABLE.type = %1 " // allrecord
"OR RECTABLE.type = %2 " // findonerecord
"OR RECTABLE.type = %3 " // finddailyrecord
"OR RECTABLE.type = %4) " // findweeklyrecord
" OR "
" ((RECTABLE.station = channel.callsign) " // channel matches
"  AND "
"  ((RECTABLE.type = %5) " // channelrecord
"   OR"
"   ((TIME_TO_SEC(RECTABLE.starttime) = TIME_TO_SEC(program.starttime)) " // timeslot matches
"    AND "
"    ((RECTABLE.type = %6) " // timeslotrecord
"     OR"
"     ((DAYOFWEEK(RECTABLE.startdate) = DAYOFWEEK(program.starttime) "
"      AND "
"      ((RECTABLE.type = %7) " // weekslotrecord
"       OR"
"       ((TO_DAYS(RECTABLE.startdate) = TO_DAYS(program.starttime)) " // date matches
"        )"
"       )"
"      )"
"     )"
"    )"
"   )"
"  )"
" )"
") ")
            .arg(kAllRecord)
            .arg(kFindOneRecord)
            .arg(kFindDailyRecord)
            .arg(kFindWeeklyRecord)
            .arg(kChannelRecord)
            .arg(kTimeslotRecord)
            .arg(kWeekslotRecord);

        while (1)
        {
            int i = query.find("RECTABLE");
            if (i == -1) break;
            query = query.replace(i, strlen("RECTABLE"), recordTable);
        }

        VERBOSE(VB_SCHEDULE, QString(" |-- Start DB Query %1...").arg(clause));

        gettimeofday(&dbstart, NULL);
        MSqlQuery result(dbConn);
        result.prepare(query);
        result.bindValues(bindings);
        result.exec();
        gettimeofday(&dbend, NULL);

        if (!result.isActive())
        {
            MythContext::DBError("UpdateMatches", result);
            continue;
        }

        VERBOSE(VB_SCHEDULE, QString(" |-- %1 results in %2 sec.")
                .arg(result.size())
                .arg(((dbend.tv_sec  - dbstart.tv_sec) * 1000000 +
                      (dbend.tv_usec - dbstart.tv_usec)) / 1000000.0));

    }

    VERBOSE(VB_SCHEDULE, " +-- Done.");
}

void Scheduler::AddNewRecords(void) 
{
    struct timeval dbstart, dbend;

    QMap<RecordingType, int> recTypeRecPriorityMap;
    RecList tmpList;

    QMap<int, bool> cardMap;
    QMap<int, EncoderLink *>::Iterator enciter = m_tvList->begin();
    for (; enciter != m_tvList->end(); ++enciter)
    {
        EncoderLink *enc = enciter.data();
        if (enc->IsConnected())
            cardMap[enc->GetCardID()] = true;
    }

    QMap<int, bool> tooManyMap;
    bool checkTooMany = false;
    schedAfterStartMap.clear();

    MSqlQuery rlist(dbConn);
    rlist.prepare(QString("SELECT recordid,title,maxepisodes,maxnewest FROM %1;").arg(recordTable));

    rlist.exec();

    if (!rlist.isActive())
    {
        MythContext::DBError("CheckTooMany", rlist);
        return;
    }

    while (rlist.next())
    {
        int recid = rlist.value(0).toInt();
        QString qtitle = QString::fromUtf8(rlist.value(1).toString());
        int maxEpisodes = rlist.value(2).toInt();
        int maxNewest = rlist.value(3).toInt();

        tooManyMap[recid] = false;
        schedAfterStartMap[recid] = false;

        if (maxEpisodes && !maxNewest)
        {
            MSqlQuery epicnt(dbConn);

            epicnt.prepare("SELECT DISTINCT chanid, progstart, progend "
                           "FROM recorded "
                           "WHERE recordid = :RECID AND preserve = 0 "
                               "AND duplicate <> 0 "
                               "AND recgroup NOT IN ('LiveTV','Deleted');");
            epicnt.bindValue(":RECID", recid);

            if (epicnt.exec() && epicnt.isActive())
            {
                if (epicnt.size() >= maxEpisodes - 1)
                {
                    schedAfterStartMap[recid] = true;
                    if (epicnt.size() >= maxEpisodes)
                    {
                        tooManyMap[recid] = true;
                        checkTooMany = true;
                    }
                }
            }
        }
    }

    int complexpriority = gContext->GetNumSetting("ComplexPriority", 0);
    prefinputpri        = gContext->GetNumSetting("PrefInputPriority", 2);
    int hdtvpriority    = gContext->GetNumSetting("HDTVRecPriority", 0);

    QString pwrpri = "channel.recpriority + cardinput.recpriority";

    if (prefinputpri)
        pwrpri += QString(" + "
        "(cardinput.cardinputid = RECTABLE.prefinput) * %1").arg(prefinputpri);

    if (hdtvpriority)
        pwrpri += QString(" + (program.hdtv > 0) * %1").arg(hdtvpriority);

    QString schedTmpRecord = recordTable;

    MSqlQuery result(dbConn);

    if (schedTmpRecord == "record")
    {
        schedTmpRecord = "sched_temp_record";

        result.prepare("DROP TABLE IF EXISTS sched_temp_record;");
        result.exec();

        if (!result.isActive())
        {
            MythContext::DBError("Dropping sched_temp_record table", result);
            return;
        }

        result.prepare("CREATE TEMPORARY TABLE sched_temp_record "
                           "LIKE record;");
        result.exec();

        if (!result.isActive())
        {
            MythContext::DBError("Creating sched_temp_record table",
                                 result);
            return;
        }

        result.prepare("INSERT sched_temp_record SELECT * from record;");
        result.exec();

        if (!result.isActive())
        {
            MythContext::DBError("Populating sched_temp_record table",
                                 result);
            return;
        }
    }

    result.prepare("DROP TABLE IF EXISTS sched_temp_recorded;");
    result.exec();

    if (!result.isActive())
    {
        MythContext::DBError("Dropping sched_temp_recorded table", result);
        return;
    }

    result.prepare("CREATE TEMPORARY TABLE sched_temp_recorded "
                       "LIKE recorded;");
    result.exec();

    if (!result.isActive())
    {
        MythContext::DBError("Creating sched_temp_recorded table", result);
        return;
    }

    result.prepare("INSERT sched_temp_recorded SELECT * from recorded;");
    result.exec();

    if (!result.isActive())
    {
        MythContext::DBError("Populating sched_temp_recorded table", result);
        return;
    }

    result.prepare(QString("SELECT recpriority, selectclause FROM %1;")
                           .arg(priorityTable));
    result.exec();

    if (!result.isActive())
    {
        MythContext::DBError("Power Priority", result);
        return;
    }

    while (result.next())
    {
        if (result.value(0).toInt())
        {
            QString sclause = result.value(1).toString();
            sclause.remove(QRegExp("^\\s*AND\\s+", false));
            sclause.remove(";", false);
            pwrpri += QString(" + (%1) * %2").arg(sclause)
                                             .arg(result.value(0).toInt());
        }
    }
    pwrpri += QString(" AS powerpriority ");

    QString progfindid = QString(
"(CASE RECTABLE.type "
"  WHEN %1 "
"   THEN RECTABLE.findid "
"  WHEN %2 "
"   THEN to_days(date_sub(program.starttime, interval "
"                time_format(RECTABLE.findtime, '%H:%i') hour_minute)) "
"  WHEN %3 "
"   THEN floor((to_days(date_sub(program.starttime, interval "
"               time_format(RECTABLE.findtime, '%H:%i') hour_minute)) - "
"               RECTABLE.findday)/7) * 7 + RECTABLE.findday "
"  WHEN %4 "
"   THEN RECTABLE.findid "
"  ELSE 0 "
" END) ")
        .arg(kFindOneRecord)
        .arg(kFindDailyRecord)
        .arg(kFindWeeklyRecord)
        .arg(kOverrideRecord);

    QString rmquery = QString(
"UPDATE recordmatch "
" INNER JOIN RECTABLE ON (recordmatch.recordid = RECTABLE.recordid) "
" INNER JOIN program ON (recordmatch.chanid = program.chanid AND "
"                        recordmatch.starttime = program.starttime AND "
"                        recordmatch.manualid = program.manualid) "
" LEFT JOIN oldrecorded ON "
"  ( "
"    RECTABLE.dupmethod > 1 AND "
"    oldrecorded.duplicate <> 0 AND "
"    program.title = oldrecorded.title "
"     AND "
"     ( "
"      (program.programid <> '' AND program.generic = 0 "
"       AND program.programid = oldrecorded.programid) "
"      OR "
"      (oldrecorded.findid <> 0 AND "
"        oldrecorded.findid = ") + progfindid + QString(") "
"      OR "
"      ( "
"       program.generic = 0 "
"       AND "
"       (program.programid = '' OR oldrecorded.programid = '') "
"       AND "
"       (((RECTABLE.dupmethod & 0x02) = 0) OR (program.subtitle <> '' "
"          AND program.subtitle = oldrecorded.subtitle)) "
"       AND "
"       (((RECTABLE.dupmethod & 0x04) = 0) OR (program.description <> '' "
"          AND program.description = oldrecorded.description)) "
"       AND "
"       (((RECTABLE.dupmethod & 0x08) = 0) OR (program.subtitle <> '' "
"          AND program.subtitle = oldrecorded.subtitle) OR (program.subtitle = ''  "
"          AND oldrecorded.subtitle = '' AND program.description <> '' "
"          AND program.description = oldrecorded.description)) "
"      ) "
"     ) "
"  ) "
" LEFT JOIN sched_temp_recorded recorded ON "
"  ( "
"    RECTABLE.dupmethod > 1 AND "
"    recorded.duplicate <> 0 AND "
"    program.title = recorded.title AND "
"    recorded.recgroup NOT IN ('LiveTV','Deleted') "
"     AND "
"     ( "
"      (program.programid <> '' AND program.generic = 0 "
"       AND program.programid = recorded.programid) "
"      OR "
"      (recorded.findid <> 0 AND "
"        recorded.findid = ") + progfindid + QString(") "
"      OR "
"      ( "
"       program.generic = 0 "
"       AND "
"       (program.programid = '' OR recorded.programid = '') "
"       AND "
"       (((RECTABLE.dupmethod & 0x02) = 0) OR (program.subtitle <> '' "
"          AND program.subtitle = recorded.subtitle)) "
"       AND "
"       (((RECTABLE.dupmethod & 0x04) = 0) OR (program.description <> '' "
"          AND program.description = recorded.description)) "
"       AND "
"       (((RECTABLE.dupmethod & 0x08) = 0) OR (program.subtitle <> '' "
"          AND program.subtitle = recorded.subtitle) OR (program.subtitle = ''  "
"          AND recorded.subtitle = '' AND program.description <> '' "
"          AND program.description = recorded.description)) "
"      ) "
"     ) "
"  ) "
" LEFT JOIN oldfind ON "
"  (oldfind.recordid = recordmatch.recordid AND "
"   oldfind.findid = ") + progfindid + QString(") "
"  SET oldrecduplicate = (oldrecorded.endtime IS NOT NULL), "
"      recduplicate = (recorded.endtime IS NOT NULL), "
"      findduplicate = (oldfind.findid IS NOT NULL), "
"      oldrecstatus = oldrecorded.recstatus "
);
    rmquery.replace("RECTABLE", schedTmpRecord);

    QString query = QString(
"SELECT DISTINCT channel.chanid, channel.sourceid, "
"program.starttime, program.endtime, "
"program.title, program.subtitle, program.description, "
"channel.channum, channel.callsign, channel.name, "
"oldrecduplicate, program.category, "
"RECTABLE.recpriority, "
"RECTABLE.dupin, "
"recduplicate, "
"findduplicate, "
"RECTABLE.type, RECTABLE.recordid, "
"program.starttime - INTERVAL RECTABLE.startoffset minute AS recstartts, "
"program.endtime + INTERVAL RECTABLE.endoffset minute AS recendts, "
"program.previouslyshown, RECTABLE.recgroup, RECTABLE.dupmethod, "
"channel.commmethod, capturecard.cardid, "
"cardinput.cardinputid, UPPER(cardinput.shareable) = 'Y' AS shareable, "
"program.seriesid, program.programid, program.category_type, "
"program.airdate, program.stars, program.originalairdate, RECTABLE.inactive, "
"RECTABLE.parentid, ") + progfindid + ", RECTABLE.playgroup, "
"oldrecstatus.recstatus, oldrecstatus.reactivate, " 
"program.videoprop+0, program.subtitletypes+0, program.audioprop+0, "
"RECTABLE.storagegroup, capturecard.hostname, recordmatch.oldrecstatus, " + 
    pwrpri + QString(
"FROM recordmatch "
" INNER JOIN RECTABLE ON (recordmatch.recordid = RECTABLE.recordid) "
" INNER JOIN program ON (recordmatch.chanid = program.chanid AND "
"                        recordmatch.starttime = program.starttime AND "
"                        recordmatch.manualid = program.manualid) "
" INNER JOIN channel ON (channel.chanid = program.chanid) "
" INNER JOIN cardinput ON (channel.sourceid = cardinput.sourceid) "
" INNER JOIN capturecard ON (capturecard.cardid = cardinput.cardid) "
" LEFT JOIN oldrecorded as oldrecstatus ON "
"  ( oldrecstatus.station = channel.callsign AND "
"    oldrecstatus.starttime = program.starttime AND "
"    oldrecstatus.title = program.title ) "
" ORDER BY RECTABLE.recordid DESC "
);
    query.replace("RECTABLE", schedTmpRecord);

    VERBOSE(VB_SCHEDULE, QString(" |-- Start DB Query..."));

    gettimeofday(&dbstart, NULL);
    result.prepare(rmquery);
    result.exec();
    if (!result.isActive())
    {
        MythContext::DBError("AddNewRecords recordmatch", result);
        return;
    }
    result.prepare(query);
    result.exec();
    if (!result.isActive())
    {
        MythContext::DBError("AddNewRecords", result);
        return;
    }
    gettimeofday(&dbend, NULL);

    VERBOSE(VB_SCHEDULE, QString(" |-- %1 results in %2 sec. Processing...")
            .arg(result.size())
            .arg(((dbend.tv_sec  - dbstart.tv_sec) * 1000000 +
                  (dbend.tv_usec - dbstart.tv_usec)) / 1000000.0));

    while (result.next())
    {
        ProgramInfo *p = new ProgramInfo;
        p->reactivate = result.value(38).toInt();
        p->oldrecstatus = RecStatusType(result.value(37).toInt());
        if (p->oldrecstatus == rsAborted || p->reactivate)
            p->recstatus = rsUnknown;
        else
            p->recstatus = p->oldrecstatus;

        p->chanid = result.value(0).toString();
        p->sourceid = result.value(1).toInt();
        p->startts = result.value(2).toDateTime();
        p->endts = result.value(3).toDateTime();
        p->title = QString::fromUtf8(result.value(4).toString());
        p->subtitle = QString::fromUtf8(result.value(5).toString());
        p->description = QString::fromUtf8(result.value(6).toString());
        p->chanstr = result.value(7).toString();
        p->chansign = QString::fromUtf8(result.value(8).toString());
        p->channame = QString::fromUtf8(result.value(9).toString());
        p->category = QString::fromUtf8(result.value(11).toString());
        p->recpriority = result.value(12).toInt();
        p->dupin = RecordingDupInType(result.value(13).toInt());
        p->dupmethod = RecordingDupMethodType(result.value(22).toInt());
        p->rectype = RecordingType(result.value(16).toInt());
        p->recordid = result.value(17).toInt();

        p->recstartts = result.value(18).toDateTime();
        p->recendts = result.value(19).toDateTime();
        p->repeat = result.value(20).toInt();
        p->recgroup = QString::fromUtf8(result.value(21).toString());
        p->storagegroup = QString::fromUtf8(result.value(42).toString());
        p->playgroup = QString::fromUtf8(result.value(36).toString());
        p->chancommfree = (result.value(23).toInt() == -2);
        p->hostname = result.value(43).toString();
        p->cardid = result.value(24).toInt();
        p->inputid = result.value(25).toInt();
        p->shareable = result.value(26).toInt();
        p->seriesid = result.value(27).toString();
        p->programid = result.value(28).toString();
        p->catType = result.value(29).toString();
        p->year = result.value(30).toString();
        p->stars =  result.value(31).toDouble();

        if (result.value(32).isNull())
        {
            p->originalAirDate = QDate::QDate (0, 1, 1);
            p->hasAirDate = false;
        }
        else
        {
            p->originalAirDate = QDate::fromString(result.value(32).toString(), Qt::ISODate);
            p->hasAirDate = true;
        }

        bool inactive = result.value(33).toInt();
        p->parentid = result.value(34).toInt();
        p->findid = result.value(35).toInt();


        p->videoproperties = result.value(39).toInt();
        p->subtitleType = result.value(40).toInt();
        p->audioproperties = result.value(41).toInt();

        if (!recTypeRecPriorityMap.contains(p->rectype))
            recTypeRecPriorityMap[p->rectype] = 
                p->GetRecordingTypeRecPriority(p->rectype);
        p->recpriority += recTypeRecPriorityMap[p->rectype];

        p->recpriority2 = result.value(45).toInt();

        if (complexpriority == 0)
        {
            p->recpriority += p->recpriority2;
            p->recpriority2 = 0;
        }

        if (p->recstartts >= p->recendts)
        {
            // start/end-offsets are invalid so ignore
            p->recstartts = p->startts;
            p->recendts = p->endts;
        }

        p->schedulerid = 
            p->startts.toString() + "_" + p->chanid;

        // Chedk to see if the program is currently recording and if
        // the end time was changed.  Ideally, checking for a new end
        // time should be done after PruneOverlaps, but that would
        // complicate the list handling.  Do it here unless it becomes
        // problematic.
        RecIter rec = worklist.begin();
        for ( ; rec != worklist.end(); rec++)
        {
            ProgramInfo *r = *rec;
            if (p->IsSameTimeslot(*r))
            {
                if (r->inputid == p->inputid &&
                    r->recendts != p->recendts &&
                    (r->recordid == p->recordid ||
                     p->rectype == kOverrideRecord))
                    ChangeRecordingEnd(r, p);
                delete p;
                p = NULL;
                break;
            }
        }
        if (p == NULL)
            continue;

        // Check for rsOffLine
        if ((threadrunning || specsched) && !cardMap.contains(p->cardid))
            p->recstatus = rsOffLine;

        // Check for rsTooManyRecordings
        if (checkTooMany && tooManyMap[p->recordid] && !p->reactivate)
            p->recstatus = rsTooManyRecordings;

        // Check for rsCurrentRecording and rsPreviousRecording
        if (p->rectype == kDontRecord)
            p->recstatus = rsDontRecord;
        else if (result.value(15).toInt() && !p->reactivate)
            p->recstatus = rsPreviousRecording;
        else if (p->rectype != kSingleRecord &&
                 p->rectype != kOverrideRecord &&
                 !p->reactivate &&
                 !(p->dupmethod & kDupCheckNone))
        {
            if ((p->dupin & kDupsNewEpi) && p->repeat)
                p->recstatus = rsRepeat;

            if ((p->dupin & kDupsInOldRecorded) && result.value(10).toInt())
            {
                if (result.value(44).toInt() == rsNeverRecord)
                    p->recstatus = rsNeverRecord;
                else
                    p->recstatus = rsPreviousRecording;
            }
            if ((p->dupin & kDupsInRecorded) && result.value(14).toInt())
                p->recstatus = rsCurrentRecording;
        }

        if (inactive)
            p->recstatus = rsInactive;

        // Mark anything that has already passed as missed.  If it
        // survives PruneOverlaps, it will get deleted or have its old
        // status restored in PruneRedundants.
        if (p->recendts < schedTime)
            p->recstatus = rsMissed;

        tmpList.push_back(p);
    }

    VERBOSE(VB_SCHEDULE, " +-- Cleanup...");
    RecIter tmp = tmpList.begin();
    for ( ; tmp != tmpList.end(); tmp++)
        worklist.push_back(*tmp);

    if (schedTmpRecord = "sched_temp_record")
    {
        result.prepare("DROP TABLE IF EXISTS sched_temp_record;");
        result.exec();
    }

    result.prepare("DROP TABLE IF EXISTS sched_temp_recorded;");
    result.exec();
}

void Scheduler::AddNotListed(void) {

    struct timeval dbstart, dbend;
    RecList tmpList;

    QString query = QString(
"SELECT RECTABLE.recordid, RECTABLE.type, RECTABLE.chanid, "
"RECTABLE.starttime, RECTABLE.startdate, RECTABLE.endtime, RECTABLE.enddate, "
"RECTABLE.startoffset, RECTABLE.endoffset, "
"RECTABLE.title, RECTABLE.subtitle, RECTABLE.description, "
"channel.channum, channel.callsign, channel.name "
"FROM RECTABLE "
" INNER JOIN channel ON (channel.chanid = RECTABLE.chanid) "
" LEFT JOIN recordmatch on RECTABLE.recordid = recordmatch.recordid "
"WHERE (type = %1 OR type = %2 OR type = %3 OR type = %4) "
" AND recordmatch.chanid IS NULL")
        .arg(kSingleRecord)
        .arg(kTimeslotRecord)
        .arg(kWeekslotRecord)
        .arg(kOverrideRecord);

    while (1)
    {
        int i = query.find("RECTABLE");
        if (i == -1) break;
        query = query.replace(i, strlen("RECTABLE"), recordTable);
    }

    VERBOSE(VB_SCHEDULE, QString(" |-- Start DB Query..."));

    gettimeofday(&dbstart, NULL);
    MSqlQuery result(dbConn);
    result.prepare(query);
    result.exec();
    gettimeofday(&dbend, NULL);

    if (!result.isActive())
    {
        MythContext::DBError("AddNotListed", result);
        return;
    }

    VERBOSE(VB_SCHEDULE, QString(" |-- %1 results in %2 sec. Processing...")
            .arg(result.size())
            .arg(((dbend.tv_sec  - dbstart.tv_sec) * 1000000 +
                  (dbend.tv_usec - dbstart.tv_usec)) / 1000000.0));

    QDateTime now = QDateTime::currentDateTime();

    while (result.next())
    {
        ProgramInfo *p = new ProgramInfo;
        p->recstatus = rsNotListed;
        p->recordid = result.value(0).toInt();
        p->rectype = RecordingType(result.value(1).toInt());
        p->chanid = result.value(2).toString();

        p->startts.setTime(result.value(3).toTime());
        p->startts.setDate(result.value(4).toDate());
        p->endts.setTime(result.value(5).toTime());
        p->endts.setDate(result.value(6).toDate());

        if (p->rectype == kTimeslotRecord)
        {
            int days = p->startts.daysTo(now);

            p->startts = p->startts.addDays(days);
            p->endts   = p->endts.addDays(days);

            if (p->endts < now)
            {
                p->startts = p->startts.addDays(1);
                p->endts   = p->endts.addDays(1);
            }
        }
        else if (p->rectype == kWeekslotRecord)
        {
            int weeks = (p->startts.daysTo(now) + 6) / 7; 

            p->startts = p->startts.addDays(weeks * 7);
            p->endts   = p->endts.addDays(weeks * 7);

            if (p->endts < now)
            {
                p->startts = p->startts.addDays(7);
                p->endts   = p->endts.addDays(7);
            }
        }

        p->recstartts = p->startts.addSecs(result.value(7).toInt() * -60);
        p->recendts = p->endts.addSecs(result.value(8).toInt() * 60);

        if (p->recstartts >= p->recendts)
        {
            // start/end-offsets are invalid so ignore
            p->recstartts = p->startts;
            p->recendts = p->endts;
        }

        // Don't bother if the end time has already passed
        if (p->recendts < schedTime)
        {
            delete p;
            continue;
        }

        p->title = QString::fromUtf8(result.value(9).toString());

        if (p->rectype == kSingleRecord || p->rectype == kOverrideRecord)
        {
            p->subtitle = QString::fromUtf8(result.value(10).toString());
            p->description = QString::fromUtf8(result.value(11).toString());
        }
        p->chanstr = result.value(12).toString();
        p->chansign = QString::fromUtf8(result.value(13).toString());
        p->channame = QString::fromUtf8(result.value(14).toString());

        p->schedulerid = p->startts.toString() + "_" + p->chanid;

        if (p == NULL)
            continue;

        tmpList.push_back(p);
    }

    RecIter tmp = tmpList.begin();
    for ( ; tmp != tmpList.end(); tmp++)
        worklist.push_back(*tmp);
}

void Scheduler::findAllScheduledPrograms(RecList &proglist)
{
    QString temptime, tempdate;
    QString query = QString("SELECT RECTABLE.chanid, RECTABLE.starttime, "
"RECTABLE.startdate, RECTABLE.endtime, RECTABLE.enddate, RECTABLE.title, "
"RECTABLE.subtitle, RECTABLE.description, RECTABLE.recpriority, RECTABLE.type, "
"channel.name, RECTABLE.recordid, RECTABLE.recgroup, RECTABLE.dupin, "
"RECTABLE.dupmethod, channel.commmethod, channel.channum, RECTABLE.station, "
"RECTABLE.seriesid, RECTABLE.programid, RECTABLE.category, RECTABLE.findid, "
"RECTABLE.playgroup "
"FROM RECTABLE "
"LEFT JOIN channel ON channel.callsign = RECTABLE.station "
"GROUP BY recordid "
"ORDER BY title ASC;");

    while (1)
    {
        int i = query.find("RECTABLE");
        if (i == -1) break;
        query = query.replace(i, strlen("RECTABLE"), recordTable);
    }

    MSqlQuery result(MSqlQuery::InitCon());
    result.prepare(query);
    result.exec();

    if (!result.isActive())
    {
        MythContext::DBError("findAllScheduledPrograms", result);
        return;
    }
    if (result.size() > 0)
    {
        while (result.next()) 
        {
            ProgramInfo *proginfo = new ProgramInfo;
            proginfo->chanid = result.value(0).toString();
            proginfo->rectype = RecordingType(result.value(9).toInt());
            proginfo->recordid = result.value(11).toInt();

            if (proginfo->rectype == kSingleRecord   || 
                proginfo->rectype == kDontRecord     ||
                proginfo->rectype == kOverrideRecord ||
                proginfo->rectype == kTimeslotRecord ||
                proginfo->rectype == kWeekslotRecord) 
            {
                proginfo->startts = QDateTime(result.value(2).toDate(),
                                              result.value(1).toTime());
                proginfo->endts = QDateTime(result.value(4).toDate(),
                                            result.value(3).toTime());
            }
            else 
            {
                // put currentDateTime() in time fields to prevent
                // Invalid date/time warnings later
                proginfo->startts = QDateTime::currentDateTime();
                proginfo->startts.setTime(QTime(0,0));
                proginfo->endts = QDateTime::currentDateTime();
                proginfo->endts.setTime(QTime(0,0));
            }

            proginfo->title = QString::fromUtf8(result.value(5).toString());
            proginfo->subtitle =
                QString::fromUtf8(result.value(6).toString());
            proginfo->description =
                QString::fromUtf8(result.value(7).toString());

            proginfo->recpriority = result.value(8).toInt();
            proginfo->channame =
                QString::fromUtf8(result.value(10).toString());
            if (proginfo->channame.isNull())
                proginfo->channame = "";
            proginfo->recgroup =
                QString::fromUtf8(result.value(12).toString());
            proginfo->playgroup =
                QString::fromUtf8(result.value(22).toString());
            proginfo->dupin = RecordingDupInType(result.value(13).toInt());
            proginfo->dupmethod =
                RecordingDupMethodType(result.value(14).toInt());
            proginfo->chancommfree = (result.value(15).toInt() == -2);
            proginfo->chanstr = result.value(16).toString();
            if (proginfo->chanstr.isNull())
                proginfo->chanstr = "";
            proginfo->chansign = 
                QString::fromUtf8(result.value(17).toString());
            proginfo->seriesid = result.value(18).toString();
            proginfo->programid = result.value(19).toString();
            proginfo->category = 
                QString::fromUtf8(result.value(20).toString());
            proginfo->findid = result.value(21).toInt();
            
            proginfo->recstartts = proginfo->startts;
            proginfo->recendts = proginfo->endts;

            proglist.push_back(proginfo);
        }
    }
}

// Sort mode-preferred to least-preferred
static bool comp_dirpreference(FileSystemInfo *a, FileSystemInfo *b)
{
    // local over remote
    if (a->isLocal && !b->isLocal)
    {
        if (a->weight <= b->weight)
        {
            return true;
        }
    }
    else if (a->isLocal == b->isLocal)
    {
        if (a->weight < b->weight)
        {
            return true;
        }
        else if (a->weight > b->weight)
        {
            return false;
        }
        else if (a->freeSpaceKB > b->freeSpaceKB)
        {
            return true;
        }
    }
    else if (!a->isLocal && b->isLocal)
    {
        if (a->weight < b->weight)
        {
            return true;
        }
    }

    return false;
}

void Scheduler::GetNextLiveTVDir(int cardid)
{
    QMutexLocker lockit(reclist_lock);

    ProgramInfo *pginfo = new ProgramInfo;

    if (!pginfo)
        return;

    EncoderLink *tv = (*m_tvList)[cardid];

    if (tv->IsLocal())
        pginfo->hostname = gContext->GetHostName();
    else
        pginfo->hostname = tv->GetHostName();

    pginfo->storagegroup = "LiveTV";
    pginfo->recstartts   = mythCurrentDateTime();
    pginfo->recendts     = pginfo->recstartts.addSecs(3600);
    pginfo->title        = "LiveTV";
    pginfo->cardid       = cardid;

    int fsID = FillRecordingDir(pginfo, reclist);
    if (expirer)
    {
        // update auto expirer
        expirer->Update(cardid, fsID, true);
    }

    VERBOSE(VB_FILE, LOC + QString("FindNextLiveTVDir: next dir is '%1'")
            .arg(pginfo->pathname));

    tv->SetNextLiveTVDir(pginfo->pathname);

    delete pginfo;
}

int Scheduler::FillRecordingDir(ProgramInfo *pginfo, RecList& reclist)
{

    VERBOSE(VB_SCHEDULE, LOC + "FillRecordingDir: Starting");

    int fsID = -1;
    MSqlQuery query(MSqlQuery::InitCon());
    QMap<QString, FileSystemInfo>::Iterator fsit;
    QMap<QString, FileSystemInfo>::Iterator fsit2;
    QString dirKey;
    QStringList strlist;
    ProgramInfo *thispg;
    RecIter recIter;
    StorageGroup mysgroup(pginfo->storagegroup, pginfo->hostname);
    QStringList dirlist = mysgroup.GetDirList();
    QStringList recsCounted;
    list<FileSystemInfo *> fsInfoList;
    list<FileSystemInfo *>::iterator fslistit;

    if (dirlist.size() == 1)
    {
        VERBOSE(VB_FILE|VB_SCHEDULE, LOC + QString("FillRecordingDir: The only "
                "directory in the %1 Storage Group is %2, so it will be used "
                "by default.")
                .arg(pginfo->storagegroup)
                .arg(dirlist[0]));
        pginfo->pathname = dirlist[0];
        VERBOSE(VB_SCHEDULE, LOC + "FillRecordingDir: Finished");

        return -1;
    }

    int weightPerRecording =
            gContext->GetNumSetting("SGweightPerRecording", 10);
    int weightPerPlayback =
            gContext->GetNumSetting("SGweightPerPlayback", 5);
    int weightPerCommFlag =
            gContext->GetNumSetting("SGweightPerCommFlag", 5);
    int weightPerTranscode =
            gContext->GetNumSetting("SGweightPerTranscode", 5);
    int localStartingWeight =
            gContext->GetNumSetting("SGweightLocalStarting",
                                    (int)(-1.99 * weightPerRecording));
    int maxOverlap = gContext->GetNumSetting("SGmaxRecOverlapMins", 3) * 60;

    FillDirectoryInfoCache();
    
    VERBOSE(VB_FILE|VB_SCHEDULE, LOC +
            "FillRecordingDir: Calculating initial FS Weights.");

    for (fsit = fsInfoCache.begin(); fsit != fsInfoCache.end(); fsit++)
    {
        FileSystemInfo *fs = &(fsit.data());
        int tmpWeight = 0;

        QString msg = QString("  %1:%2").arg(fs->hostname)
                              .arg(fs->directory);
        // allow local dives to have 2 recordings before we prefer remote
        if (fs->isLocal)
        {
            tmpWeight = localStartingWeight;
            msg += " is local (" + QString::number(tmpWeight) + ")";
        }
        else
        {
            tmpWeight = 0;
            msg += " is remote (+" + QString::number(tmpWeight) + ")";
        }

        fs->weight = tmpWeight;

        tmpWeight = gContext->GetNumSetting(QString("SGweightPerDir:%1:%2")
                                .arg(fs->hostname).arg(fs->directory), 0);
        fs->weight += tmpWeight;

        if (tmpWeight)
            msg += ", has SGweightPerDir offset of "
                   + QString::number(tmpWeight) + ")";

        msg += ". initial dir weight = " + QString::number(fs->weight);
        VERBOSE(VB_FILE|VB_SCHEDULE, msg);

        fsInfoList.push_back(fs);
    }

    VERBOSE(VB_FILE|VB_SCHEDULE, LOC +
            "FillRecordingDir: Adjusting FS Weights from inuseprograms.");

    query.prepare("SELECT i.chanid, i.starttime, r.endtime, recusage, "
                      "rechost, recdir "
                  "FROM inuseprograms i, recorded r "
                  "WHERE DATE_ADD(lastupdatetime, INTERVAL 16 MINUTE) > NOW() "
                    "AND recdir <> '' "
                    "AND i.chanid = r.chanid "
                    "AND i.starttime = r.starttime;");
    if (!query.exec() || !query.isActive())
        MythContext::DBError(LOC + "FillRecordingDir", query);
    else
    {
        int recChanid;
        QDateTime recStart;
        QDateTime recEnd;
        QString recUsage;
        QString recHost;
        QString recDir;

        while (query.next())
        {
            recChanid = query.value(0).toInt();
            recStart  = query.value(1).toDateTime();
            recEnd    = query.value(2).toDateTime();
            recUsage  = query.value(3).toString();
            recHost   = query.value(4).toString();
            recDir    = query.value(5).toString();

            for (fslistit = fsInfoList.begin();
                 fslistit != fsInfoList.end(); fslistit++)
            {
                FileSystemInfo *fs = *fslistit;
                if ((recHost == fs->hostname) &&
                    (recDir == fs->directory))
                {
                    int weightOffset = 0;

                    if (recUsage == "recorder")
                    {
                        if (recEnd > pginfo->recstartts.addSecs(maxOverlap))
                        {
                            weightOffset += weightPerRecording;
                            recsCounted << QString::number(recChanid) + ":" +
                                           recStart.toString(Qt::ISODate);
                        }
                    }
                    else if (recUsage == "player")
                        weightOffset += weightPerPlayback;
                    else if (recUsage == "flagger")
                        weightOffset += weightPerCommFlag;
                    else if (recUsage == "transcoder")
                        weightOffset += weightPerTranscode;

                    if (weightOffset)
                    {
                        VERBOSE(VB_FILE|VB_SCHEDULE, QString(
                                "  %1 @ %2 in use by '%3' on %4:%5, FSID #%6, "
                                "FSID weightOffset +%7.")
                                .arg(recChanid)
                                .arg(recStart.toString(Qt::ISODate))
                                .arg(recUsage).arg(recHost).arg(recDir)
                                .arg(fs->fsID).arg(weightOffset));

                        // need to offset all directories on this filesystem
                        for (fsit2 = fsInfoCache.begin();
                             fsit2 != fsInfoCache.end(); fsit2++)
                        {
                            FileSystemInfo *fs2 = &(fsit2.data());
                            if (fs2->fsID == fs->fsID)
                            {
                                VERBOSE(VB_FILE|VB_SCHEDULE, QString("    "
                                        "%1:%2 => old weight %3 plus %4 = %5")
                                        .arg(fs2->hostname).arg(fs2->directory)
                                        .arg(fs2->weight).arg(weightOffset)
                                        .arg(fs2->weight + weightOffset));

                                fs2->weight += weightOffset;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    VERBOSE(VB_FILE|VB_SCHEDULE, LOC +
            "FillRecordingDir: Adjusting FS Weights from scheduler.");

    for (recIter = reclist.begin(); recIter != reclist.end(); recIter++)
    {
        thispg = *recIter;

        if ((pginfo->recendts < thispg->recstartts) ||
            (pginfo->recstartts > thispg->recendts) ||
            (thispg->recstatus != rsWillRecord) ||
            (thispg->cardid == 0) ||
            (recsCounted.contains(thispg->chanid + ":" +
                thispg->recstartts.toString(Qt::ISODate))) ||
            (thispg->pathname == ""))
            continue;

        if (thispg->pathname != "")
        {
            for (fslistit = fsInfoList.begin();
                 fslistit != fsInfoList.end(); fslistit++)
            {
                FileSystemInfo *fs = *fslistit;
                if ((fs->hostname == thispg->hostname) &&
                    (fs->directory == thispg->pathname))
                {
                    VERBOSE(VB_FILE|VB_SCHEDULE, QString(
                            "%1 @ %2 will record on %3:%4, FSID #%5, "
                            "weightPerRecording +%6.")
                            .arg(thispg->chanid)
                            .arg(thispg->recstartts.toString(Qt::ISODate))
                            .arg(fs->hostname).arg(fs->directory)
                            .arg(fs->fsID).arg(weightPerRecording));

                    for (fsit2 = fsInfoCache.begin();
                         fsit2 != fsInfoCache.end(); fsit2++)
                    {
                        FileSystemInfo *fs2 = &(fsit2.data());
                        if (fs2->fsID == fs->fsID)
                        {
                            VERBOSE(VB_FILE|VB_SCHEDULE, QString("    "
                                    "%1:%2 => old weight %3 plus %4 = %5")
                                    .arg(fs2->hostname).arg(fs2->directory)
                                    .arg(fs2->weight).arg(weightPerRecording)
                                    .arg(fs2->weight + weightPerRecording));

                            fs2->weight += weightPerRecording;
                        }
                    }
                    break;
                }
            }
        }
    }

    fsInfoList.sort(comp_dirpreference);

    if (print_verbose_messages & (VB_FILE|VB_SCHEDULE))
    {
        cout << "--- FillRecordingDir Sorted fsInfoList start ---\n";
        for (fslistit = fsInfoList.begin();fslistit != fsInfoList.end();
             fslistit++)
        {
            FileSystemInfo *fs = *fslistit;
            cout << fs->hostname << ":" << fs->directory << endl;
            cout << "    Location    : ";
            if (fs->isLocal)
                cout << "local" << endl;
            else
                cout << "remote" << endl;
            cout << "    weight      : " << fs->weight << endl;
            cout << "    free space  : " << fs->freeSpaceKB << endl;
            cout << endl;
        }
        cout << "--- FillRecordingDir Sorted fsInfoList end ---\n";
    }

    // This code could probably be expanded to check the actual bitrate the
    // recording will record at for analog broadcasts that are encoded locally.
    EncoderLink *nexttv = (*m_tvList)[pginfo->cardid];
    long long maxByterate = nexttv->GetMaxBitrate() / 8;
    long long maxSizeKB = maxByterate *
                          pginfo->recstartts.secsTo(pginfo->recendts) / 1024;

    // Loop though looking for a directory to put the file in.  The first time
    // through we look for directories with enough free space in them.  If we
    // can't find a directory that way we loop through and pick the first good
    // one from the list no matter how much free space it has.  We assume that
    // something will have to be expired for us to finish the recording.
    for (unsigned int pass = 1; pass <= 2; pass++)
    {
        bool foundDir = false;
        for (fslistit = fsInfoList.begin();
            fslistit != fsInfoList.end(); fslistit++)
        {
            long long desiredSpaceKB = 0;
            FileSystemInfo *fs = *fslistit;
            if (expirer)
                desiredSpaceKB = expirer->GetDesiredSpace(fs->fsID);

            if ((fs->hostname == pginfo->hostname) &&
                (dirlist.contains(fs->directory)) &&
                ((pass == 2) ||
                 (fs->freeSpaceKB > (desiredSpaceKB + maxSizeKB))))
            {
                pginfo->pathname = fs->directory;
                fsID = fs->fsID;

                if (pass == 1)
                    VERBOSE(VB_FILE, QString("'%1' will record in '%2' which "
                            "has %3 MiB free. This recording could use a max "
                            "of %4 MiB and the AutoExpirer wants to keep %5 "
                            "MiB free.")
                            .arg(pginfo->title).arg(pginfo->pathname)
                            .arg(fs->freeSpaceKB / 1024).arg(maxSizeKB / 1024)
                            .arg(desiredSpaceKB / 1024));
                else
                    VERBOSE(VB_FILE, QString("'%1' will record in '%2' "
                            "although there is only %3 MiB free and the "
                            "AutoExpirer wants at least %4 MiB.  Something "
                            "will have to be deleted or expired in order for "
                            "this recording to complete successfully.")
                            .arg(pginfo->title).arg(pginfo->pathname)
                            .arg(fs->freeSpaceKB / 1024)
                            .arg(desiredSpaceKB / 1024));

                foundDir = true;
                break;
            }
        }

        if (foundDir)
            break;
    }

    VERBOSE(VB_SCHEDULE, LOC + "FillRecordingDir: Finished");
    return fsID;
}

void Scheduler::FillDirectoryInfoCache(bool force)
{
    if ((!force) &&
        (fsInfoCacheFillTime > QDateTime::currentDateTime().addSecs(-180)))
        return;

    vector<FileSystemInfo> fsInfos;

    fsInfoCache.clear();

    GetFilesystemInfos(m_tvList, fsInfos);

    QMap <int, bool> fsMap;
    vector<FileSystemInfo>::iterator it1;
    for (it1 = fsInfos.begin(); it1 != fsInfos.end(); it1++)
    {
        fsMap[it1->fsID] = true;
        fsInfoCache[it1->hostname + ":" + it1->directory] = *it1;
    }

    VERBOSE(VB_FILE, LOC + QString("FillDirectoryInfoCache: found %1 unique "
            "filesystems").arg(fsMap.size()));

    fsInfoCacheFillTime = QDateTime::currentDateTime();
}

void Scheduler::SchedPreserveLiveTV(void)
{
    if (!livetvTime.isValid())
        return;

    if (livetvTime < schedTime)
    {
        livetvTime = QDateTime();
        return;
    }
    
    livetvpriority = gContext->GetNumSetting("LiveTVPriority", 0);

    // Build a list of active livetv programs
    QMap<int, EncoderLink *>::Iterator enciter = m_tvList->begin();
    for (; enciter != m_tvList->end(); ++enciter)
    {
        EncoderLink *enc = enciter.data();

        if (kState_WatchingLiveTV != enc->GetState())
            continue;

        TunedInputInfo in;
        enc->IsBusy(&in);

        if (!in.inputid)
            continue;

        // Get the program that will be recording on this channel
        // at record start time, if this LiveTV session continues.
        ProgramInfo *dummy =
            dummy->GetProgramAtDateTime(QString::number(in.chanid),
                                        livetvTime, true, 4);
        if (!dummy)
            continue;

        dummy->cardid = enc->GetCardID();
        dummy->inputid = in.inputid;
        dummy->recstatus = rsUnknown;

        retrylist.push_front(dummy);
    }

    if (!retrylist.size())
        return;

    MoveHigherRecords(false);

    while (retrylist.size() > 0)
    {
        ProgramInfo *p = retrylist.back();
        delete p;
        retrylist.pop_back();
    }
}

/* Determines if the system was started by the auto-wakeup process */
bool Scheduler::WasStartedAutomatically()
{
    bool autoStart = false;

    QDateTime startupTime = QDateTime();
    QString s = gContext->GetSetting("MythShutdownWakeupTime", "");
    if (s != "")
        startupTime = QDateTime::fromString(s, Qt::ISODate);

    // if we don't have a valid startup time assume we were started manually
    if (startupTime.isValid())
    {
        // if we started within 15mins of the saved wakeup time assume we
        // started automatically to record or for a daily wakeup/shutdown period

        if (abs(startupTime.secsTo(QDateTime::currentDateTime())) < (15 * 60))
        {
            VERBOSE(VB_SCHEDULE,
                    "Close to auto-start time, AUTO-Startup assumed");
            autoStart = true;
        }
    }

    return autoStart;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
