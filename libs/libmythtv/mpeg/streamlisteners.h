// -*- Mode: c++ -*-
#ifndef _STREAMLISTENERS_H_
#define _STREAMLISTENERS_H_

#include "tspacket.h"
#include "util.h"

class TSPacket;
class TSPacket_nonconst;
class PESPacket;
class PSIPTable;

class MPEGStreamData;
class ATSCStreamData;
class DVBStreamData;
class ScanStreamData;

class ProgramAssociationTable;
class ConditionalAccessTable;
class ProgramMapTable;

class SystemTimeTable;
class MasterGuideTable;
class VirtualChannelTable;
class TerrestrialVirtualChannelTable;
class CableVirtualChannelTable;
class EventInformationTable;
class ExtendedTextTable;
class RatingRegionTable;
class DirectedChannelChangeTable;
class DirectedChannelChangeSelectionCodeTable;

class NetworkInformationTable;
class ServiceDescriptionTable;
class TimeDateTable;
class DVBEventInformationTable;
class PremiereContentInformationTable;

class TSDataListener
{
  public:
    /// Callback function to add MPEG2 TS data
    virtual void AddData(const unsigned char *data, uint dataSize) = 0;

  protected:
    virtual ~TSDataListener() { }
};

class TSPacketListener
{
  public:
    virtual bool ProcessTSPacket(const TSPacket& tspacket) = 0;

  protected:
    virtual ~TSPacketListener() { }
};

class TSPacketListenerAV
{
  public:
    virtual bool ProcessVideoTSPacket(const TSPacket& tspacket) = 0;
    virtual bool ProcessAudioTSPacket(const TSPacket& tspacket) = 0;

  protected:
    virtual ~TSPacketListenerAV() { }
};

class MPEGStreamListener
{
  protected:
    virtual ~MPEGStreamListener() {}
  public:
    virtual void HandlePAT(const ProgramAssociationTable*) = 0;
    virtual void HandleCAT(const ConditionalAccessTable*) = 0;
    virtual void HandlePMT(uint program_num, const ProgramMapTable*) = 0;
    virtual void HandleEncryptionStatus(uint program_number, bool) = 0;
};

class MPEGSingleProgramStreamListener
{
  protected:
    virtual ~MPEGSingleProgramStreamListener() {}
  public:
    virtual void HandleSingleProgramPAT(ProgramAssociationTable*) = 0;
    virtual void HandleSingleProgramPMT(ProgramMapTable*) = 0;
};

class ATSCMainStreamListener
{
  protected:
    virtual ~ATSCMainStreamListener() {}
  public:
    virtual void HandleSTT(const SystemTimeTable*) = 0;
    virtual void HandleMGT(const MasterGuideTable*) = 0;
    virtual void HandleVCT(uint pid, const VirtualChannelTable*) = 0;
};

class ATSCAuxStreamListener
{
  protected:
    virtual ~ATSCAuxStreamListener() {}
  public:
    virtual void HandleTVCT(uint pid,const TerrestrialVirtualChannelTable*)=0;
    virtual void HandleCVCT(uint pid, const CableVirtualChannelTable*) = 0;
    virtual void HandleRRT(const RatingRegionTable*) = 0;
    virtual void HandleDCCT(const DirectedChannelChangeTable*) = 0;
    virtual void HandleDCCSCT(
        const DirectedChannelChangeSelectionCodeTable*) = 0;
};

class ATSCEITStreamListener
{
  protected:
    virtual ~ATSCEITStreamListener() {}
  public:
    virtual void HandleEIT( uint pid, const EventInformationTable*) = 0;
    virtual void HandleETT( uint pid, const ExtendedTextTable*) = 0;
};

class DVBMainStreamListener
{
  protected:
    virtual ~DVBMainStreamListener() {}
  public:
    virtual void HandleTDT(const TimeDateTable*) = 0;
    virtual void HandleNIT(const NetworkInformationTable*) = 0;
    virtual void HandleSDT(uint tsid, const ServiceDescriptionTable*) = 0;
};

class DVBOtherStreamListener
{
  protected:
    virtual ~DVBOtherStreamListener() {}
  public:
    virtual void HandleNITo(const NetworkInformationTable*) = 0;
    virtual void HandleSDTo(uint tsid, const ServiceDescriptionTable*) = 0;
};

class DVBEITStreamListener
{
  protected:
    virtual ~DVBEITStreamListener() {}
  public:
    virtual void HandleEIT(const DVBEventInformationTable*) = 0;
    virtual void HandleEIT(const PremiereContentInformationTable*) = 0;
};


#endif // _STREAMLISTENERS_H_
