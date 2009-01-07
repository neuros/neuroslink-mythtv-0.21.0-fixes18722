#include <unistd.h>

#include "dtvchannel.h"
#include "dvbchannel.h"
#include "dtvsignalmonitor.h"
#include "scanstreamdata.h"
#include "mpegtables.h"
#include "atsctables.h"
#include "dvbtables.h"
#include "compat.h"

#undef DBG_SM
#define DBG_SM(FUNC, MSG) VERBOSE(VB_CHANNEL, \
    "DTVSM("<<channel->GetDevice()<<")::"<<FUNC<<": "<<MSG);

#define LOC QString("DTVSM(%1): ").arg(channel->GetDevice())
#define LOC_ERR QString("DTVSM(%1) Error: ").arg(channel->GetDevice())

/** \class DTVSignalMonitor
 *  \brief This class is intended to detect the presence of needed tables.
 */

DTVSignalMonitor::DTVSignalMonitor(int db_cardnum,
                                   DTVChannel *_channel,
                                   uint64_t wait_for_mask,
                                   const char *name)
    : SignalMonitor(db_cardnum, _channel, wait_for_mask, name),
      stream_data(NULL),
      seenPAT(tr("Seen")+" PAT", "seen_pat", 1, true, 0, 1, 0),
      seenPMT(tr("Seen")+" PMT", "seen_pmt", 1, true, 0, 1, 0),
      seenMGT(tr("Seen")+" MGT", "seen_mgt", 1, true, 0, 1, 0),
      seenVCT(tr("Seen")+" VCT", "seen_vct", 1, true, 0, 1, 0),
      seenNIT(tr("Seen")+" NIT", "seen_nit", 1, true, 0, 1, 0),
      seenSDT(tr("Seen")+" SDT", "seen_sdt", 1, true, 0, 1, 0),
      seenCrypt(tr("Seen")+" Crypt", "seen_crypt", 1, true, 0, 1, 0),
      matchingPAT(tr("Matching")+" PAT", "matching_pat", 1, true, 0, 1, 0),
      matchingPMT(tr("Matching")+" PMT", "matching_pmt", 1, true, 0, 1, 0),
      matchingMGT(tr("Matching")+" MGT", "matching_mgt", 1, true, 0, 1, 0),
      matchingVCT(tr("Matching")+" VCT", "matching_vct", 1, true, 0, 1, 0),
      matchingNIT(tr("Matching")+" NIT", "matching_nit", 1, true, 0, 1, 0),
      matchingSDT(tr("Matching")+" SDT", "matching_sdt", 1, true, 0, 1, 0),
      matchingCrypt(tr("Matching")+" Crypt", "matching_crypt",
                    1, true, 0, 1, 0),
      majorChannel(-1), minorChannel(-1),
      networkID(0), transportID(0),
      detectedNetworkID(0), detectedTransportID(0),
      programNumber(-1),
      last_pat_crc(-1),
      error(""),
      ignore_encrypted(false)
{
}

DTVSignalMonitor::~DTVSignalMonitor()
{
    SetStreamData(NULL);
}

void DTVSignalMonitor::deleteLater(void)
{
    SetStreamData(NULL);
    SignalMonitor::deleteLater();
}

DTVChannel *DTVSignalMonitor::GetDTVChannel(void)
{
    return dynamic_cast<DTVChannel*>(channel);
}

QStringList DTVSignalMonitor::GetStatusList(bool kick)
{
    QStringList list = SignalMonitor::GetStatusList(kick);
    QMutexLocker locker(&statusLock);
    // mpeg tables
    if (flags & kDTVSigMon_WaitForPAT)
    {
        list<<seenPAT.GetName()<<seenPAT.GetStatus();
        list<<matchingPAT.GetName()<<matchingPAT.GetStatus();
    }
    if (flags & kDTVSigMon_WaitForPMT)
    {
#define DEBUG_PMT 1
#if DEBUG_PMT
        static int seenGood = -1;
        static int matchingGood = -1;
#endif
        list<<seenPMT.GetName()<<seenPMT.GetStatus();
        list<<matchingPMT.GetName()<<matchingPMT.GetStatus();
#if DEBUG_PMT
        if ((seenGood != (int)seenPMT.IsGood()) ||
            (matchingGood != (int)matchingPMT.IsGood()))
        {
            DBG_SM("GetStatusList", "WaitForPMT seen("<<seenPMT.IsGood()
                   <<") matching("<<matchingPMT.IsGood()<<")");
            seenGood = (int)seenPMT.IsGood();
            matchingGood = (int)matchingPMT.IsGood();
        }
#endif
    }
    // atsc tables
    if (flags & kDTVSigMon_WaitForMGT)
    {
        list<<seenMGT.GetName()<<seenMGT.GetStatus();
        list<<matchingMGT.GetName()<<matchingMGT.GetStatus();
    }
    if (flags & kDTVSigMon_WaitForVCT)
    {
        list<<seenVCT.GetName()<<seenVCT.GetStatus();
        list<<matchingVCT.GetName()<<matchingVCT.GetStatus();
    }
    // dvb tables
    if (flags & kDTVSigMon_WaitForNIT)
    {
        list<<seenNIT.GetName()<<seenNIT.GetStatus();
        list<<matchingNIT.GetName()<<matchingNIT.GetStatus();
    }
    if (flags & kDTVSigMon_WaitForSDT)
    {
        list<<seenSDT.GetName()<<seenSDT.GetStatus();
        list<<matchingSDT.GetName()<<matchingSDT.GetStatus();
    }
    if (flags & kDTVSigMon_WaitForCrypt)
    {
        list<<seenCrypt.GetName()<<seenCrypt.GetStatus();
        list<<matchingCrypt.GetName()<<matchingCrypt.GetStatus();
    }
    if (error != "")
    {
        list<<"error"<<error;
    }
    return list;
}

void DTVSignalMonitor::AddFlags(uint64_t _flags)
{
    SignalMonitor::AddFlags(_flags);
    UpdateMonitorValues();
}

void DTVSignalMonitor::RemoveFlags(uint64_t _flags)
{
    SignalMonitor::RemoveFlags(_flags);
    UpdateMonitorValues();
}

void DTVSignalMonitor::UpdateMonitorValues(void)
{
    QMutexLocker locker(&statusLock);
    seenPAT.SetValue(    (flags & kDTVSigMon_PATSeen)  ? 1 : 0);
    seenPMT.SetValue(    (flags & kDTVSigMon_PMTSeen)  ? 1 : 0);
    seenMGT.SetValue(    (flags & kDTVSigMon_MGTSeen)  ? 1 : 0);
    seenVCT.SetValue(    (flags & kDTVSigMon_VCTSeen)  ? 1 : 0);
    seenNIT.SetValue(    (flags & kDTVSigMon_NITSeen)  ? 1 : 0);
    seenSDT.SetValue(    (flags & kDTVSigMon_SDTSeen)  ? 1 : 0);
    seenCrypt.SetValue(  (flags & kDTVSigMon_CryptSeen)? 1 : 0);
    matchingPAT.SetValue((flags & kDTVSigMon_PATMatch) ? 1 : 0);
    matchingPMT.SetValue((flags & kDTVSigMon_PMTMatch) ? 1 : 0);
    matchingMGT.SetValue((flags & kDTVSigMon_MGTMatch) ? 1 : 0);
    matchingVCT.SetValue((flags & kDTVSigMon_VCTMatch) ? 1 : 0);
    matchingNIT.SetValue((flags & kDTVSigMon_NITMatch) ? 1 : 0);
    matchingSDT.SetValue((flags & kDTVSigMon_SDTMatch) ? 1 : 0);
    matchingCrypt.SetValue((flags & kDTVSigMon_CryptMatch) ? 1 : 0);
}

void DTVSignalMonitor::UpdateListeningForEIT(void)
{
    vector<uint> add_eit, del_eit;

    if (GetStreamData()->HasEITPIDChanges(eit_pids) &&
        GetStreamData()->GetEITPIDChanges(eit_pids, add_eit, del_eit))
    {
        for (uint i = 0; i < del_eit.size(); i++)
        {
            uint_vec_t::iterator it;
            it = find(eit_pids.begin(), eit_pids.end(), del_eit[i]);
            if (it != eit_pids.end())
                eit_pids.erase(it);
            GetStreamData()->RemoveListeningPID(del_eit[i]);
        }

        for (uint i = 0; i < add_eit.size(); i++)
        {
            eit_pids.push_back(add_eit[i]);
            GetStreamData()->AddListeningPID(add_eit[i]);
        }
    }
}

void DTVSignalMonitor::SetChannel(int major, int minor)
{
    DBG_SM(QString("SetChannel(%1, %2)").arg(major).arg(minor), "");
    last_pat_crc = -1;
    if (GetATSCStreamData() && (majorChannel != major || minorChannel != minor))
    {
        RemoveFlags(kDTVSigMon_PATSeen   | kDTVSigMon_PATMatch |
                    kDTVSigMon_PMTSeen   | kDTVSigMon_PMTMatch |
                    kDTVSigMon_VCTSeen   | kDTVSigMon_VCTMatch |
                    kDTVSigMon_CryptSeen | kDTVSigMon_CryptMatch);
        majorChannel = major;
        minorChannel = minor;
        GetATSCStreamData()->SetDesiredChannel(major, minor);
        AddFlags(kDTVSigMon_WaitForVCT | kDTVSigMon_WaitForPAT);
    }
}

void DTVSignalMonitor::SetProgramNumber(int progNum)
{
    DBG_SM(QString("SetProgramNumber(%1)").arg(progNum), "");
    last_pat_crc = -1;
    if (programNumber != progNum)
    {
        RemoveFlags(kDTVSigMon_PMTSeen   | kDTVSigMon_PMTMatch |
                    kDTVSigMon_CryptSeen | kDTVSigMon_CryptMatch);
        programNumber = progNum;
        if (GetStreamData())
            GetStreamData()->SetDesiredProgram(programNumber);
        AddFlags(kDTVSigMon_WaitForPMT);
    }
}

void DTVSignalMonitor::SetDVBService(uint netid, uint tsid, int serviceid)
{
    DBG_SM(QString("SetDVBService(transport_id: %1, network_id: %2, "
                   "service_id: %3)").arg(tsid).arg(netid).arg(serviceid), "");
    last_pat_crc = -1;

    if (netid == networkID && tsid == transportID &&
        serviceid == programNumber)
    {
        return;
    }

    RemoveFlags(kDTVSigMon_PMTSeen   | kDTVSigMon_PMTMatch |
                kDTVSigMon_SDTSeen   | kDTVSigMon_SDTMatch |
                kDTVSigMon_CryptSeen | kDTVSigMon_CryptMatch);

    transportID   = tsid;
    networkID     = netid;
    programNumber = serviceid;

    if (GetDVBStreamData())
    {
        GetDVBStreamData()->SetDesiredService(netid, tsid, programNumber);
        AddFlags(kDTVSigMon_WaitForPMT | kDTVSigMon_WaitForSDT);
        GetDVBStreamData()->AddListeningPID(DVB_SDT_PID);
    }
}

void DTVSignalMonitor::SetStreamData(MPEGStreamData *data)
{
    if (stream_data)
        stream_data->RemoveMPEGListener(this);

    ATSCStreamData *atsc = GetATSCStreamData();
    DVBStreamData  *dvb  = GetDVBStreamData();
    if (atsc)
    {
        atsc->RemoveATSCMainListener(this);
        atsc->RemoveATSCAuxListener(this);
    }
    if (dvb)
        dvb->RemoveDVBMainListener(this);

    stream_data = data;
    if (!data)
        return;

    data->AddMPEGListener(this);

    atsc = GetATSCStreamData();
    dvb  = GetDVBStreamData();
    if (atsc)
    {
        atsc->AddATSCMainListener(this);
        atsc->AddATSCAuxListener(this);
    }
    if (dvb)
        dvb->AddDVBMainListener(this);
}


void DTVSignalMonitor::HandlePAT(const ProgramAssociationTable *pat)
{
    AddFlags(kDTVSigMon_PATSeen);
    int pmt_pid = pat->FindPID(programNumber);
    if (GetStreamData() && pmt_pid)
    {
        AddFlags(kDTVSigMon_PATMatch);
        GetStreamData()->AddListeningPID(pmt_pid);
        last_pat_crc = pat->CRC();
        return;
    }

    if (programNumber >= 0)
    {
        // BEGIN HACK HACK HACK
        // Reset version in case we're physically on the wrong transport
        // due to tuning hardware being in a transitional state or we
        // are in the middle of something like a DiSEqC rotor turn.
        uint tsid = pat->TransportStreamID();
        GetStreamData()->SetVersionPAT(tsid, -1,0);
        // END HACK HACK HACK

        if (last_pat_crc != pat->CRC())
        {
            last_pat_crc = pat->CRC();
            QString errStr = QString("Program #%1 not found in PAT!")
                .arg(programNumber);
            VERBOSE(VB_IMPORTANT, errStr<<endl<<pat->toString());
        }
        if (pat->ProgramCount() == 1)
        {
            VERBOSE(VB_IMPORTANT, "But there is only one program "
                    "in the PAT, so we'll just use it");
            SetProgramNumber(pat->ProgramNumber(0));
            AddFlags(kDTVSigMon_PATMatch);
            GetStreamData()->AddListeningPID(pat->ProgramPID(0));
            last_pat_crc = pat->CRC();
        }
    }
}

void DTVSignalMonitor::HandlePMT(uint, const ProgramMapTable *pmt)
{
    AddFlags(kDTVSigMon_PMTSeen);

    if (programNumber < 0)
        return; // don't print error messages during channel scan.

    if (pmt->ProgramNumber() != (uint)programNumber)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Wrong PMT; pmt->pn(%1) desired(%2)")
                .arg(pmt->ProgramNumber()).arg(programNumber));
        return; // Not the PMT we are looking for...
    }

    if (pmt->IsEncrypted())
        GetStreamData()->TestDecryption(pmt);

    // if PMT contains audio and/or video stream set as matching.
    uint hasAudio = 0;
    uint hasVideo = 0;

    for (uint i = 0; i < pmt->StreamCount(); i++)
    {
        hasVideo += pmt->IsVideo(i, GetDTVChannel()->GetSIStandard());
        hasAudio += pmt->IsAudio(i, GetDTVChannel()->GetSIStandard());
    }

    if ((hasVideo >= GetStreamData()->GetVideoStreamsRequired()) &&
        (hasAudio >= GetStreamData()->GetAudioStreamsRequired()))
    {
        if (pmt->IsEncrypted() && !ignore_encrypted)
            AddFlags(kDTVSigMon_WaitForCrypt);

        AddFlags(kDTVSigMon_PMTMatch);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("We want %1 audio and %2 video streams")
                .arg(GetStreamData()->GetAudioStreamsRequired())
                .arg(GetStreamData()->GetVideoStreamsRequired()) +
                QString("\n\t\t\tBut have %1 audio and %2 video streams")
                .arg(hasAudio).arg(hasVideo));
    }
}

void DTVSignalMonitor::HandleSTT(const SystemTimeTable*)
{
    VERBOSE(VB_CHANNEL, LOC + "Time Offset: "<<GetStreamData()->TimeOffset());
}

void DTVSignalMonitor::HandleMGT(const MasterGuideTable* mgt)
{
    AddFlags(kDTVSigMon_MGTSeen);

    if (!GetATSCStreamData())
        return;

    for (uint i=0; i<mgt->TableCount(); i++)
    {
        if ((TableClass::TVCTc == mgt->TableClass(i)) ||
            (TableClass::CVCTc == mgt->TableClass(i)))
        {
            GetATSCStreamData()->AddListeningPID(mgt->TablePID(i));
            AddFlags(kDTVSigMon_MGTMatch);
        }
    }
}

void DTVSignalMonitor::HandleTVCT(
    uint, const TerrestrialVirtualChannelTable* tvct)
{ 
    AddFlags(kDTVSigMon_VCTSeen | kDTVSigMon_TVCTSeen);
    int idx = tvct->Find(majorChannel, minorChannel);

    if (minorChannel < 0)
        return; // don't print error message during channel scan.

    if (idx < 0)
    {
        VERBOSE(VB_IMPORTANT, "Could not find channel "
                <<majorChannel<<"_"<<minorChannel<<" in TVCT");
        VERBOSE(VB_IMPORTANT, endl<<tvct->toString());
        GetATSCStreamData()->SetVersionTVCT(tvct->TransportStreamID(),-1);
        return;
    }

    DBG_SM("SetVCT()", QString("tvct->ProgramNumber(idx %1): prog num %2")
           .arg(idx).arg(tvct->ProgramNumber(idx)));

    SetProgramNumber(tvct->ProgramNumber(idx));
    AddFlags(kDTVSigMon_VCTMatch | kDTVSigMon_TVCTMatch);
}

void DTVSignalMonitor::HandleCVCT(uint, const CableVirtualChannelTable* cvct)
{
    AddFlags(kDTVSigMon_VCTSeen | kDTVSigMon_CVCTSeen);
    int idx = cvct->Find(majorChannel, minorChannel);

    if (idx < 0)
    {
        VERBOSE(VB_IMPORTANT, "Could not find channel "
                <<majorChannel<<"_"<<minorChannel<<" in CVCT");
        VERBOSE(VB_IMPORTANT, endl<<cvct->toString());
        GetATSCStreamData()->SetVersionCVCT(cvct->TransportStreamID(),-1);
        return;
    }

    DBG_SM("SetVCT()", QString("cvct->ProgramNumber(idx %1): prog num %2")
           .arg(idx).arg(cvct->ProgramNumber(idx)));

    SetProgramNumber(cvct->ProgramNumber(idx));
    AddFlags(kDTVSigMon_VCTMatch | kDTVSigMon_CVCTMatch);
}

void DTVSignalMonitor::HandleTDT(const TimeDateTable*)
{
    VERBOSE(VB_CHANNEL, LOC + "Time Offset: "<<GetStreamData()->TimeOffset());
}

void DTVSignalMonitor::HandleNIT(const NetworkInformationTable *nit)
{
    DBG_SM("SetNIT()", QString("net_id = %1").arg(nit->NetworkID()));
    AddFlags(kDTVSigMon_NITSeen);
    if (!GetDVBStreamData())
        return;
}

void DTVSignalMonitor::HandleSDT(uint, const ServiceDescriptionTable *sdt)
{
    AddFlags(kDTVSigMon_SDTSeen);

    detectedNetworkID = sdt->OriginalNetworkID();
    detectedTransportID = sdt->TSID();

    if (sdt->OriginalNetworkID() != networkID || sdt->TSID() != transportID)
    {
        GetDVBStreamData()->SetVersionSDT(sdt->TSID(), -1, 0);
    }
    else
    {
        DBG_SM("SetSDT()", QString("tsid = %1 orig_net_id = %2")
               .arg(sdt->TSID()).arg(sdt->OriginalNetworkID()));
        AddFlags(kDTVSigMon_SDTMatch);
        RemoveFlags(kDVBSigMon_WaitForPos);
    }
}

void DTVSignalMonitor::HandleEncryptionStatus(uint, bool enc_status)
{
    AddFlags(kDTVSigMon_CryptSeen);
    if (!enc_status)
        AddFlags(kDTVSigMon_CryptMatch);
}

ATSCStreamData *DTVSignalMonitor::GetATSCStreamData()
{
    return dynamic_cast<ATSCStreamData*>(stream_data);
}

DVBStreamData *DTVSignalMonitor::GetDVBStreamData()
{
    return dynamic_cast<DVBStreamData*>(stream_data);
}

ScanStreamData *DTVSignalMonitor::GetScanStreamData()
{
    return dynamic_cast<ScanStreamData*>(stream_data);
}

const ATSCStreamData *DTVSignalMonitor::GetATSCStreamData() const
{
    return dynamic_cast<const ATSCStreamData*>(stream_data);
}

const DVBStreamData *DTVSignalMonitor::GetDVBStreamData() const
{
    return dynamic_cast<const DVBStreamData*>(stream_data);
}

const ScanStreamData *DTVSignalMonitor::GetScanStreamData() const
{
    return dynamic_cast<const ScanStreamData*>(stream_data);
}

bool DTVSignalMonitor::IsAllGood(void) const
{
    QMutexLocker locker(&statusLock);
    if (!SignalMonitor::IsAllGood())
        return false;
    if ((flags & kDTVSigMon_WaitForPAT) && !matchingPAT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForPMT) && !matchingPMT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForMGT) && !matchingMGT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForVCT) && !matchingVCT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForNIT) && !matchingNIT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForSDT) && !matchingSDT.IsGood())
            return false;
    if ((flags & kDTVSigMon_WaitForCrypt) && !matchingCrypt.IsGood())
            return false;

    return true;
}

/** \fn  SignalMonitor::WaitForLock(int)
 *  \brief Wait for a StatusSignaLock(int) of true.
 *
 *   This can be called only after the signal
 *   monitoring thread has been started.
 *
 *  \param timeout maximum time to wait in milliseconds.
 *  \return true if signal was acquired.
 */
bool DTVSignalMonitor::WaitForLock(int timeout)
{
    statusLock.lock();
    if (-1 == timeout)
        timeout = signalLock.GetTimeout();
    statusLock.unlock();
    if (timeout < 0)
        return false;

    MythTimer t;
    t.start();
    while (t.elapsed()<timeout && running)
    {
        SignalMonitorList slist = 
            SignalMonitorValue::Parse(GetStatusList());
        if (SignalMonitorValue::AllGood(slist))
            return true;
        usleep(250);
    }

    return false;
}
