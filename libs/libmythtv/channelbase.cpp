// Std C headers
#include <cstdio>
#include <cstdlib>
#include <cerrno>

// POSIX headers
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

// C++ headers
#include <iostream>
#include <algorithm>
using namespace std;

// MythTV headers
#include "videodev_myth.h"
#include "channel.h"
#include "frequencies.h"
#include "tv_rec.h"
#include "mythcontext.h"
#include "exitcodes.h"
#include "mythdbcon.h"
#include "cardutil.h"
#include "channelutil.h"
#include "remoteutil.h"
#include "sourceutil.h"
#include "cardutil.h"
#include "compat.h"

#define LOC QString("ChannelBase(%1): ").arg(GetCardID())
#define LOC_WARN QString("ChannelBase(%1) Warning: ").arg(GetCardID())
#define LOC_ERR QString("ChannelBase(%1) Error: ").arg(GetCardID())

ChannelBase::ChannelBase(TVRec *parent)
    : 
    pParent(parent), curchannelname(""),
    currentInputID(-1), commfree(false), cardid(0)
{
}

ChannelBase::~ChannelBase(void)
{
}

bool ChannelBase::Init(QString &inputname, QString &startchannel, bool setchan)
{
    bool ok;

    if (!setchan)
        ok = inputname.isEmpty() ? false : IsTunable(inputname, startchannel);
    else if (inputname.isEmpty())
        ok = SetChannelByString(startchannel);
    else
        ok = SwitchToInput(inputname, startchannel);

    if (ok)
        return true;

    // try to find a valid channel if given start channel fails.
    QString msg1 = QString("Setting start channel '%1' failed, ")
        .arg(startchannel);
    QString msg2 = "and we failed to find any suitible channels on any input.";
    bool msg_error = true;

    QStringList inputs = GetConnectedInputs();
    QStringList::const_iterator start =
        find(inputs.begin(), inputs.end(), inputname);
    start = (start == inputs.end()) ?  inputs.begin() : start;

    if (start != inputs.end())
    {
        VERBOSE(VB_CHANNEL, LOC +
                QString("Looking for startchannel '%1' on input '%2'")
                .arg(startchannel).arg(*start));
    }

    // Attempt to find an input for the requested startchannel
    QStringList::const_iterator it = start;
    while (it != inputs.end())
    {
        DBChanList channels = GetChannels(*it);

        DBChanList::const_iterator cit = channels.begin();
        for (; cit != channels.end(); cit++)
        {
            if ((*cit).channum == startchannel &&
                IsTunable(*it, startchannel))
            {
                inputname = *it;
                VERBOSE(VB_CHANNEL, LOC +
                        QString("Found startchannel '%1' on input '%2'")
                        .arg(startchannel).arg(inputname));
                return true;
            }
        }

        it++;
        it = (it == inputs.end()) ? inputs.begin() : it;
        if (it == start)
            break;
    }

    it = start;
    while (it != inputs.end() && !ok)
    {
        uint mplexid_restriction = 0;

        DBChanList channels = GetChannels(*it);
        if (channels.size() &&
            IsInputAvailable(GetInputByName(*it), mplexid_restriction))
        {
            uint chanid = ChannelUtil::GetNextChannel(
                channels, channels[0].chanid,
                mplexid_restriction, CHANNEL_DIRECTION_UP);

            DBChanList::const_iterator cit =
                find(channels.begin(), channels.end(), chanid);

            if (chanid && cit != channels.end())
            {
                if (!setchan)
                {
                    ok = IsTunable(*it, (mplexid_restriction) ?
                                   (*cit).channum : startchannel);
                }
                else
                    ok = SwitchToInput(*it, (*cit).channum);

                if (ok)
                {
                    inputname = *it;
                    if (mplexid_restriction)
                        startchannel = QDeepCopy<QString>((*cit).channum);
                    msg2 = QString("selected to '%1' on input '%2' instead.")
                        .arg(startchannel).arg(inputname);
                    msg_error = false;
                }
            }
        }

        it++;
        it = (it == inputs.end()) ? inputs.begin() : it;
        if (it == start)
            break;
    }

    VERBOSE(((msg_error) ? VB_IMPORTANT : VB_GENERAL),
            ((msg_error) ? LOC_ERR : LOC_WARN) +
            msg1 + "\n\t\t\t" + msg2);

    return ok;
}

bool ChannelBase::IsTunable(const QString &input, const QString &channum) const
{
    QString loc = LOC + QString("IsTunable(%1,%2)").arg(input).arg(channum);

    int inputid = currentInputID;
    if (!input.isEmpty())
        inputid = GetInputByName(input);

    InputMap::const_iterator it = inputs.find(inputid);
    if (it == inputs.end())
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Requested non-existant input '%1':'%2' ")
                .arg(input).arg(inputid));

        return false;
    }

    uint mplexid_restriction;
    if (!IsInputAvailable(inputid, mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Requested channel is on input '%1' "
                    "which is in a busy input group")
                .arg(inputid));

        return false;
    }

    // Fetch tuning data from the database.
    QString tvformat, modulation, freqtable, freqid, dtv_si_std;
    int finetune;
    uint64_t frequency;
    int mpeg_prog_num;
    uint atsc_major, atsc_minor, mplexid, tsid, netid;
    bool commfree;

    if (!ChannelUtil::GetChannelData(
        (*it)->sourceid, channum,
        tvformat, modulation, freqtable, freqid,
        finetune, frequency,
        dtv_si_std, mpeg_prog_num, atsc_major, atsc_minor, tsid, netid,
        mplexid, commfree))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Failed to find channel in DB on input '%1' ")
                .arg(inputid));

        return false;
    }

    if (mplexid_restriction && (mplexid != mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Channel is valid, but tuner is busy "
                    "on different multiplex (%1 != %2)")
                .arg(mplexid).arg(mplexid_restriction));

        return false;
    }

    return true;
}

uint ChannelBase::GetNextChannel(uint chanid, int direction) const
{
    if (!chanid)
    {
        InputMap::const_iterator it = inputs.find(currentInputID);
        if (it == inputs.end())
            return 0;

        chanid = ChannelUtil::GetChanID((*it)->sourceid, curchannelname);
    }

    uint mplexid_restriction = 0;
    IsInputAvailable(currentInputID, mplexid_restriction);

    return ChannelUtil::GetNextChannel(
        allchannels, chanid, mplexid_restriction, direction);
}

uint ChannelBase::GetNextChannel(const QString &channum, int direction) const
{
    InputMap::const_iterator it = inputs.find(currentInputID);
    if (it == inputs.end())
        return 0;

    uint chanid = ChannelUtil::GetChanID((*it)->sourceid, channum);
    return GetNextChannel(chanid, direction);
}

int ChannelBase::GetNextInputNum(void) const
{
    // Exit early if inputs don't exist..
    if (!inputs.size())
        return -1;

    // Find current input
    InputMap::const_iterator it;
    it = inputs.find(currentInputID);

    // If we can't find the current input, start at
    // the beginning and don't increment initially.
    bool skip_incr = false;
    if (it == inputs.end())
    {
        it = inputs.begin();
        skip_incr = true;
    }

    // Find the next _connected_ input.
    int i = 0;
    for (; i < 100; i++)
    {
        if (!skip_incr)
        {
            ++it;
            it = (it == inputs.end()) ? inputs.begin() : it;
        }
        skip_incr = false;

        if ((*it)->sourceid)
            break;
    }

    // if we found anything, including current cap channel return it
    return (i<100) ? (int)it.key() : -1;
}

/** \fn ChannelBase::GetConnectedInputs(void) const
 *  \brief Returns names of connected inputs
 */
QStringList ChannelBase::GetConnectedInputs(void) const
{
    QStringList list;

    InputMap::const_iterator it = inputs.begin();
    for (; it != inputs.end(); ++it)
        if ((*it)->sourceid)
            list.push_back((*it)->name);

    return list;
}

/** \fn ChannelBase::GetInputByNum(int capchannel) const
 *  \brief Returns name of numbered input, returns null if not found.
 */
QString ChannelBase::GetInputByNum(int capchannel) const
{
    InputMap::const_iterator it = inputs.find(capchannel);
    if (it != inputs.end())
        return (*it)->name;
    return QString::null;
}

/** \fn ChannelBase::GetInputByName(const QString &input) const
 *  \brief Returns number of named input, returns -1 if not found.
 */
int ChannelBase::GetInputByName(const QString &input) const
{
    InputMap::const_iterator it = inputs.begin();
    for (; it != inputs.end(); ++it)
    {
        if ((*it)->name == input)
            return (int)it.key();
    }
    return -1;
}

bool ChannelBase::SwitchToInput(const QString &inputname)
{
    int input = GetInputByName(inputname);

    if (input >= 0)
        return SwitchToInput(input, true);
    else
        VERBOSE(VB_IMPORTANT, QString("ChannelBase: Could not find input: "
                                      "%1 on card\n").arg(inputname));
    return false;
}

bool ChannelBase::SwitchToInput(const QString &inputname, const QString &chan)
{
    int input = GetInputByName(inputname);

    bool ok = false;
    if (input >= 0)
    {
        ok = SwitchToInput(input, false);
        if (ok)
            ok = SetChannelByString(chan);
    }
    else
    {
        VERBOSE(VB_IMPORTANT,
                QString("ChannelBase: Could not find input: %1 on card when "
                        "setting channel %2\n").arg(inputname).arg(chan));
    }
    return ok;
}

bool ChannelBase::SwitchToInput(int newInputNum, bool setstarting)
{
    InputMap::const_iterator it = inputs.find(newInputNum);
    if (it == inputs.end() || (*it)->startChanNum.isEmpty())
        return false;

    uint mplexid_restriction;
    if (!IsInputAvailable(newInputNum, mplexid_restriction))
        return false;

    // input switching code would go here

    if (setstarting)
        return SetChannelByString((*it)->startChanNum);

    return true;
}

static bool is_input_group_busy(
    uint                       inputid,
    uint                       groupid,
    const vector<uint>        &excluded_cardids,
    QMap<uint,bool>           &busygrp,
    QMap<uint,bool>           &busyrec,
    QMap<uint,TunedInputInfo> &busyin,
    uint                      &mplexid_restriction)
{
    // If none are busy, we don't need to check further
    QMap<uint,bool>::const_iterator bit = busygrp.find(groupid);
    if ((bit != busygrp.end()) && !*bit)
        return false;

    vector<TunedInputInfo> conflicts;
    vector<uint> cardids = CardUtil::GetGroupCardIDs(groupid);
    for (uint i = 0; i < cardids.size(); i++)
    {
        if (find(excluded_cardids.begin(),
                 excluded_cardids.end(), cardids[i]) != excluded_cardids.end())
        {
            continue;
        }

        TunedInputInfo info;
        QMap<uint,bool>::const_iterator it = busyrec.find(cardids[i]);
        if (it == busyrec.end())
        {
            busyrec[cardids[i]] = RemoteIsBusy(cardids[i], info);
            it = busyrec.find(cardids[i]);
            if (*it)
                busyin[cardids[i]] = info;
        }

        if (*it)
            conflicts.push_back(busyin[cardids[i]]);
    }

    // If none are busy, we don't need to check further
    busygrp[groupid] = !conflicts.empty();
    if (conflicts.empty())
        return false;

    InputInfo in;
    in.inputid = inputid;
    if (!CardUtil::GetInputInfo(in))
        return true;

    // If they aren't using the same source they are definately busy
    bool is_busy_input = false;

    for (uint i = 0; i < conflicts.size() && !is_busy_input; i++)
        is_busy_input = (in.sourceid != conflicts[i].sourceid);

    if (is_busy_input)
        return true;

    // If the source's channels aren't digitally tuned then there is a conflict
    is_busy_input = !SourceUtil::HasDigitalChannel(in.sourceid);
    if (!is_busy_input && conflicts[0].chanid)
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.prepare(
            "SELECT mplexid "
            "FROM channel "
            "WHERE chanid = :CHANID");
        query.bindValue(":CHANID", conflicts[0].chanid);
        if (!query.exec())
            MythContext::DBError("is_input_group_busy", query);
        else if (query.next())
        {
            mplexid_restriction = query.value(0).toUInt();
            mplexid_restriction = (32767 == mplexid_restriction) ?
                0 : mplexid_restriction;
        }
    }

    return is_busy_input;
}

static bool is_input_busy(
    uint                       inputid,
    const vector<uint>        &groupids,
    const vector<uint>        &excluded_cardids,
    QMap<uint,bool>           &busygrp,
    QMap<uint,bool>           &busyrec,
    QMap<uint,TunedInputInfo> &busyin,
    uint                      &mplexid_restriction)
{
    bool is_busy = false;
    for (uint i = 0; i < groupids.size() && !is_busy; i++)
    {
        is_busy |= is_input_group_busy(
            inputid, groupids[i], excluded_cardids,
            busygrp, busyrec, busyin, mplexid_restriction);
    }
    return is_busy;
}

bool ChannelBase::IsInputAvailable(
    int inputid, uint &mplexid_restriction) const
{
    if (inputid < 0)
        return false;

    // Check each input to make sure it doesn't belong to an
    // input group which is attached to a busy recorder.
    QMap<uint,bool>           busygrp;
    QMap<uint,bool>           busyrec;
    QMap<uint,TunedInputInfo> busyin;

    // Cache our busy input if applicable
    uint cid = GetCardID();
    TunedInputInfo info;
    busyrec[cid] = pParent->IsBusy(&info);
    if (busyrec[cid])
    {
        busyin[cid] = info;
        info.chanid = GetChanID();
    }

    vector<uint> excluded_cardids;
    excluded_cardids.push_back(cid);

    mplexid_restriction = 0;

    vector<uint> groupids = CardUtil::GetInputGroups(inputid);

    return !is_input_busy(inputid, groupids, excluded_cardids,
                          busygrp, busyrec, busyin, mplexid_restriction);
}

/** \fn ChannelBase::GetFreeInputs(const vector<uint>&) const
 *  \brief Returns the recorders available inputs.
 *
 *   This filters out the connected inputs that belong to an input 
 *   group which is busy. Recorders in the excluded cardids will 
 *   not be considered busy for the sake of determining free inputs.
 *
 */
vector<InputInfo> ChannelBase::GetFreeInputs(
    const vector<uint> &excluded_cardids) const
{
    vector<InputInfo> new_list;

    QStringList list = GetConnectedInputs();
    if (list.empty())
        return new_list;

    // Check each input to make sure it doesn't belong to an
    // input group which is attached to a busy recorder.
    QMap<uint,bool>           busygrp;
    QMap<uint,bool>           busyrec;
    QMap<uint,TunedInputInfo> busyin;

    // Cache our busy input if applicable
    TunedInputInfo info;
    uint cid = GetCardID();
    busyrec[cid] = pParent->IsBusy(&info);
    if (busyrec[cid])
    {
        busyin[cid] = info;
        info.chanid = GetChanID();
    }

    // If we're busy and not excluded, all inputs are busy
    if (busyrec[cid] &&
        (find(excluded_cardids.begin(), excluded_cardids.end(),
              cid) == excluded_cardids.end()))
    {
        return new_list;
    }

    QStringList::const_iterator it;
    for (it = list.begin(); it != list.end(); ++it)
    {
        InputInfo info;
        vector<uint> groupids;
        info.inputid = GetInputByName(*it);

        if (!CardUtil::GetInputInfo(info, &groupids))
            continue;

        bool is_busy_grp = is_input_busy(
            info.inputid, groupids, excluded_cardids, 
            busygrp, busyrec, busyin, info.mplexid);

        if (!is_busy_grp)
            new_list.push_back(info);
    }

    return new_list;
}

uint ChannelBase::GetInputCardID(int inputNum) const
{
    InputMap::const_iterator it = inputs.find(inputNum);
    if (it != inputs.end())
        return (*it)->cardid;
    return 0;    
}

DBChanList ChannelBase::GetChannels(int inputNum) const
{
    int inputid = (inputNum > 0) ? inputNum : currentInputID;

    DBChanList ret;
    InputMap::const_iterator it = inputs.find(inputid);
    if (it != inputs.end())
        ret = (*it)->channels;

    return ret;
}

DBChanList ChannelBase::GetChannels(const QString &inputname) const
{
    int inputid = currentInputID;
    if (!inputname.isEmpty())
    {
        int tmp = GetInputByName(inputname);
        inputid = (tmp > 0) ? tmp : inputid;
    }

    return GetChannels(inputid);
}

bool ChannelBase::ChangeExternalChannel(const QString &channum)
{
#ifdef USING_MINGW
    VERBOSE(VB_IMPORTANT, LOC_WARN +
            QString("ChangeExternalChannel is not implemented in MinGW."));
    return false;
#else
    InputMap::const_iterator it = inputs.find(currentInputID);
    QString changer = (*it)->externalChanger;

    if (changer.isEmpty())
        return false;

    QString command = QString("%1 %2").arg(changer).arg(channum);

    VERBOSE(VB_CHANNEL, QString("External channel change: %1").arg(command));
    pid_t child = fork();
    if (child < 0)
    {   // error encountered in creating fork
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Fork error -- " + ENO);
        return false;
    }
    else if (child == 0)
    {   // we are the new fork
        for(int i = 3; i < sysconf(_SC_OPEN_MAX) - 1; ++i)
            close(i);
        int ret = execl("/bin/sh", "sh", "-c", command.ascii(), (char *)NULL);
        QString msg("ChannelBase: ");
        if (EACCES == ret) {
            msg.append(QString("Access denied to /bin/sh"
                               " when executing %1\n").arg(command.ascii()));
        }
        msg.append(strerror(errno));
        VERBOSE(VB_IMPORTANT, msg);
        _exit(CHANNEL__EXIT__EXECL_ERROR); // this exit is ok
    }
    else
    {   // child contains the pid of the new process
        int status = 0, pid = 0;
        VERBOSE(VB_CHANNEL, "Waiting for External Tuning program to exit");

        bool timed_out = false;
        uint timeout = 30; // how long to wait in seconds
        time_t start_time = time(0);
        while (-1 != pid && !timed_out)
        {
            sleep(1);
            pid = waitpid(child, &status, WUNTRACED|WNOHANG);
            VERBOSE(VB_IMPORTANT, QString("ret_pid(%1) child(%2) status(0x%3)")
                    .arg(pid).arg(child).arg(status,0,16));
            if (pid==child)
                break;
            else if (time(0) > (time_t)(start_time + timeout))
                timed_out = true;
        }
        if (timed_out)
        {
            VERBOSE(VB_IMPORTANT, "External Tuning program timed out, killing");
            kill(child, SIGTERM);
            usleep(500);
            kill(child, SIGKILL);
            return false;
        }

        VERBOSE(VB_CHANNEL, "External Tuning program no longer running");
        if (WIFEXITED(status))
        {   // program exited normally
            int ret = WEXITSTATUS(status);
            if (CHANNEL__EXIT__EXECL_ERROR == ret)
            {
                VERBOSE(VB_IMPORTANT, QString("ChannelBase: Could not execute "
                                              "external tuning program."));
                return false;
            }
            else if (ret)
            {   // external tuning program returned error value
                VERBOSE(VB_IMPORTANT,
                        QString("ChannelBase: external tuning program "
                                "exited with error %1").arg(ret));
                return false;
            }
            VERBOSE(VB_IMPORTANT, "External Tuning program exited with no error");
        }
        else
        {   // program exited abnormally
            QString msg = QString("ChannelBase: external tuning program "
                                  "encountered error %1 -- ").arg(errno);
            msg.append(strerror(errno));
            VERBOSE(VB_IMPORTANT, msg);
            return false;
        }
    }

    return true;
#endif // !USING_MINGW
}

/** \fn ChannelBase::GetCardID(void) const
 *  \brief Returns card id.
 */
int ChannelBase::GetCardID(void) const
{
    if (cardid > 0)
        return cardid;

    if (pParent)
        return pParent->GetCaptureCardNum();

    if (GetDevice().isEmpty())
        return -1;

    uint tmpcardid = CardUtil::GetFirstCardID(GetDevice());
    return (tmpcardid <= 0) ? -1 : tmpcardid;
}

int ChannelBase::GetChanID() const
{
    InputMap::const_iterator it = inputs.find(currentInputID);
    if (it == inputs.end())
        return false;

    MSqlQuery query(MSqlQuery::InitCon());

    query.prepare("SELECT chanid FROM channel "
                  "WHERE channum  = :CHANNUM AND "
                  "      sourceid = :SOURCEID");
    query.bindValue(":CHANNUM", curchannelname);
    query.bindValue(":SOURCEID", (*it)->sourceid);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("fetching chanid", query);
        return -1;
    }

    if (query.size() <= 0)
        return -1;

    query.next();
    return query.value(0).toInt();
}

/** \fn ChannelBase::InitializeInputs(void)
 *  \brief Fills in input map from DB
 */
bool ChannelBase::InitializeInputs(void)
{
    inputs.clear();
    
    uint cardid = max(GetCardID(), 0);
    if (!cardid)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "InitializeInputs(): "
                "Programmer error, cardid invalid.");
        return false;
    }

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT cardinputid, "
        "       inputname,   startchan, "
        "       tunechan,    externalcommand, "
        "       sourceid "
        "FROM cardinput "
        "WHERE cardid = :CARDID");
    query.bindValue(":CARDID", cardid);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("InitializeInputs", query);
        return false;
    }
    else if (!query.size())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "InitializeInputs(): "
                "\n\t\t\tCould not get inputs for the capturecard."
                "\n\t\t\tPerhaps you have forgotten to bind video"
                "\n\t\t\tsources to your card's inputs?");
        return false;
    }

    allchannels.clear();
    QString order = gContext->GetSetting("ChannelOrdering", "channum");
    while (query.next())
    {
        uint sourceid = query.value(5).toUInt();
        DBChanList channels = ChannelUtil::GetChannels(sourceid, false);

        ChannelUtil::SortChannels(channels, order);

        inputs[query.value(0).toUInt()] = new ChannelInputInfo(
            query.value(1).toString(), query.value(2).toString(),
            query.value(3).toString(), query.value(4).toString(),
            sourceid,                  cardid,
            query.value(0).toUInt(),   0,
            channels);

        allchannels.insert(allchannels.end(),
                           channels.begin(), channels.end());
    }
    ChannelUtil::SortChannels(allchannels, order);
    ChannelUtil::EliminateDuplicateChanNum(allchannels);

    // Set initial input to first connected input
    currentInputID = -1;
    currentInputID = GetNextInputNum();

    // print em
    InputMap::const_iterator it;
    for (it = inputs.begin(); it != inputs.end(); ++it)
    {
        VERBOSE(VB_CHANNEL, LOC + QString("Input #%1: '%2' schan(%3) "
                                          "sourceid(%4) ccid(%5)")
                .arg(it.key()).arg((*it)->name).arg((*it)->startChanNum)
                .arg((*it)->sourceid).arg((*it)->cardid));
    }
    VERBOSE(VB_CHANNEL, LOC + QString("Current Input #%1: '%2'")
            .arg(GetCurrentInputNum()).arg(GetCurrentInput()));

    return inputs.size();
}

/** \fn ChannelBase::Renumber(uint,const QString&,const QString&)
 *  \brief Changes a channum if we have it cached anywhere.
 */
void ChannelBase::Renumber(uint sourceid,
                           const QString &oldChanNum,
                           const QString &newChanNum)
{
    InputMap::iterator it = inputs.begin();

    for (; it != inputs.end(); ++it)
    {
        bool skip = ((*it)->name.isEmpty()                ||
                     (*it)->startChanNum.isEmpty()        ||
                     (*it)->startChanNum != oldChanNum ||
                     (*it)->sourceid     != sourceid);
        if (!skip)
            (*it)->startChanNum = newChanNum;
    }

    if (GetCurrentSourceID() == sourceid && oldChanNum == curchannelname)
        curchannelname = newChanNum;

    StoreInputChannels(inputs);
}

/** \fn ChannelBase::StoreInputChannels(const InputMap&)
 *  \brief Sets starting channel for the each input in the input map.
 *  \param input Map from cardinputid to input params.
 */
void ChannelBase::StoreInputChannels(const InputMap &inputs)
{
    MSqlQuery query(MSqlQuery::InitCon());
    InputMap::const_iterator it = inputs.begin();
    for (; it != inputs.end(); ++it)
    {
        if ((*it)->name.isEmpty() || (*it)->startChanNum.isEmpty())
            continue;

        query.prepare(
            "UPDATE cardinput "
            "SET startchan = :STARTCHAN "
            "WHERE cardinputid = :CARDINPUTID");
        query.bindValue(":STARTCHAN",   (*it)->startChanNum);
        query.bindValue(":CARDINPUTID", it.key());

        if (!query.exec() || !query.isActive())
            MythContext::DBError("StoreInputChannels", query);
    }
}

/** \fn ChannelBase::StoreDefaultInput(uint, const QString&)
 *  \brief Sets default input for the cardid
 *  \param cardid ChannelBase::GetCardID()
 *  \param input  ChannelBase::GetCurrentInput()
 */
void ChannelBase::StoreDefaultInput(uint cardid, const QString &input)
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "UPDATE capturecard "
        "SET defaultinput = :DEFAULTINPUT "
        "WHERE cardid = :CARDID");
    query.bindValue(":DEFAULTINPUT", input);
    query.bindValue(":CARDID", cardid);

    if (!query.exec() || !query.isActive())
        MythContext::DBError("StoreDefaultInput", query);
}

bool ChannelBase::CheckChannel(const QString &channum, 
                               QString& inputName) const
{
    inputName = "";
    
    bool ret = false;

    QString channelinput = GetCurrentInput();

    MSqlQuery query(MSqlQuery::InitCon());
    if (!query.isConnected())
        return false;

    query.prepare(
        "SELECT channel.chanid "
        "FROM channel, capturecard, cardinput "
        "WHERE channel.channum      = :CHANNUM           AND "
        "      channel.sourceid     = cardinput.sourceid AND "
        "      cardinput.inputname  = :INPUT             AND "
        "      cardinput.cardid     = capturecard.cardid AND "
        "      capturecard.cardid   = :CARDID            AND "
        "      capturecard.hostname = :HOSTNAME");
    query.bindValue(":CHANNUM",  channum);
    query.bindValue(":INPUT",    channelinput);
    query.bindValue(":CARDID",   GetCardID());
    query.bindValue(":HOSTNAME", gContext->GetHostName());

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("checkchannel", query);
    }
    else if (query.size() > 0)
    {
        return true;
    }

    QString msg = QString(
        "Failed to find channel(%1) on current input (%2) of card (%3).")
        .arg(channum).arg(channelinput).arg(GetCardID());
    VERBOSE(VB_CHANNEL, LOC + msg);

    // We didn't find it on the current input let's widen the search
    query.prepare(
        "SELECT channel.chanid, cardinput.inputname "
        "FROM channel, capturecard, cardinput "
        "WHERE channel.channum      = :CHANNUM           AND "
        "      channel.sourceid     = cardinput.sourceid AND "
        "      cardinput.cardid     = capturecard.cardid AND "
        "      capturecard.cardid   = :CARDID            AND "
        "      capturecard.hostname = :HOSTNAME");
    query.bindValue(":CHANNUM",  channum);
    query.bindValue(":CARDID",   GetCardID());
    query.bindValue(":HOSTNAME", gContext->GetHostName());

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("checkchannel", query);
    } 
    else if (query.size() > 0)
    {
        query.next();
        QString test = query.value(1).toString();
        if (test != QString::null)
            inputName = QString::fromUtf8(test);

        msg = QString("Found channel(%1) on another input (%2) of card (%3).")
            .arg(channum).arg(inputName).arg(GetCardID());
        VERBOSE(VB_CHANNEL, LOC + msg);

        return true;
    }

    msg = QString("Failed to find channel(%1) on any input of card (%2).")
        .arg(channum).arg(GetCardID());
    VERBOSE(VB_CHANNEL, LOC + msg);

    query.prepare("SELECT NULL FROM channel");

    if (query.exec() && query.size() == 0)
        ret = true;

    return ret;
}
