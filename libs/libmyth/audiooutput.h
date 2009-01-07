#ifndef AUDIOOUTPUT
#define AUDIOOUTPUT

#include <iostream>
using namespace std;

#include "mythcontext.h"
#include "volumebase.h"
#include "output.h"

typedef enum {
    AUDIOOUTPUT_VIDEO,
    AUDIOOUTPUT_MUSIC,
    AUDIOOUTPUT_TELEPHONY
} AudioOutputSource;

class MPUBLIC AudioOutput : public VolumeBase, public OutputListeners
{
 public:
    // opens one of the concrete subclasses
    static AudioOutput *OpenAudio(QString audiodevice, QString passthrudevice,
                                  int audio_bits,
                                  int audio_channels, int audio_samplerate,
                                  AudioOutputSource source,
                                  bool set_initial_vol, bool audio_passthru);

    AudioOutput() :
        VolumeBase(),             OutputListeners(),
        lastError(QString::null), lastWarn(QString::null) {}

    virtual ~AudioOutput() { };

    // reconfigure sound out for new params
    virtual void Reconfigure(int audio_bits, 
                             int audio_channels, 
                             int audio_samplerate,
                             bool audio_passthru,
                             void* audio_codec = NULL) = 0;
    
    virtual void SetStretchFactor(float factor);
    virtual float GetStretchFactor(void) { return 1.0f; }

    // do AddSamples calls block?
    virtual void SetBlocking(bool blocking) = 0;
    
    // dsprate is in 100 * samples/second
    virtual void SetEffDsp(int dsprate) = 0;

    virtual void Reset(void) = 0;

    // timecode is in milliseconds.
    // Return true if all samples were written, false if none.
    virtual bool AddSamples(char *buffer, int samples, long long timecode) = 0;
    virtual bool AddSamples(char *buffers[], int samples, long long timecode) = 0;

    virtual void SetTimecode(long long timecode) = 0;
    virtual bool GetPause(void) = 0;
    virtual void Pause(bool paused) = 0;
 
    // Wait for all data to finish playing
    virtual void Drain(void) = 0;

    virtual int GetAudiotime(void) = 0;

    virtual void SetSourceBitrate(int ) { }

    QString GetError(void)   const { return lastError; }
    QString GetWarning(void) const { return lastWarn; }

    virtual void GetBufferStatus(uint &fill, uint &total) { fill = total = 0; }

    //  Only really used by the AudioOutputNULL object
    
    virtual void bufferOutputData(bool y) = 0;
    virtual int readOutputData(unsigned char *read_buffer, int max_length) = 0;

  protected:
    void Error(QString msg)
    {
        lastError = msg;
        VERBOSE(VB_IMPORTANT, "AudioOutput Error: " + lastError);
    }
    void ClearError(void) { lastError = QString::null; }

    void Warn(QString msg)
    {
        lastWarn  = msg;
        VERBOSE(VB_IMPORTANT, "AudioOutput Warning: " + lastWarn);
    }

  protected:
    QString lastError;
    QString lastWarn;
};

#endif

