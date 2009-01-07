#ifndef DUMMYDECODER_H_
#define DUMMYDECODER_H_

#include <qmap.h>
#include <qptrlist.h>

#include "programinfo.h"
#include "format.h"
#include "decoderbase.h"

extern "C" {
#include "frame.h"
}

class DummyDecoder : public DecoderBase
{
  public:
    DummyDecoder(NuppelVideoPlayer *parent, ProgramInfo *pginfo) : 
        DecoderBase(parent, pginfo) {}
    virtual ~DummyDecoder() {}

    virtual int OpenFile(RingBuffer *, bool, char *, int) { return 0; }
    virtual bool GetFrame(int) { usleep(10000); return false; }

    virtual bool isLastFrameKey(void) { return true; }
    virtual void WriteStoredData(RingBuffer *, bool, long) {}

    virtual long UpdateStoredFrameNum(long) { return 0; }

    virtual QString GetCodecDecoderName(void) const { return "dummy"; }
    virtual MythCodecID GetVideoCodecID(void) const { return kCodec_NONE; }

    virtual bool SyncPositionMap(void) { return false; }
};

#endif
