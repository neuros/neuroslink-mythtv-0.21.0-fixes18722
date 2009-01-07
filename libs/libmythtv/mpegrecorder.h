// -*- Mode: c++ -*-

#ifndef MPEGRECORDER_H_
#define MPEGRECORDER_H_

#include "recorderbase.h"

struct AVFormatContext;
struct AVPacket;

class MpegRecorder : public RecorderBase
{
  public:
    MpegRecorder(TVRec*);
   ~MpegRecorder();
    void TeardownAll(void);

    void SetOption(const QString &opt, int value);
    void SetOption(const QString &name, const QString &value);
    void SetVideoFilters(QString&) {}

    void SetOptionsFromProfile(RecordingProfile *profile,
                               const QString &videodev, 
                               const QString &audiodev,
                               const QString &vbidev);

    void Initialize(void) {}
    void StartRecording(void);
    void StopRecording(void);
    void Reset(void);

    void Pause(bool clear = true);
    bool PauseAndWait(int timeout = 100);

    bool IsRecording(void) { return recording; }
    bool IsErrored(void) { return errored; }

    long long GetFramesWritten(void) { return framesWritten; }

    bool Open(void);
    int GetVideoFd(void) { return chanfd; }

    long long GetKeyframePosition(long long desired);

    void SetNextRecording(const ProgramInfo*, RingBuffer*);

  private:
    bool SetupRecording(void);
    void FinishRecording(void);
    void HandleKeyframe(void);

    void ProcessData(unsigned char *buffer, int len);

    bool OpenMpegFileAsInput(void);
    bool OpenV4L2DeviceAsInput(void);
    bool SetIVTVDeviceOptions(int chanfd);
    bool SetV4L2DeviceOptions(int chanfd);
    bool SetVBIOptions(int chanfd);
    uint GetFilteredStreamType(void) const;
    uint GetFilteredAudioSampleRate(void) const;
    uint GetFilteredAudioLayer(void) const;
    uint GetFilteredAudioBitRate(uint audio_layer) const;

    void ResetForNewFile(void);

    bool deviceIsMpegFile;
    int bufferSize;

    // Driver info
    QString  card;
    QString  driver;
    uint32_t version;
    bool     usingv4l2;
    bool     has_buggy_vbi;
    bool     has_v4l2_vbi;
    bool     requires_special_pause;

    // State
    bool recording;
    bool encoding;
    bool errored;

    // Pausing state
    bool cleartimeonpause;

    // Number of frames written
    long long framesWritten;

    // Encoding info
    int width, height;
    int bitrate, maxbitrate, streamtype, aspectratio;
    int audtype, audsamplerate, audbitratel1, audbitratel2, audbitratel3;
    int audvolume;
    unsigned int language; ///< 0 is Main Lang; 1 is SAP Lang; 2 is Dual

    // Input file descriptors
    int chanfd;
    int readfd;

    // Keyframe tracking inforamtion
    int keyframedist;
    bool gopset;
    unsigned int leftovers;
    long long lastpackheaderpos;
    long long lastseqstart;
    long long numgops;

    // buffer used for ...
    unsigned char *buildbuffer;
    unsigned int buildbuffersize;

    static const int   audRateL1[];
    static const int   audRateL2[];
    static const int   audRateL3[];
    static const char *streamType[];
    static const char *aspectRatio[];
    static const unsigned int kBuildBufferMaxSize;
};
#endif
