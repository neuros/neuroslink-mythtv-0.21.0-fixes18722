#ifndef DVBCAM_H
#define DVBCAM_H

#include <deque>
using namespace std;

#include <qmutex.h>

#include "mpegtables.h"

#include "dvbtypes.h"

class ChannelBase;
class cCiHandler;
typedef QMap<const ChannelBase*, ProgramMapTable*> pmt_list_t;

class DVBCam
{
  public:
    DVBCam(int cardnum);
    ~DVBCam();

    bool Start();
    bool Stop();
    bool IsRunning() const { return ciThreadRunning; }
    void SetPMT(const ChannelBase *chan, const ProgramMapTable *pmt);
    void SetTimeOffset(double offset_in_seconds);

  private:
    static void *CiHandlerThreadHelper(void*);
    void CiHandlerLoop(void);
    void HandleUserIO(void);
    void HandlePMT(void);

    void SendPMT(const ProgramMapTable &pmt, uint cplm);

    int             cardnum;
    int             numslots;
    cCiHandler     *ciHandler;

    bool            exitCiThread;
    bool            ciThreadRunning;

    pthread_t       ciHandlerThread;

    pmt_list_t      PMTList;
    pmt_list_t      PMTAddList;
    QMutex          pmt_lock;
    bool            have_pmt;
    bool            pmt_sent;
    bool            pmt_updated;
    bool            pmt_added;
};

#endif // DVBCAM_H

