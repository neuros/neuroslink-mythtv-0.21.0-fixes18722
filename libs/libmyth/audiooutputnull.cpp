#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#ifndef USING_MINGW
#include <sys/ioctl.h>
#else
#include "compat.h"
#endif
#include <cerrno>
#include <cstring>
#include <iostream>
#include <qdatetime.h>
#include "config.h"

using namespace std;

#include "mythcontext.h"
#include "audiooutputnull.h"

AudioOutputNULL::AudioOutputNULL(
    QString laudio_main_device, QString           laudio_passthru_device,
    int     laudio_bits,        int               laudio_channels,
    int     laudio_samplerate,  AudioOutputSource lsource,
    bool    lset_initial_vol,   bool              laudio_passthru) :
    AudioOutputBase(laudio_main_device, laudio_passthru_device,
                    laudio_bits,        laudio_channels,
                    laudio_samplerate,  lsource,
                    lset_initial_vol,   laudio_passthru),
    pcm_output_buffer_mutex(false),
    current_buffer_size(0),
    locked_audio_channels(laudio_channels),
    locked_audio_bits(laudio_bits),
    locked_audio_samplerate(laudio_samplerate)
{
    bzero(pcm_output_buffer, sizeof(char) * NULLAUDIO_OUTPUT_BUFFER_SIZE);

    Reconfigure(laudio_bits,       laudio_channels,
                laudio_samplerate, laudio_passthru);
}

AudioOutputNULL::~AudioOutputNULL()
{
    KillAudio();
}

bool AudioOutputNULL::OpenDevice()
{
    VERBOSE(VB_GENERAL, "Opening NULL audio device.");
    
    fragment_size = NULLAUDIO_OUTPUT_BUFFER_SIZE / 2;
    soundcard_buffer_size = NULLAUDIO_OUTPUT_BUFFER_SIZE;
    
    audio_bits = locked_audio_bits;
    audio_channels = locked_audio_channels;
    audio_samplerate = locked_audio_samplerate;
    
    return true;
}

void AudioOutputNULL::CloseDevice()
{
}


void AudioOutputNULL::WriteAudio(unsigned char* aubuf, int size)
{
    if (buffer_output_data_for_use)
    {
        if (size + current_buffer_size > NULLAUDIO_OUTPUT_BUFFER_SIZE)
        {
            VERBOSE(VB_IMPORTANT, "null audio output should not have just "
                                  "had data written to it");
            return;
        }
        pcm_output_buffer_mutex.lock();
            memcpy(pcm_output_buffer + current_buffer_size, aubuf, size);
            current_buffer_size += size;
        pcm_output_buffer_mutex.unlock();
    }
}

int AudioOutputNULL::readOutputData(unsigned char *read_buffer, int max_length)
{
    int amount_to_read = max_length;
    if (amount_to_read > current_buffer_size)
    {
        amount_to_read = current_buffer_size;
    }

    pcm_output_buffer_mutex.lock();
    memcpy(read_buffer, pcm_output_buffer, amount_to_read);
    memmove(pcm_output_buffer, pcm_output_buffer + amount_to_read,
            current_buffer_size - amount_to_read);
    current_buffer_size -= amount_to_read;
    pcm_output_buffer_mutex.unlock();
    
    return amount_to_read;
}

void AudioOutputNULL::Reset()
{
    if (buffer_output_data_for_use)
    {
        pcm_output_buffer_mutex.lock();
            current_buffer_size = 0;
        pcm_output_buffer_mutex.unlock();
    }
    AudioOutputBase::Reset();
}

inline int AudioOutputNULL::getBufferedOnSoundcard(void) 
{
    if (buffer_output_data_for_use)
    {
        return current_buffer_size;
    }

    return 0;
}


inline int AudioOutputNULL::getSpaceOnSoundcard(void)
{
    if (buffer_output_data_for_use)
    {
        return NULLAUDIO_OUTPUT_BUFFER_SIZE - current_buffer_size;
    }
    else
    {
        return NULLAUDIO_OUTPUT_BUFFER_SIZE;
    }
}

