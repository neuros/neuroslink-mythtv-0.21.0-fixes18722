/**
 *  FirewireRecorder
 *  Copyright (c) 2005 by Jim Westfall and Dave Abrahams
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// MythTV includes
#include "firewirerecorder.h"
#include "firewirechannel.h"
#include "mythcontext.h"
#include "mpegtables.h"
#include "mpegstreamdata.h"
#include "tv_rec.h"

#define LOC QString("FireRecBase(%1): ").arg(channel->GetDevice())
#define LOC_ERR QString("FireRecBase(%1), Error: ").arg(channel->GetDevice())

FirewireRecorder::FirewireRecorder(TVRec *rec, FirewireChannel *chan) :
    DTVRecorder(rec), _mpeg_stream_data(NULL),
    channel(chan), isopen(false)
{
}

FirewireRecorder::~FirewireRecorder()
{
    SetStreamData(NULL);
    Close();
}

bool FirewireRecorder::Open(void)
{
    if (!isopen)
        isopen = channel->GetFirewireDevice()->OpenPort();

    return isopen;
}

void FirewireRecorder::Close(void)
{
    if (isopen)
    {
        channel->GetFirewireDevice()->ClosePort();
        isopen = false;
    }
}

void FirewireRecorder::StartStreaming(void)
{
    channel->GetFirewireDevice()->AddListener(this);
}

void FirewireRecorder::StopStreaming(void)
{
    channel->GetFirewireDevice()->RemoveListener(this);
}

void FirewireRecorder::StartRecording(void)
{
    VERBOSE(VB_RECORD, LOC + "StartRecording");

    if (!Open())
    {
        _error = true;
        return;
    }

    _request_recording = true;
    _recording = true;

    StartStreaming();

    while (_request_recording)
    {
        if (!PauseAndWait())
            usleep(50 * 1000);
    }

    StopStreaming();
    FinishRecording();

    _recording = false;
}

void FirewireRecorder::AddData(const unsigned char *data, uint len)
{
    uint bufsz = buffer.size();
    if ((SYNC_BYTE == data[0]) && (TSPacket::SIZE == len) &&
        (TSPacket::SIZE > bufsz))
    {
        if (bufsz)
            buffer.clear();

        ProcessTSPacket(*(reinterpret_cast<const TSPacket*>(data)));
        return;
    }

    buffer.insert(buffer.end(), data, data + len);
    bufsz += len;

    int sync_at = -1;
    for (uint i = 0; (i < bufsz) && (sync_at < 0); i++)
    {
        if (buffer[i] == SYNC_BYTE)
            sync_at = i;
    }

    if (sync_at < 0)
        return;

    if (bufsz < 30 * TSPacket::SIZE)
        return; // build up a little buffer

    while (sync_at + TSPacket::SIZE < bufsz)
    {
        ProcessTSPacket(*(reinterpret_cast<const TSPacket*>(
                              &buffer[0] + sync_at)));

        sync_at += TSPacket::SIZE;
    }

    buffer.erase(buffer.begin(), buffer.begin() + sync_at);

    return;
}

void FirewireRecorder::ProcessTSPacket(const TSPacket &tspacket)
{
    if (tspacket.TransportError())
        return;

    if (tspacket.ScramplingControl())
        return;

    if (tspacket.HasAdaptationField())
        GetStreamData()->HandleAdaptationFieldControl(&tspacket);

    if (tspacket.HasPayload())
    {
        const unsigned int lpid = tspacket.PID();

        // Pass or reject packets based on PID, and parse info from them
        if (lpid == GetStreamData()->VideoPIDSingleProgram())
        {
            _buffer_packets = !FindMPEG2Keyframes(&tspacket);
            BufferedWrite(tspacket);
        }
        else if (GetStreamData()->IsAudioPID(lpid))
        {
            _buffer_packets = !FindAudioKeyframes(&tspacket);
            BufferedWrite(tspacket);
        }
        else if (GetStreamData()->IsListeningPID(lpid))
            GetStreamData()->HandleTSTables(&tspacket);
        else if (GetStreamData()->IsWritingPID(lpid))
            BufferedWrite(tspacket);
    }
}

void FirewireRecorder::SetOptionsFromProfile(RecordingProfile *profile,
                                                 const QString &videodev,
                                                 const QString &audiodev,
                                                 const QString &vbidev)
{
    (void)videodev;
    (void)audiodev;
    (void)vbidev;
    (void)profile;
}

// documented in recorderbase.cpp
bool FirewireRecorder::PauseAndWait(int timeout)
{
    if (request_pause)
    {
        VERBOSE(VB_RECORD, LOC + "PauseAndWait("<<timeout<<") -- pause");
        if (!paused)
        {
            StopStreaming();
            paused = true;
            pauseWait.wakeAll();
            if (tvrec)
                tvrec->RecorderPaused();
        }
        unpauseWait.wait(timeout);
    }
    if (!request_pause && paused)
    {
        VERBOSE(VB_RECORD, LOC + "PauseAndWait("<<timeout<<") -- unpause");
        StartStreaming();
        paused = false;
    }
    return paused;
}

void FirewireRecorder::SetStreamData(MPEGStreamData *data)
{
    if (data == _mpeg_stream_data)
        return;

    MPEGStreamData *old_data = _mpeg_stream_data;
    _mpeg_stream_data = data;
    if (old_data)
        delete old_data;

    if (data)
    {
        data->AddMPEGSPListener(this);

        if (data->DesiredProgram() >= 0)
            data->SetDesiredProgram(data->DesiredProgram());
    }
}

void FirewireRecorder::HandleSingleProgramPAT(ProgramAssociationTable *pat)
{
    if (!pat)
        return;

    int next = (pat->tsheader()->ContinuityCounter()+1)&0xf;
    pat->tsheader()->SetContinuityCounter(next);
    BufferedWrite(*(reinterpret_cast<const TSPacket*>(pat->tsheader())));
}

void FirewireRecorder::HandleSingleProgramPMT(ProgramMapTable *pmt)
{
    if (!pmt)
        return;

    int next = (pmt->tsheader()->ContinuityCounter()+1)&0xf;
    pmt->tsheader()->SetContinuityCounter(next);
    BufferedWrite(*(reinterpret_cast<const TSPacket*>(pmt->tsheader())));
}
