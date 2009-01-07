#ifndef AUDIOOUTPUTALSA
#define AUDIOOUTPUTALSA

#include <vector>
#include <qstring.h>
#include <qmutex.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include "audiooutputbase.h"

using namespace std;

class AudioOutputALSA : public AudioOutputBase
{
  public:
    AudioOutputALSA(QString laudio_main_device, QString laudio_passthru_device,
                    int laudio_bits,
                    int laudio_channels, int laudio_samplerate,
                    AudioOutputSource source,
                    bool set_initial_vol, bool laudio_passthru);
    virtual ~AudioOutputALSA();

    // Volume control
    virtual int GetVolumeChannel(int channel); // Returns 0-100
    virtual void SetVolumeChannel(int channel, int volume); // range 0-100 for vol

    
  protected:
    // You need to implement the following functions
    virtual bool OpenDevice(void);
    virtual void CloseDevice(void);
    virtual void WriteAudio(unsigned char *aubuf, int size);
    virtual inline int getSpaceOnSoundcard(void);
    virtual inline int getBufferedOnSoundcard(void);

  private:
    inline int SetParameters(snd_pcm_t *handle,
                             snd_pcm_format_t format, unsigned int channels,
                             unsigned int rate, unsigned int buffer_time,
                             unsigned int period_time);


    // Volume related
    void SetCurrentVolume(QString control, int channel, int volume);
    void OpenMixer(bool setstartingvolume);
    void CloseMixer(void);
    void SetupMixer(void);
    void GetVolumeRange(snd_mixer_elem_t *elem);

  private:
    snd_pcm_t   *pcm_handle;
    int          numbadioctls;
    QMutex       killAudioLock;
    snd_mixer_t *mixer_handle;
    QString      mixer_control; // e.g. "PCM"
    float        volume_range_multiplier;
    long         playback_vol_min;
    long         playback_vol_max;
    snd_pcm_sframes_t (*pcm_write_func)(
        snd_pcm_t*, const void*, snd_pcm_uframes_t);
};

#endif

