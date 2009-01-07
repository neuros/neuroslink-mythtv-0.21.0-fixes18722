#ifndef AVFORMATDECODER_H_
#define AVFORMATDECODER_H_

#include <qstring.h>
#include <qmap.h>

#include "programinfo.h"
#include "format.h"
#include "decoderbase.h"
#include "vbilut.h"
#include "h264utils.h"

extern "C" {
#include "frame.h"
#include "../libavcodec/avcodec.h"
#include "../libavformat/avformat.h"
}

#include "avfringbuffer.h"

#define CODEC_IS_MPEG(c)     (c == CODEC_ID_MPEG1VIDEO      || \
                              c == CODEC_ID_MPEG2VIDEO      || \
                              c == CODEC_ID_MPEG2VIDEO_DVDV || \
                              c == CODEC_ID_MPEG2VIDEO_XVMC || \
                              c == CODEC_ID_MPEG2VIDEO_XVMC_VLD)

#define CODEC_IS_HW_ACCEL(c) (c == CODEC_ID_MPEG2VIDEO_DVDV || \
                              c == CODEC_ID_MPEG2VIDEO_XVMC || \
                              c == CODEC_ID_MPEG2VIDEO_XVMC_VLD)

class TeletextDecoder;
class CC608Decoder;
class CC708Decoder;
class InteractiveTV;
class ProgramInfo;
class MythSqlDatabase;

extern "C" void HandleStreamChange(void*);

class AudioInfo
{
  public:
    AudioInfo() :
        codec_id(CODEC_ID_NONE), sample_size(-2),   sample_rate(-1),
        channels(-1), do_passthru(false)
    {;}

    AudioInfo(CodecID id, int sr, int ch, bool passthru) :
        codec_id(id), sample_size(ch*2),   sample_rate(sr),
        channels(ch), do_passthru(passthru)
    {;}

    CodecID codec_id;
    int sample_size, sample_rate, channels;
    bool do_passthru;

    /// \brief Bits per sample.
    int bps(void) const
    {
        uint chan = (channels) ? channels : 2;
        return (8 * sample_size) / chan;
    }
    bool operator==(const AudioInfo &o) const
    {
        return (codec_id==o.codec_id        && channels==o.channels       &&
                sample_size==o.sample_size  && sample_rate==o.sample_rate &&
                do_passthru==o.do_passthru);
    }
    QString toString() const
    {
        return QString("id(%1) %2Hz %3ch %4bps%5")
            .arg(codec_id_string(codec_id),4).arg(sample_rate,5)
            .arg(channels,2).arg(bps(),3)
            .arg((do_passthru) ? "pt":"",3);
    }
};

/// A decoder for video files.

/// The AvFormatDecoder is used to decode non-NuppleVideo files.
/// It's used a a decoder of last resort after trying the NuppelDecoder
/// and IvtvDecoder (if "USING_IVTV" is defined).
class AvFormatDecoder : public DecoderBase
{
    friend void HandleStreamChange(void*);
  public:
    AvFormatDecoder(NuppelVideoPlayer *parent, ProgramInfo *pginfo,
                    bool use_null_video_out, bool allow_libmpeg2 = true);
   ~AvFormatDecoder();

    void CloseCodecs();
    void CloseContext();
    void Reset(void);
    void Reset(bool reset_video_data = true, bool seek_reset = true);

    /// Perform an av_probe_input_format on the passed data to see if we
    /// can decode it with this class.
    static bool CanHandle(char testbuf[kDecoderProbeBufferSize], 
                          const QString &filename,
                          int testbufsize = kDecoderProbeBufferSize);

    /// Open our file and set up or audio and video parameters.
    int OpenFile(RingBuffer *rbuffer, bool novideo, 
                 char testbuf[kDecoderProbeBufferSize],
                 int testbufsize = kDecoderProbeBufferSize);

    bool GetFrame(int onlyvideo);

    bool isLastFrameKey(void) { return false; }

    /// This is a No-op for this class.
    void WriteStoredData(RingBuffer *rb, bool storevid, long timecodeOffset)
                           { (void)rb; (void)storevid; (void)timecodeOffset;}

    /// This is a No-op for this class.
    void SetRawAudioState(bool state) { (void)state; }

    /// This is a No-op for this class.
    bool GetRawAudioState(void) const { return false; }

    /// This is a No-op for this class.
    void SetRawVideoState(bool state) { (void)state; }

    /// This is a No-op for this class.
    bool GetRawVideoState(void) const { return false; }

    /// This is a No-op for this class.
    long UpdateStoredFrameNum(long frame) { (void)frame; return 0;}

    QString      GetCodecDecoderName(void) const;
    MythCodecID  GetVideoCodecID(void) const { return video_codec_id; }
    void        *GetVideoCodecPrivate(void);

    virtual void SetDisablePassThrough(bool disable);
    void AddTextData(unsigned char *buf, int len, long long timecode, char type);

    virtual QString GetTrackDesc(uint type, uint trackNo) const;
    virtual int SetTrack(uint type, int trackNo);

    int ScanStreams(bool novideo);

    virtual bool DoRewind(long long desiredFrame, bool doflush = true);
    virtual bool DoFastForward(long long desiredFrame, bool doflush = true);

    virtual int  GetTeletextDecoderType(void) const;
    virtual void SetTeletextDecoderViewer(TeletextViewer*);

    virtual QString GetXDS(const QString&) const;

    // MHEG stuff
    virtual bool SetAudioByComponentTag(int tag);
    virtual bool SetVideoByComponentTag(int tag);

  protected:
    RingBuffer *getRingBuf(void) { return ringBuffer; }

    virtual int AutoSelectTrack(uint type);

    void ScanATSCCaptionStreams(int av_stream_index);
    void ScanTeletextCaptions(int av_stream_index);
    void ScanDSMCCStreams(void);
    int AutoSelectAudioTrack(void);

  private:
    friend int get_avf_buffer(struct AVCodecContext *c, AVFrame *pic);
    friend void release_avf_buffer(struct AVCodecContext *c, AVFrame *pic);

    friend int get_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic);
    friend void release_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic);
    friend void render_slice_xvmc(struct AVCodecContext *c, const AVFrame *src,
                                  int offset[4], int y, int type, int height);

    friend void decode_cc_dvd(struct AVCodecContext *c, const uint8_t *buf, int buf_size);

    friend int open_avf(URLContext *h, const char *filename, int flags);
    friend int read_avf(URLContext *h, uint8_t *buf, int buf_size);
    friend int write_avf(URLContext *h, uint8_t *buf, int buf_size);
    friend offset_t seek_avf(URLContext *h, offset_t offset, int whence);
    friend int close_avf(URLContext *h);

    void DecodeDTVCC(const uint8_t *buf);
    void InitByteContext(void);
    void InitVideoCodec(AVStream *stream, AVCodecContext *enc,
                        bool selectedStream = false);

    /// Preprocess a packet, setting the video parms if nessesary.
    void MpegPreProcessPkt(AVStream *stream, AVPacket *pkt);
    void H264PreProcessPkt(AVStream *stream, AVPacket *pkt);

    void ProcessVBIDataPacket(const AVStream *stream, const AVPacket *pkt);
    void ProcessDVBDataPacket(const AVStream *stream, const AVPacket *pkt);
    void ProcessDSMCCPacket(const AVStream *stream, const AVPacket *pkt);

    float GetMpegAspect(AVCodecContext *context, int aspect_ratio_info,
                        int width, int height);

    void SeekReset(long long, uint skipFrames, bool doFlush, bool discardFrames);

    bool SetupAudioStream(void);
    void SetupAudioStreamSubIndexes(int streamIndex);
    void RemoveAudioStreams();

    /// Update our position map, keyframe distance, and the like.
    /// Called for key frame packets.
    void HandleGopStart(AVPacket *pkt);

    bool GenerateDummyVideoFrame(void);
    bool HasVideo(const AVFormatContext *ic);

  private:
    class AvFormatDecoderPrivate *d;
    H264::KeyframeSequencer *h264_kf_seq;

    AVFormatContext *ic;
    AVFormatParameters params;

    URLContext readcontext;

    int frame_decoded;
    VideoFrame *decoded_video_frame;
    AVFRingBuffer *avfRingBuffer;

    bool directrendering;
    bool drawband;

    bool no_dts_hack;

    bool gopset;
    /// A flag to indicate that we've seen a GOP frame.  Used in junction with seq_count.
    bool seen_gop;
    int seq_count; ///< A counter used to determine if we need to force a call to HandleGopStart

    QPtrList<AVPacket> storedPackets;

    int firstgoppos;
    int prevgoppos;

    bool gotvideo;

    uint32_t  start_code_state;

    long long lastvpts;
    long long lastapts;
    long long lastccptsu;

    bool using_null_videoout;
    MythCodecID video_codec_id;

    int maxkeyframedist;

    // Caption/Subtitle/Teletext decoders
    CC608Decoder     *ccd608;
    CC708Decoder     *ccd708;
    TeletextDecoder  *ttd;
    int               cc608_parity_table[256];

    // MHEG
    InteractiveTV    *itv;                ///< MHEG/MHP decoder
    int               selectedVideoIndex; ///< MHEG/MHP video stream to use.

    // Audio
    short int        *audioSamples;
    bool              allow_ac3_passthru;
    bool              allow_dts_passthru;
    bool              disable_passthru;
    uint              max_channels;

    VideoFrame       *dummy_frame;

    AudioInfo         audioIn;
    AudioInfo         audioOut;

    // DVD
    int  lastdvdtitle;
    bool decodeStillFrame;
    bool dvd_xvmc_enabled;
    bool dvd_video_codec_changed;
    bool dvdTitleChanged;
    bool mpeg_seq_end_seen;
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */
