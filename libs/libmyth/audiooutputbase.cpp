// Std C headers
#include <cstdio>
#include <cstdlib>
#include <cmath>

// POSIX headers
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// Qt headers
#include <qdatetime.h>
#include <qstring.h>
#include <qdeepcopy.h>

// MythTV headers
#include "compat.h"
#include "audiooutputbase.h"
#include "audiooutputdigitalencoder.h"
#include "SoundTouch.h"
#include "freesurround.h"

#define LOC QString("AO: ")
#define LOC_ERR QString("AO, ERROR: ")

AudioOutputBase::AudioOutputBase(
    QString laudio_main_device,    QString           laudio_passthru_device,
    int     /*laudio_bits*/,       int               /*laudio_channels*/,
    int     /*laudio_samplerate*/, AudioOutputSource lsource,
    bool    lset_initial_vol,      bool              /*laudio_passthru*/) :

    effdsp(0),                  effdspstretched(0),
    audio_channels(-1),         audio_bytes_per_sample(0),
    audio_bits(-1),             audio_samplerate(-1),
    audio_buffer_unused(0),
    fragment_size(0),           soundcard_buffer_size(0),

    audio_main_device(QDeepCopy<QString>(laudio_main_device)),
    audio_passthru_device(QDeepCopy<QString>(laudio_passthru_device)),
    audio_passthru(false),      audio_stretchfactor(1.0f),

    audio_codec(NULL),
    source(lsource),            killaudio(false),

    pauseaudio(false),          audio_actually_paused(false),
    was_paused(false),

    set_initial_vol(lset_initial_vol),
    buffer_output_data_for_use(false),
    need_resampler(false),

    src_ctx(NULL),

    pSoundStretch(NULL),        
    encoder(NULL),
    upmixer(NULL),
    source_audio_channels(-1),
    source_audio_bytes_per_sample(0),
    needs_upmix(false),
    surround_mode(FreeSurround::SurroundModePassive),

    blocking(false),

    lastaudiolen(0),            samples_buffered(0),

    audio_thread_exists(false),

    audiotime(0),
    raud(0),                    waud(0),
    audbuf_timecode(0),

    numlowbuffer(0),            killAudioLock(false),
    current_seconds(-1),        source_bitrate(-1)
{
    pthread_mutex_init(&audio_buflock, NULL);
    pthread_mutex_init(&avsync_lock, NULL);
    pthread_cond_init(&audio_bufsig, NULL);

    // The following are not bzero() because MS Windows doesn't like it.
    memset(&src_data,          0, sizeof(SRC_DATA));
    memset(src_in,             0, sizeof(float) * AUDIO_SRC_IN_SIZE);
    memset(src_out,            0, sizeof(float) * AUDIO_SRC_OUT_SIZE);
    memset(tmp_buff,           0, sizeof(short) * AUDIO_TMP_BUF_SIZE);
    memset(&audiotime_updated, 0, sizeof(audiotime_updated));
    memset(audiobuffer,        0, sizeof(char)  * AUDBUFSIZE);
    configured_audio_channels = gContext->GetNumSetting("MaxChannels", 2);

    // You need to call Reconfigure from your concrete class.
    // Reconfigure(laudio_bits,       laudio_channels,
    //             laudio_samplerate, laudio_passthru);
}

AudioOutputBase::~AudioOutputBase()
{
    // Make sure you call the next line in your concrete class to ensure everything is shutdown correctly.
    // Cant be called here due to use of virtual functions
    // KillAudio();
    
    pthread_mutex_destroy(&audio_buflock);
    pthread_mutex_destroy(&avsync_lock);
    pthread_cond_destroy(&audio_bufsig);
}

void AudioOutputBase::SetSourceBitrate(int rate)
{
    if (rate > 0)
        source_bitrate = rate;
}

void AudioOutputBase::SetStretchFactorLocked(float laudio_stretchfactor)
{
    effdspstretched = (int)((float)effdsp / laudio_stretchfactor);
    if (audio_stretchfactor != laudio_stretchfactor)
    {
        audio_stretchfactor = laudio_stretchfactor;
        if (pSoundStretch)
        {
            VERBOSE(VB_GENERAL, LOC + QString("Changing time stretch to %1")
                                        .arg(audio_stretchfactor));
            pSoundStretch->setTempo(audio_stretchfactor);
        }
        else if (audio_stretchfactor != 1.0)
        {
            VERBOSE(VB_GENERAL, LOC + QString("Using time stretch %1")
                                        .arg(audio_stretchfactor));
            pSoundStretch = new soundtouch::SoundTouch();
            if (audio_codec)
            {
                if (!encoder)
                {
                    VERBOSE(VB_AUDIO, LOC +
                            QString("Creating Encoder for codec %1 origfs %2")
                            .arg(audio_codec->codec_id)
                            .arg(audio_codec->frame_size));

                    encoder = new AudioOutputDigitalEncoder();
                    if (!encoder->Init(audio_codec->codec_id,
                                audio_codec->bit_rate,
                                audio_codec->sample_rate,
                                audio_codec->channels
                                ))
                    {
                        // eeks
                        delete encoder;
                        encoder = NULL;
                        VERBOSE(VB_AUDIO, LOC +
                                QString("Failed to Create Encoder"));
                    }
                }
            }
            if (audio_codec && encoder)
            {
                pSoundStretch->setSampleRate(audio_codec->sample_rate);
                pSoundStretch->setChannels(audio_codec->channels);
            }
            else
            {
                pSoundStretch->setSampleRate(audio_samplerate);
                pSoundStretch->setChannels(audio_channels);
            }

            pSoundStretch->setTempo(audio_stretchfactor);
            pSoundStretch->setSetting(SETTING_SEQUENCE_MS, 35);

            // dont need these with only tempo change
            //pSoundStretch->setPitch(1.0);
            //pSoundStretch->setRate(1.0);

            //pSoundStretch->setSetting(SETTING_USE_QUICKSEEK, true);
            //pSoundStretch->setSetting(SETTING_USE_AA_FILTER, false);
        }
    }
}

void AudioOutputBase::SetStretchFactor(float laudio_stretchfactor)
{
    pthread_mutex_lock(&audio_buflock);
    SetStretchFactorLocked(laudio_stretchfactor);
    pthread_mutex_unlock(&audio_buflock);
}

float AudioOutputBase::GetStretchFactor(void)
{
    return audio_stretchfactor;
}

void AudioOutputBase::Reconfigure(int laudio_bits, int laudio_channels, 
                                  int laudio_samplerate, bool laudio_passthru,
                                  void *laudio_codec)
{
    int codec_id = CODEC_ID_NONE;
    int lcodec_id = CODEC_ID_NONE;
    int lcchannels = 0;
    int cchannels = 0;
    int lsource_audio_channels = laudio_channels;
    bool lneeds_upmix = false;

    if (laudio_codec)
    {
        lcodec_id = ((AVCodecContext*)laudio_codec)->codec_id;
        laudio_bits = 16;
        laudio_channels = 2;
        lsource_audio_channels = laudio_channels;
        laudio_samplerate = 48000;
        lcchannels = ((AVCodecContext*)laudio_codec)->channels;
    }

    if (audio_codec)
    {
        codec_id = audio_codec->codec_id;
        cchannels = ((AVCodecContext*)audio_codec)->channels;
    }

    if ((configured_audio_channels == 6) && 
        !(laudio_codec || audio_codec))
    {
        laudio_channels = configured_audio_channels;
        lneeds_upmix = true;
        VERBOSE(VB_AUDIO,LOC + "Needs upmix");
    }

    ClearError();
    bool general_deps = (laudio_bits == audio_bits && 
        laudio_channels == audio_channels &&
        laudio_samplerate == audio_samplerate && !need_resampler &&
        laudio_passthru == audio_passthru &&
        lneeds_upmix == needs_upmix &&
        lcodec_id == codec_id && lcchannels == cchannels);
    bool upmix_deps =
        (lsource_audio_channels == source_audio_channels);
    if (general_deps && upmix_deps)
    {
        VERBOSE(VB_AUDIO,LOC + "no change exiting");
        return;
    }

    if (general_deps && !upmix_deps && lneeds_upmix && upmixer)
    {
        upmixer->flush();
        source_audio_channels = lsource_audio_channels;
        VERBOSE(VB_AUDIO,LOC + QString("source channels changed to %1")
                .arg(source_audio_channels));
        return;
    }

    KillAudio();
    
    pthread_mutex_lock(&audio_buflock);
    pthread_mutex_lock(&avsync_lock);

    lastaudiolen = 0;
    waud = raud = 0;
    audio_actually_paused = false;
    
    bool redo_stretch = (pSoundStretch && audio_channels != laudio_channels);
    audio_channels = laudio_channels;
    source_audio_channels = lsource_audio_channels;
    audio_bits = laudio_bits;
    audio_samplerate = laudio_samplerate;
    audio_codec = (AVCodecContext*)laudio_codec;
    audio_passthru = laudio_passthru;
    needs_upmix = lneeds_upmix;

    if (audio_bits != 8 && audio_bits != 16)
    {
        pthread_mutex_unlock(&avsync_lock);
        pthread_mutex_unlock(&audio_buflock);
        Error("AudioOutput only supports 8 or 16bit audio.");
        return;
    }
    audio_bytes_per_sample = audio_channels * audio_bits / 8;
    source_audio_bytes_per_sample = source_audio_channels * audio_bits / 8;
    
    need_resampler = false;
    killaudio = false;
    pauseaudio = false;
    was_paused = true;
    internal_vol = gContext->GetNumSetting("MythControlsVolume", 0);
    
    numlowbuffer = 0;

    VERBOSE(VB_GENERAL, QString("Opening audio device '%1'. ch %2(%3) sr %4")
            .arg(audio_main_device).arg(audio_channels)
            .arg(source_audio_channels).arg(audio_samplerate));
 
    // Actually do the device specific open call
    if (!OpenDevice())
    {
        VERBOSE(VB_AUDIO, LOC_ERR + "Aborting reconfigure");
        pthread_mutex_unlock(&avsync_lock);
        pthread_mutex_unlock(&audio_buflock);
        if (GetError().isEmpty())
            Error("Aborting reconfigure");
        VERBOSE(VB_AUDIO, "Aborting reconfigure");
        return;
    }

    SyncVolume();
    
    VERBOSE(VB_AUDIO, LOC + QString("Audio fragment size: %1")
            .arg(fragment_size));

    if (audio_buffer_unused < 0)
        audio_buffer_unused = 0;

    if (!gContext->GetNumSetting("AggressiveSoundcardBuffer", 0))
        audio_buffer_unused = 0;

    audbuf_timecode = 0;
    audiotime = 0;
    samples_buffered = 0;
    effdsp = audio_samplerate * 100;
    gettimeofday(&audiotime_updated, NULL);
    current_seconds = -1;
    source_bitrate = -1;

    // NOTE: this won't do anything as above samplerate vars are set equal
    // Check if we need the resampler
    if (audio_samplerate != laudio_samplerate)
    {
        int error;
        VERBOSE(VB_GENERAL, LOC + QString("Using resampler. From: %1 to %2")
                               .arg(laudio_samplerate).arg(audio_samplerate));
        src_ctx = src_new (SRC_SINC_BEST_QUALITY, audio_channels, &error);
        if (error)
        {
            Error(QString("Error creating resampler, the error was: %1")
                  .arg(src_strerror(error)) );
            pthread_mutex_unlock(&avsync_lock);
            pthread_mutex_unlock(&audio_buflock);
            return;
        }
        src_data.src_ratio = (double) audio_samplerate / laudio_samplerate;
        src_data.data_in = src_in;
        src_data.data_out = src_out;
        src_data.output_frames = 16384*6;
        need_resampler = true;
    }

    if (needs_upmix)
    {
        VERBOSE(VB_AUDIO, LOC + QString("create upmixer"));
        if (configured_audio_channels == 6)
        {
            surround_mode = gContext->GetNumSetting("AudioUpmixType", 2);
        }

        upmixer = new FreeSurround(
            audio_samplerate, 
            source == AUDIOOUTPUT_VIDEO, 
            (FreeSurround::SurroundMode)surround_mode);

        VERBOSE(VB_AUDIO, LOC +
                QString("create upmixer done with surround mode %1")
                .arg(surround_mode));
    }

    VERBOSE(VB_AUDIO, LOC + QString("Audio Stretch Factor: %1")
            .arg(audio_stretchfactor));
    VERBOSE(VB_AUDIO, QString("Audio Codec Used: %1")
            .arg((audio_codec) ?
                 codec_id_string(audio_codec->codec_id) : "not set"));

    if (redo_stretch)
    {
        float laudio_stretchfactor = audio_stretchfactor;
        delete pSoundStretch;
        pSoundStretch = NULL;
        audio_stretchfactor = 0.0f;
        SetStretchFactorLocked(laudio_stretchfactor);
    }
    else
    {
        SetStretchFactorLocked(audio_stretchfactor);
        if (pSoundStretch)
        {
            // if its passthru then we need to reencode
            if (audio_codec)
            {
                if (!encoder)
                {
                    VERBOSE(VB_AUDIO, LOC +
                            QString("Creating Encoder for codec %1")
                            .arg(audio_codec->codec_id));

                    encoder = new AudioOutputDigitalEncoder();
                    if (!encoder->Init(audio_codec->codec_id,
                                audio_codec->bit_rate,
                                audio_codec->sample_rate,
                                audio_codec->channels
                                ))
                    {
                        // eeks
                        delete encoder;
                        encoder = NULL;
                        VERBOSE(VB_AUDIO, LOC + "Failed to Create Encoder");
                    }
                }
            }
            if (audio_codec && encoder)
            {
                pSoundStretch->setSampleRate(audio_codec->sample_rate);
                pSoundStretch->setChannels(audio_codec->channels);
            }
            else
            {
                pSoundStretch->setSampleRate(audio_samplerate);
                pSoundStretch->setChannels(audio_channels);
            }
        }
    }

    // Setup visualisations, zero the visualisations buffers
    prepareVisuals();

    StartOutputThread();
    pthread_mutex_unlock(&avsync_lock);
    pthread_mutex_unlock(&audio_buflock);
    VERBOSE(VB_AUDIO, LOC + "Ending reconfigure");
}

bool AudioOutputBase::StartOutputThread(void)
{
    if (audio_thread_exists)
        return true;

    int status = pthread_create(
        &audio_thread, NULL, kickoffOutputAudioLoop, this);

    if (status)
    {
        Error("Failed to create audio thread" + ENO);
        return false;
    }

    audio_thread_exists = true;

    return true;
}


void AudioOutputBase::StopOutputThread(void)
{
    if (audio_thread_exists)
    {
        pthread_join(audio_thread, NULL);
        audio_thread_exists = false;
    }
}

void AudioOutputBase::KillAudio()
{
    killAudioLock.lock();

    VERBOSE(VB_AUDIO, LOC + "Killing AudioOutputDSP");
    killaudio = true;
    StopOutputThread();

    // Close resampler?
    if (src_ctx)
        src_delete(src_ctx);
    need_resampler = false;

    // close sound stretcher
    if (pSoundStretch)
    {
        delete pSoundStretch;
        pSoundStretch = NULL;
    }

    if (encoder)
    {
        delete encoder;
        encoder = NULL;
    }

    if (upmixer)
    {
        delete upmixer;
        upmixer = NULL;
    }
    needs_upmix = false;

    CloseDevice();

    killAudioLock.unlock();
}


bool AudioOutputBase::GetPause(void)
{
    return audio_actually_paused;
}

void AudioOutputBase::Pause(bool paused)
{
    VERBOSE(VB_AUDIO, LOC + QString("Pause %0").arg(paused));
    pauseaudio = paused;
    audio_actually_paused = false;
}

void AudioOutputBase::Reset()
{
    pthread_mutex_lock(&audio_buflock);
    pthread_mutex_lock(&avsync_lock);

    raud = waud = 0;
    audbuf_timecode = 0;
    audiotime = 0;
    samples_buffered = 0;
    current_seconds = -1;
    was_paused = !pauseaudio;

    // Setup visualisations, zero the visualisations buffers
    prepareVisuals();

    gettimeofday(&audiotime_updated, NULL);

    pthread_mutex_unlock(&avsync_lock);
    pthread_mutex_unlock(&audio_buflock);
}

void AudioOutputBase::SetTimecode(long long timecode)
{
    pthread_mutex_lock(&audio_buflock);
    audbuf_timecode = timecode;
    samples_buffered = (long long)((timecode * effdsp) / 100000.0);
    pthread_mutex_unlock(&audio_buflock);
}

void AudioOutputBase::SetEffDsp(int dsprate)
{
    VERBOSE(VB_AUDIO, LOC + QString("SetEffDsp: %1").arg(dsprate));
    effdsp = dsprate;
    effdspstretched = (int)((float)effdsp / audio_stretchfactor);
}

void AudioOutputBase::SetBlocking(bool blocking)
{
    this->blocking = blocking;
}

int AudioOutputBase::audiolen(bool use_lock)
{
    /* Thread safe, returns the number of valid bytes in the audio buffer */
    int ret;
    
    if (use_lock) 
        pthread_mutex_lock(&audio_buflock);

    if (waud >= raud)
        ret = waud - raud;
    else
        ret = AUDBUFSIZE - (raud - waud);

    if (use_lock)
        pthread_mutex_unlock(&audio_buflock);

    return ret;
}

int AudioOutputBase::audiofree(bool use_lock)
{
    return AUDBUFSIZE - audiolen(use_lock) - 1;
    /* There is one wasted byte in the buffer. The case where waud = raud is
       interpreted as an empty buffer, so the fullest the buffer can ever
       be is AUDBUFSIZE - 1. */
}

int AudioOutputBase::GetAudiotime(void)
{
    /* Returns the current timecode of audio leaving the soundcard, based
       on the 'audiotime' computed earlier, and the delay since it was computed.

       This is a little roundabout...

       The reason is that computing 'audiotime' requires acquiring the audio 
       lock, which the video thread should not do. So, we call 'SetAudioTime()'
       from the audio thread, and then call this from the video thread. */
    long long ret;
    struct timeval now;

    if (audiotime == 0)
        return 0;

    pthread_mutex_lock(&avsync_lock);

    gettimeofday(&now, NULL);

    ret = (now.tv_sec - audiotime_updated.tv_sec) * 1000;
    ret += (now.tv_usec - audiotime_updated.tv_usec) / 1000;
    ret = (long long)(ret * audio_stretchfactor);

#if 1
    VERBOSE(VB_AUDIO|VB_TIMESTAMP, 
            QString("GetAudiotime now=%1.%2, set=%3.%4, ret=%5, audt=%6 sf=%7")
            .arg(now.tv_sec).arg(now.tv_usec)
            .arg(audiotime_updated.tv_sec).arg(audiotime_updated.tv_usec)
            .arg(ret)
            .arg(audiotime)
            .arg(audio_stretchfactor)
           );
#endif

    ret += audiotime;

    pthread_mutex_unlock(&avsync_lock);
    return (int)ret;
}

void AudioOutputBase::SetAudiotime(void)
{
    if (audbuf_timecode == 0)
        return;

    int soundcard_buffer = 0;
    int totalbuffer;

    /* We want to calculate 'audiotime', which is the timestamp of the audio
       which is leaving the sound card at this instant.

       We use these variables:

       'effdsp' is samples/sec, multiplied by 100.
       Bytes per sample is assumed to be 4.

       'audiotimecode' is the timecode of the audio that has just been 
       written into the buffer.

       'totalbuffer' is the total # of bytes in our audio buffer, and the
       sound card's buffer.

       'ms/byte' is given by '25000/effdsp'...
     */

    pthread_mutex_lock(&audio_buflock);
    pthread_mutex_lock(&avsync_lock);
 
    soundcard_buffer = getBufferedOnSoundcard(); // bytes
    totalbuffer = audiolen(false) + soundcard_buffer;
 
    // include algorithmic latencies
    if (pSoundStretch)
    {
        // add the effect of any unused but processed samples,
        // AC3 reencode does this
        totalbuffer += (int)(pSoundStretch->numSamples() *
                             audio_bytes_per_sample);
        // add the effect of unprocessed samples in time stretch algo
        totalbuffer += (int)((pSoundStretch->numUnprocessedSamples() *
                              audio_bytes_per_sample) / audio_stretchfactor);
    }

    if (upmixer && needs_upmix)
    {
        totalbuffer += upmixer->sampleLatency() * audio_bytes_per_sample;
    }

    audiotime = audbuf_timecode - (int)(totalbuffer * 100000.0 /
                                   (audio_bytes_per_sample * effdspstretched));
 
    gettimeofday(&audiotime_updated, NULL);
#if 1
    VERBOSE(VB_AUDIO|VB_TIMESTAMP, 
            QString("SetAudiotime set=%1.%2, audt=%3 atc=%4 "
                    "tb=%5 sb=%6 eds=%7 abps=%8 sf=%9")
            .arg(audiotime_updated.tv_sec).arg(audiotime_updated.tv_usec)
            .arg(audiotime)
            .arg(audbuf_timecode)
            .arg(totalbuffer)
            .arg(soundcard_buffer)
            .arg(effdspstretched)
            .arg(audio_bytes_per_sample)
            .arg(audio_stretchfactor));
#endif

    pthread_mutex_unlock(&avsync_lock);
    pthread_mutex_unlock(&audio_buflock);
}

bool AudioOutputBase::AddSamples(char *buffers[], int samples, 
                                 long long timecode)
{
    // NOTE: This function is not threadsafe
    int afree = audiofree(true);
    int abps = (encoder) ?
        encoder->audio_bytes_per_sample : audio_bytes_per_sample;
    int len = samples * abps;

    // Check we have enough space to write the data
    if (need_resampler && src_ctx)
        len = (int)ceilf(float(len) * src_data.src_ratio);

    // include samples in upmix buffer that may be flushed
    if (needs_upmix && upmixer)
        len += upmixer->numUnprocessedSamples() * abps;

    if (pSoundStretch)
        len += (pSoundStretch->numUnprocessedSamples() +
                (int)(pSoundStretch->numSamples()/audio_stretchfactor))*abps;

    if (((len > afree) || ((audbuf_timecode - GetAudiotime()) > 2000)) && !blocking) 
    {
        VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC + QString(
                "AddSamples FAILED bytes=%1, used=%2, free=%3, timecode=%4") 
                .arg(len).arg(AUDBUFSIZE-afree).arg(afree)
                .arg(timecode)); 

        return false; // would overflow
    }

    // resample input if necessary
    if (need_resampler && src_ctx) 
    {
        // Convert to floats
        // TODO: Implicit assumption dealing with 16 bit input only.
        short **buf_ptr = (short**)buffers;
        for (int sample = 0; sample < samples; sample++) 
        {
            for (int channel = 0; channel < audio_channels; channel++) 
            {
                src_in[sample] = buf_ptr[channel][sample] / (1.0 * 0x8000);
            }
        }

        src_data.input_frames = samples;
        src_data.end_of_input = 0;
        int error = src_process(src_ctx, &src_data);
        if (error)
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    QString("Error occured while resampling audio: %1")
                    .arg(src_strerror(error)));

        src_float_to_short_array(src_data.data_out, (short int*)tmp_buff,
                                 src_data.output_frames_gen*audio_channels);

        _AddSamples(tmp_buff, true, src_data.output_frames_gen, timecode);
    } 
    else 
    {
        // Call our function to do the work
        _AddSamples(buffers, false, samples, timecode);
    }

    return true;
}

bool AudioOutputBase::AddSamples(char *buffer, int samples, long long timecode)
{
    // NOTE: This function is not threadsafe

    int afree = audiofree(true);
    int abps = (encoder) ?
        encoder->audio_bytes_per_sample : audio_bytes_per_sample;
    int len = samples * abps;

    // Check we have enough space to write the data
    if (need_resampler && src_ctx)
        len = (int)ceilf(float(len) * src_data.src_ratio);

    // include samples in upmix buffer that may be flushed
    if (needs_upmix && upmixer)
        len += upmixer->numUnprocessedSamples() * abps;
 
    if (pSoundStretch)
    {
        len += (pSoundStretch->numUnprocessedSamples() +
                (int)(pSoundStretch->numSamples()/audio_stretchfactor))*abps;
    }

    if (((len > afree) || (audiotime && ((audbuf_timecode - GetAudiotime()) > 2000))) && !blocking) 
    {
        VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC + QString(
                "AddSamples FAILED bytes=%1, used=%2, free=%3, timecode=%4") 
                .arg(len).arg(AUDBUFSIZE-afree).arg(afree)
                .arg(timecode)); 
        return false; // would overflow
    }

    // resample input if necessary
    if (need_resampler && src_ctx) 
    {
        // Convert to floats
        short *buf_ptr = (short*)buffer;
        for (int sample = 0; sample < samples * audio_channels; sample++) 
        {       
            src_in[sample] = (float)buf_ptr[sample] / (1.0 * 0x8000);
        }
        
        src_data.input_frames = samples;
        src_data.end_of_input = 0;
        int error = src_process(src_ctx, &src_data);
        if (error)
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    QString("Error occured while resampling audio: %1")
                    .arg(src_strerror(error)));
        src_float_to_short_array(src_data.data_out, (short int*)tmp_buff, 
                                 src_data.output_frames_gen*audio_channels);

        _AddSamples(tmp_buff, true, src_data.output_frames_gen, timecode);
    } 
    else 
    {
        // Call our function to do the work
        _AddSamples(buffer, true, samples, timecode);
    }

    return true;
}

int AudioOutputBase::WaitForFreeSpace(int samples)
{
    int abps = (encoder) ?
        encoder->audio_bytes_per_sample : audio_bytes_per_sample;
    int len = samples * abps;
    int afree = audiofree(false);

    while (len > afree)
    {
        if (blocking)
        {
            VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC + "Waiting for free space " +
                    QString("(need %1, available %2)").arg(len).arg(afree));

            // wait for more space
            pthread_cond_wait(&audio_bufsig, &audio_buflock);
            afree = audiofree(false);
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + 
                    QString("Audio buffer overflow, %1 audio samples lost!")
                    .arg(samples - (afree / abps)));
            samples = afree / abps;
            len = samples * abps;
            if (src_ctx) 
            {
                int error = src_reset(src_ctx);
                if (error)
                    VERBOSE(VB_IMPORTANT, LOC_ERR + QString(
                            "Error occured while resetting resampler: %1")
                            .arg(src_strerror(error)));
            }
        }
    }
    return len;
}

void AudioOutputBase::_AddSamples(void *buffer, bool interleaved, int samples, 
                                  long long timecode)
{
    pthread_mutex_lock(&audio_buflock);

    int len; // = samples * audio_bytes_per_sample;
    int audio_bytes = audio_bits / 8;
    int org_waud = waud;
    
    int afree = audiofree(false);

    int abps = (encoder) ?
        encoder->audio_bytes_per_sample : audio_bytes_per_sample;

    VERBOSE(VB_AUDIO|VB_TIMESTAMP, 
            LOC + QString("_AddSamples samples=%1 bytes=%2, used=%3, "
                          "free=%4, timecode=%5 needsupmix %6")
            .arg(samples)
            .arg(samples * abps)
            .arg(AUDBUFSIZE-afree).arg(afree).arg(timecode)
            .arg(needs_upmix));
    
    if (upmixer && needs_upmix)
    {
        int out_samples = 0;
        int step = (interleaved)?source_audio_channels:1;
        len = WaitForFreeSpace(samples);    // test
        for (int itemp = 0; itemp < samples; )
        {
            // just in case it does a processing cycle, release the lock
            // to allow the output loop to do output
            pthread_mutex_unlock(&audio_buflock);
            if (audio_bytes == 2)
            {
                itemp += upmixer->putSamples(
                    (short*)buffer + itemp * step,
                    samples - itemp,
                    source_audio_channels,
                    (interleaved) ? 0 : samples);
            }
            else
            {
                itemp += upmixer->putSamples(
                    (char*)buffer + itemp * step,
                    samples - itemp,
                    source_audio_channels,
                    (interleaved) ? 0 : samples);
            }
            pthread_mutex_lock(&audio_buflock);

            int copy_samples = upmixer->numSamples();
            if (copy_samples)
            {
                int copy_len = copy_samples * abps;
                out_samples += copy_samples;
                if (out_samples > samples)
                    len = WaitForFreeSpace(out_samples);
                int bdiff = AUDBUFSIZE - org_waud;
                if (bdiff < copy_len) 
                {
                    int bdiff_samples = bdiff/abps;
                    upmixer->receiveSamples(
                        (short*)(audiobuffer + org_waud), bdiff_samples);
                    upmixer->receiveSamples(
                        (short*)(audiobuffer), (copy_samples - bdiff_samples));
                }
                else
                {
                    upmixer->receiveSamples(
                        (short*)(audiobuffer + org_waud), copy_samples);
                }
                org_waud = (org_waud + copy_len) % AUDBUFSIZE;
            }
        }

        if (samples > 0)
            len = WaitForFreeSpace(out_samples);

        samples = out_samples;
    }
    else
    {
        len = WaitForFreeSpace(samples);

        if (interleaved) 
        {
            char *mybuf = (char*)buffer;
            int bdiff = AUDBUFSIZE - org_waud;
            if (bdiff < len)
            {
                memcpy(audiobuffer + org_waud, mybuf, bdiff);
                memcpy(audiobuffer, mybuf + bdiff, len - bdiff);
            }
            else
            {
                memcpy(audiobuffer + org_waud, mybuf, len);
            }
     
            org_waud = (org_waud + len) % AUDBUFSIZE;
        } 
        else 
        {
            char **mybuf = (char**)buffer;
            for (int itemp = 0; itemp < samples * audio_bytes;
                 itemp += audio_bytes)
            {
                for (int chan = 0; chan < audio_channels; chan++)
                {
                    audiobuffer[org_waud++] = mybuf[chan][itemp];
                    if (audio_bits == 16)
                        audiobuffer[org_waud++] = mybuf[chan][itemp+1];

                    if (org_waud >= AUDBUFSIZE)
                        org_waud -= AUDBUFSIZE;
                }
            }
        }
    }

    if (samples > 0)
    {
        if (pSoundStretch)
        {

            // does not change the timecode, only the number of samples
            // back to orig pos
            org_waud = waud;
            int bdiff = AUDBUFSIZE - org_waud;
            int nSamplesToEnd = bdiff/abps;
            if (bdiff < len)
            {
                pSoundStretch->putSamples((soundtouch::SAMPLETYPE*)
                                          (audiobuffer + 
                                           org_waud), nSamplesToEnd);
                pSoundStretch->putSamples((soundtouch::SAMPLETYPE*)audiobuffer,
                                          (len - bdiff) / abps);
            }
            else
            {
                pSoundStretch->putSamples((soundtouch::SAMPLETYPE*)
                                          (audiobuffer + org_waud),
                                          len / abps);
            }

            if (encoder)
            {
                // pull out a packet's worth and reencode it until we
                // don't have enough for any more packets
                soundtouch::SAMPLETYPE *temp_buff = 
                    (soundtouch::SAMPLETYPE*)encoder->GetFrameBuffer();
                size_t frameSize = encoder->FrameSize()/abps;

                VERBOSE(VB_AUDIO|VB_TIMESTAMP,
                        QString("_AddSamples Enc sfs=%1 bfs=%2 sss=%3")
                        .arg(frameSize)
                        .arg(encoder->FrameSize())
                        .arg(pSoundStretch->numSamples()));

                // process the same number of samples as it creates
                // a full encoded buffer just like before
                while (pSoundStretch->numSamples() >= frameSize)
                {
                    int got = pSoundStretch->receiveSamples(
                        temp_buff, frameSize);
                    int amount = encoder->Encode(temp_buff);

                    VERBOSE(VB_AUDIO|VB_TIMESTAMP, 
                            QString("_AddSamples Enc bytes=%1 got=%2 left=%3")
                            .arg(amount)
                            .arg(got)
                            .arg(pSoundStretch->numSamples()));

                    if (!amount)
                        continue;

                    //len = WaitForFreeSpace(amount);
                    char *ob = encoder->GetOutBuff();
                    if (amount >= bdiff)
                    {
                        memcpy(audiobuffer + org_waud, ob, bdiff);
                        ob += bdiff;
                        amount -= bdiff;
                        org_waud = 0;
                    }
                    if (amount > 0)
                        memcpy(audiobuffer + org_waud, ob, amount);

                    bdiff = AUDBUFSIZE - amount;
                    org_waud += amount;
                }
            }
            else
            {
                int newLen = 0;
                int nSamples;
                len = WaitForFreeSpace(pSoundStretch->numSamples() * 
                                       audio_bytes_per_sample);
                do 
                {
                    int samplesToGet = len/audio_bytes_per_sample;
                    if (samplesToGet > nSamplesToEnd)
                    {
                        samplesToGet = nSamplesToEnd;    
                    }

                    nSamples = pSoundStretch->receiveSamples(
                        (soundtouch::SAMPLETYPE*)
                        (audiobuffer + org_waud), samplesToGet);
                    if (nSamples == nSamplesToEnd)
                    {
                        org_waud = 0;
                        nSamplesToEnd = AUDBUFSIZE/audio_bytes_per_sample;
                    }
                    else
                    {
                        org_waud += nSamples * audio_bytes_per_sample;
                        nSamplesToEnd -= nSamples;
                    }

                    newLen += nSamples * audio_bytes_per_sample;
                    len -= nSamples * audio_bytes_per_sample;
                } while (nSamples > 0);
            }
        }

        waud = org_waud;
        lastaudiolen = audiolen(false);

        if (timecode < 0)
        {
            // mythmusic doesn't give timestamps..
            timecode = (int)((samples_buffered * 100000.0) / effdsp);
        }
        
        samples_buffered += samples;
        
        /* we want the time at the end -- but the file format stores
           time at the start of the chunk. */
        // even with timestretch, timecode is still calculated from original
        // sample count
        audbuf_timecode = timecode + (int)((samples * 100000.0) / effdsp);

        if (interleaved)
        {
            dispatchVisual((unsigned char *)buffer, len, timecode,
                           source_audio_channels, audio_bits);
        }
    }

    pthread_mutex_unlock(&audio_buflock);
}

void AudioOutputBase::Status()
{
    long ct = GetAudiotime();

    if (ct < 0)
        ct = 0;

    if (source_bitrate == -1)
    {
        source_bitrate = audio_samplerate * source_audio_channels * audio_bits;
    }

    if (ct / 1000 != current_seconds) 
    {
        current_seconds = ct / 1000;
        OutputEvent e(current_seconds, ct,
                      source_bitrate, audio_samplerate, audio_bits, 
                      source_audio_channels);
        dispatch(e);
    }
}

void AudioOutputBase::GetBufferStatus(uint &fill, uint &total)
{
    fill = AUDBUFSIZE - audiofree(true);
    total = AUDBUFSIZE;
}

void AudioOutputBase::OutputAudioLoop(void)
{
    int space_on_soundcard, last_space_on_soundcard;
    unsigned char zeros[fragment_size];
    unsigned char fragment[fragment_size];
 
    bzero(zeros, fragment_size);
    last_space_on_soundcard = 0;

    while (!killaudio)
    {
        if (pauseaudio)
        {
            if (!audio_actually_paused)
            {
                VERBOSE(VB_AUDIO, LOC + "OutputAudioLoop: audio paused");
                OutputEvent e(OutputEvent::Paused);
                dispatch(e);
                was_paused = true;
            }

            audio_actually_paused = true;
            
            audiotime = 0; // mark 'audiotime' as invalid.

            space_on_soundcard = getSpaceOnSoundcard();

            if (space_on_soundcard != last_space_on_soundcard)
            {
                VERBOSE(VB_AUDIO|VB_TIMESTAMP,
                        LOC + QString("%1 bytes free on soundcard")
                        .arg(space_on_soundcard));

                last_space_on_soundcard = space_on_soundcard;
            }

            // only send zeros if card doesn't already have at least one
            // fragment of zeros -dag
            if (fragment_size >= soundcard_buffer_size - space_on_soundcard) 
            {
                if (fragment_size <= space_on_soundcard) {
                    WriteAudio(zeros, fragment_size);
                } else {
                    // this should never happen now -dag
                    VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC + 
                            QString("waiting for space on soundcard "
                                    "to write zeros: have %1 need %2")
                            .arg(space_on_soundcard).arg(fragment_size));
                    usleep(5000);
                }
            }

            usleep(2000);
            continue;
        }
        else
        {
            if (was_paused) 
            {
                VERBOSE(VB_AUDIO, LOC + "OutputAudioLoop: Play Event");
                OutputEvent e(OutputEvent::Playing);
                dispatch(e);
                was_paused = false;
            }
        }

        space_on_soundcard = getSpaceOnSoundcard();
        
        // if nothing has gone out the soundcard yet no sense calling
        // this (think very fast loops here when soundcard doesn't have
        // space to take another fragment) -dag
        if (space_on_soundcard != last_space_on_soundcard)
            SetAudiotime(); // once per loop, calculate stuff for a/v sync

        /* do audio output */
        
        // wait for the buffer to fill with enough to play
        if (fragment_size > audiolen(true))
        {
            if (audiolen(true) > 0)  // only log if we're sending some audio
                VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC +
                        QString("audio waiting for buffer to fill: "
                                "have %1 want %2")
                        .arg(audiolen(true)).arg(fragment_size));

            //VERBOSE(VB_AUDIO|VB_TIMESTAMP,
            //LOC + "Broadcasting free space avail");
            pthread_mutex_lock(&audio_buflock);
            pthread_cond_broadcast(&audio_bufsig);
            pthread_mutex_unlock(&audio_buflock);

            usleep(2000);
            continue;
        }
        
        // wait for there to be free space on the sound card so we can write
        // without blocking.  We don't want to block while holding audio_buflock
        if (fragment_size > space_on_soundcard)
        {
            if (space_on_soundcard != last_space_on_soundcard) {
                VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC +
                        QString("audio waiting for space on soundcard: "
                                "have %1 need %2")
                        .arg(space_on_soundcard).arg(fragment_size));
                last_space_on_soundcard = space_on_soundcard;
            }

            numlowbuffer++;
            if (numlowbuffer > 5 && audio_buffer_unused)
            {
                VERBOSE(VB_IMPORTANT, LOC + "dropping back audio_buffer_unused");
                audio_buffer_unused /= 2;
            }

            usleep(5000);
            continue;
        }
        else
            numlowbuffer = 0;

        Status();

        if (GetAudioData(fragment, fragment_size, true))
            WriteAudio(fragment, fragment_size);
    }

    VERBOSE(VB_AUDIO, LOC + "OutputAudioLoop: Stop Event");
    OutputEvent e(OutputEvent::Stopped);
    dispatch(e);
}

int AudioOutputBase::GetAudioData(unsigned char *buffer, int buf_size, bool full_buffer)
{
    pthread_mutex_lock(&audio_buflock); // begin critical section
    
    // re-check audiolen() in case things changed.
    // for example, ClearAfterSeek() might have run
    int avail_size = audiolen(false);
    int fragment_size = buf_size;
    int written_size = 0;
    if (!full_buffer && (buf_size > avail_size))
    {
        // when full_buffer is false, return any available data
        fragment_size = avail_size;
    }
    
    if (avail_size && (fragment_size <= avail_size))
    {
        int bdiff = AUDBUFSIZE - raud;
        if (fragment_size > bdiff)
        {
            // always want to write whole fragments
            memcpy(buffer, audiobuffer + raud, bdiff);
            memcpy(buffer + bdiff, audiobuffer, fragment_size - bdiff);
        }
        else
        {
            memcpy(buffer, audiobuffer + raud, fragment_size);
        }

        /* update raud */
        raud = (raud + fragment_size) % AUDBUFSIZE;
        VERBOSE(VB_AUDIO|VB_TIMESTAMP, LOC + "Broadcasting free space avail");
        pthread_cond_broadcast(&audio_bufsig);

        written_size = fragment_size;
    }
    pthread_mutex_unlock(&audio_buflock); // end critical section
    
    // Mute individual channels through mono->stereo duplication
    kMuteState mute_state = GetMute();
    if (written_size &&
        audio_channels > 1 &&
        (mute_state == MUTE_LEFT || mute_state == MUTE_RIGHT))
    {
        int offset_src = 0;
        int offset_dst = 0;
        
        if (mute_state == MUTE_LEFT)
            offset_src = audio_bits / 8;    // copy channel 1 to channel 0
        else if (mute_state == MUTE_RIGHT)
            offset_dst = audio_bits / 8;    // copy channel 0 to channel 1
            
        for (int i = 0; i < written_size; i += audio_bytes_per_sample)
        {
            buffer[i + offset_dst] = buffer[i + offset_src];
            if (audio_bits == 16)
                buffer[i + offset_dst + 1] = buffer[i + offset_src + 1];
        }
    }
    
    return written_size;
}

// Wait for all data to finish playing
void AudioOutputBase::Drain()
{
    while (audiolen(true) > fragment_size)
    {
        usleep(1000);
    }
}

void *AudioOutputBase::kickoffOutputAudioLoop(void *player)
{
    VERBOSE(VB_AUDIO, LOC + QString("kickoffOutputAudioLoop: pid = %1")
                                    .arg(getpid()));
    ((AudioOutputBase *)player)->OutputAudioLoop();
    VERBOSE(VB_AUDIO, LOC + "kickoffOutputAudioLoop exiting");
    return NULL;
}

int AudioOutputBase::readOutputData(unsigned char*, int)
{
    VERBOSE(VB_IMPORTANT, LOC_ERR + "base AudioOutputBase should not be "
                                    "getting asked to readOutputData()");
    return 0;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */

