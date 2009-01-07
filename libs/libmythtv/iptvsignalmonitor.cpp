// -*- Mode: c++ -*-

#include <unistd.h>

// MythTV headers
#include "mpegstreamdata.h"
#include "iptvchannel.h"
#include "iptvfeederwrapper.h"
#include "iptvsignalmonitor.h"

#undef DBG_SM
#define DBG_SM(FUNC, MSG) VERBOSE(VB_CHANNEL, \
    "IPTVSM("<<channel->GetDevice()<<")::"<<FUNC<<": "<<MSG);

#define LOC QString("IPTVSM(%1): ").arg(channel->GetDevice())
#define LOC_ERR QString("IPTVSM(%1), Error: ").arg(channel->GetDevice())

/** \fn IPTVSignalMonitor::IPTVSignalMonitor(int,IPTVChannel*,uint,const char*)
 *  \brief Initializes signal lock and signal values.
 *
 *   Start() must be called to actually begin continuous
 *   signal monitoring. The timeout is set to 3 seconds,
 *   and the signal threshold is initialized to 0%.
 *
 *  \param db_cardnum Recorder number to monitor,
 *                    if this is less than 0, SIGNAL events will not be
 *                    sent to the frontend even if SetNotifyFrontend(true)
 *                    is called.
 *  \param _channel IPTVChannel for card
 *  \param _flags   Flags to start with
 *  \param _name    Instance name for Qt signal/slot debugging
 */
IPTVSignalMonitor::IPTVSignalMonitor(
    int db_cardnum, IPTVChannel *_channel,
    uint64_t _flags, const char *_name)
    : DTVSignalMonitor(db_cardnum, _channel, _flags, _name),
      dtvMonitorRunning(false)
{
    bool isLocked = false;
    IPTVChannelInfo chaninfo = GetChannel()->GetCurrentChanInfo();
    if (chaninfo.isValid())
    {
        isLocked = GetChannel()->GetFeeder()->Open(chaninfo.m_url);
    }

    QMutexLocker locker(&statusLock);
    signalLock.SetValue((isLocked) ? 1 : 0);
    signalStrength.SetValue((isLocked) ? 100 : 0);
}

/** \fn IPTVSignalMonitor::~IPTVSignalMonitor()
 *  \brief Stops signal monitoring and table monitoring threads.
 */
IPTVSignalMonitor::~IPTVSignalMonitor()
{
    GetChannel()->GetFeeder()->RemoveListener(this);
    Stop();
}

IPTVChannel *IPTVSignalMonitor::GetChannel(void)
{
    return dynamic_cast<IPTVChannel*>(channel);
}

void IPTVSignalMonitor::deleteLater(void)
{
    disconnect(); // disconnect signals we may be sending...
    GetChannel()->GetFeeder()->RemoveListener(this);
    Stop();
    DTVSignalMonitor::deleteLater();
}

/** \fn IPTVSignalMonitor::Stop(void)
 *  \brief Stop signal monitoring and table monitoring threads.
 */
void IPTVSignalMonitor::Stop(void)
{
    DBG_SM("Stop", "begin");
    GetChannel()->GetFeeder()->RemoveListener(this);
    SignalMonitor::Stop();
    if (dtvMonitorRunning)
    {
        GetChannel()->GetFeeder()->Stop();
        dtvMonitorRunning = false;
        pthread_join(table_monitor_thread, NULL);
    }
    DBG_SM("Stop", "end");
}

void *IPTVSignalMonitor::TableMonitorThread(void *param)
{
    IPTVSignalMonitor *mon = (IPTVSignalMonitor*) param;
    mon->RunTableMonitor();
    return NULL;
}

/** \fn IPTVSignalMonitor::RunTableMonitor(void)
 */
void IPTVSignalMonitor::RunTableMonitor(void)
{
    DBG_SM("Run", "begin");
    dtvMonitorRunning = true;

    GetStreamData()->AddListeningPID(0);

    GetChannel()->GetFeeder()->AddListener(this);
    GetChannel()->GetFeeder()->Run();
    GetChannel()->GetFeeder()->RemoveListener(this);

    dtvMonitorRunning = false;
    DBG_SM("Run", "end");
}

void IPTVSignalMonitor::AddData(
    const unsigned char *data, unsigned int dataSize)
{
    GetStreamData()->ProcessData((unsigned char*)data, dataSize);
}

/** \fn IPTVSignalMonitor::UpdateValues(void)
 *  \brief Fills in frontend stats and emits status Qt signals.
 *
 *   This is automatically called by MonitorLoop(), after Start()
 *   has been used to start the signal monitoring thread.
 */
void IPTVSignalMonitor::UpdateValues(void)
{
    if (!running || exit)
        return;

    if (dtvMonitorRunning)
    {
        EmitIPTVSignals();
        if (IsAllGood())
            emit AllGood();
        // TODO dtv signals...

        update_done = true;
        return;
    }

    bool isLocked = false;
    {
        QMutexLocker locker(&statusLock);
        isLocked = signalLock.IsGood();
    }

    EmitIPTVSignals();
    if (IsAllGood())
        emit AllGood();

    // Start table monitoring if we are waiting on any table
    // and we have a lock.
    if (isLocked && GetStreamData() &&
        HasAnyFlag(kDTVSigMon_WaitForPAT | kDTVSigMon_WaitForPMT |
                   kDTVSigMon_WaitForMGT | kDTVSigMon_WaitForVCT |
                   kDTVSigMon_WaitForNIT | kDTVSigMon_WaitForSDT))
    {
        pthread_create(&table_monitor_thread, NULL,
                       TableMonitorThread, this);
        DBG_SM("UpdateValues", "Waiting for table monitor to start");
        while (!dtvMonitorRunning)
            usleep(50);
        DBG_SM("UpdateValues", "Table monitor started");
    }

    update_done = true;
}

#define EMIT(SIGNAL_FUNC, SIGNAL_VAL) \
    do { statusLock.lock(); \
         SignalMonitorValue val = SIGNAL_VAL; \
         statusLock.unlock(); \
         emit SIGNAL_FUNC(val); } while (false)

/** \fn IPTVSignalMonitor::EmitIPTVSignals(void)
 *  \brief Emits signals for lock, signal strength, etc.
 */
void IPTVSignalMonitor::EmitIPTVSignals(void)
{
    // Emit signals..
    EMIT(StatusSignalLock, signalLock); 
    if (HasFlags(kDTVSigMon_WaitForSig))
        EMIT(StatusSignalStrength, signalStrength);
}

#undef EMIT
