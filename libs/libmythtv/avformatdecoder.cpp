// C headers
#include <cassert>
#include <unistd.h>
#include <cmath>

// C++ headers
#include <algorithm>
#include <iostream>
using namespace std;

// MythTV headers
#include "mythconfig.h"
#include "avformatdecoder.h"
#include "RingBuffer.h"
#include "NuppelVideoPlayer.h"
#include "remoteencoder.h"
#include "programinfo.h"
#include "mythcontext.h"
#include "mythdbcon.h"
#include "iso639.h"
#include "mpegtables.h"
#include "atscdescriptors.h"
#include "dvbdescriptors.h"
#include "cc608decoder.h"
#include "cc708decoder.h"
#include "interactivetv.h"
#include "DVDRingBuffer.h"
#include "videodisplayprofile.h"

#include "videoout_dvdv.h"    // AvFormatDecoderPrivate has DVDV ptr
#include "videoout_quartz.h"  // For VOQ::GetBestSupportedCodec()

#ifdef USING_XVMC
#include "videoout_xv.h"
extern "C" {
#include "libavcodec/xvmc_render.h"
}
#endif // USING_XVMC

extern "C" {
#include "../libavutil/avutil.h"
#include "../libavcodec/ac3_parser.h"
#include "../libmythmpeg2/mpeg2.h"
#include "ivtv_myth.h"
// from libavcodec
extern const uint8_t *ff_find_start_code(const uint8_t * restrict p, const uint8_t *end, uint32_t * restrict state);
}

#define LOC QString("AFD: ")
#define LOC_ERR QString("AFD Error: ")
#define LOC_WARN QString("AFD Warning: ")

#define MAX_AC3_FRAME_SIZE 6144

static const float eps = 1E-5;

static int cc608_parity(uint8_t byte);
static int cc608_good_parity(const int *parity_table, uint16_t data);
static void cc608_build_parity_table(int *parity_table);

static int dts_syncinfo(uint8_t *indata_ptr, int *flags,
                        int *sample_rate, int *bit_rate);
static int dts_decode_header(uint8_t *indata_ptr, int *rate,
                             int *nblks, int *sfreq);
static int encode_frame(bool dts, unsigned char* data, int len,
                        short *samples, int &samples_size);

int get_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic);
int get_avf_buffer(struct AVCodecContext *c, AVFrame *pic);
void release_avf_buffer(struct AVCodecContext *c, AVFrame *pic);
void release_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic);
void render_slice_xvmc(struct AVCodecContext *s, const AVFrame *src,
                       int offset[4], int y, int type, int height);
void decode_cc_dvd(struct AVCodecContext *c, const uint8_t *buf, int buf_size);

static void myth_av_log(void *ptr, int level, const char* fmt, va_list vl)
{
    static QString full_line("");
    static const int msg_len = 255;
    static QMutex string_lock;

    // determine mythtv debug level from av log level
    uint verbose_level = (level < AV_LOG_WARNING) ? VB_IMPORTANT : VB_LIBAV;

    if (!(print_verbose_messages & verbose_level))
        return;

    string_lock.lock();
    if (full_line.isEmpty() && ptr) {
        AVClass* avc = *(AVClass**)ptr;
        full_line.sprintf("[%s @ %p]", avc->item_name(ptr), avc);
    }

    char str[msg_len+1];
    int bytes = vsnprintf(str, msg_len+1, fmt, vl);
    // check for truncted messages and fix them
    if (bytes > msg_len)
    {
        VERBOSE(VB_IMPORTANT, QString("Libav log output truncated %1 of %2 bytes written")
                .arg(msg_len).arg(bytes));
        str[msg_len-1] = '\n';
    }

    full_line += QString(str);
    if (full_line.endsWith("\n"))
    {
        full_line.truncate(full_line.length() - 1);
        VERBOSE(verbose_level, full_line);
        full_line.truncate(0);
    }
    string_lock.unlock();
}

static int get_canonical_lang(const char *lang_cstr)
{
    if (lang_cstr[0] == '\0' || lang_cstr[1] == '\0')
    {
        return iso639_str3_to_key("und");
    }
    else if (lang_cstr[2] == '\0')
    {
        QString tmp2 = lang_cstr;
        QString tmp3 = iso639_str2_to_str3(tmp2);
        int lang = iso639_str3_to_key(tmp3.ascii());
        return iso639_key_to_canonical_key(lang);
    }
    else
    {
        int lang = iso639_str3_to_key(lang_cstr);
        return iso639_key_to_canonical_key(lang);
    }
}

typedef MythDeque<AVFrame*> avframe_q;

/**
 * Management of libmpeg2 decoding
 */
class AvFormatDecoderPrivate
{
  public:
    AvFormatDecoderPrivate(bool allow_libmpeg2)
        : mpeg2dec(NULL), dvdvdec(NULL), allow_mpeg2dec(allow_libmpeg2) { ; }
   ~AvFormatDecoderPrivate() { DestroyMPEG2(); }
    
    bool InitMPEG2(const QString &dec);
    bool HasMPEG2Dec(void) const { return (bool)(mpeg2dec); }
    bool HasDVDVDec(void) const { return (bool)(dvdvdec); }
    bool HasDecoder(void) const { return HasMPEG2Dec() || HasDVDVDec(); }

    void DestroyMPEG2();
    void ResetMPEG2();
    int DecodeMPEG2Video(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr, uint8_t *buf, int buf_size);

    // Mac OS X Hardware DVD-Video decoder
    bool SetVideoSize(const QSize &video_dim);
    DVDV *GetDVDVDecoder(void) { return dvdvdec; }

  private:
    mpeg2dec_t *mpeg2dec;
    DVDV       *dvdvdec;
    bool        allow_mpeg2dec;
    avframe_q   partialFrames;
};

/**
 * \brief Initialise either libmpeg2, or DVDV (Mac HW accel), to do decoding
 *
 * Both of these are meant to be alternatives to FFMPEG,
 * but currently, DVDV uses the MPEG demuxer in FFMPEG
 */
bool AvFormatDecoderPrivate::InitMPEG2(const QString &dec)
{
    // only ffmpeg is used for decoding previews
    if (!allow_mpeg2dec)
        return false;
    DestroyMPEG2();

#ifdef USING_DVDV
    if (dec == "macaccel")
    {
        dvdvdec = new DVDV();
        if (dvdvdec)
        {
            VERBOSE(VB_PLAYBACK,
                    LOC + "Using Mac Acceleration (DVDV) for video decoding");
        }
    }
#endif // !USING_DVDV

    if (dec == "libmpeg2")
    {
        mpeg2dec = mpeg2_init();
        if (mpeg2dec)
            VERBOSE(VB_PLAYBACK, LOC + "Using libmpeg2 for video decoding");
    }

    return HasDecoder();
}

void AvFormatDecoderPrivate::DestroyMPEG2()
{
    if (mpeg2dec)
    {
        mpeg2_close(mpeg2dec);
        mpeg2dec = NULL;

        avframe_q::iterator it = partialFrames.begin();
        for (; it != partialFrames.end(); ++it)
            delete (*it);
        partialFrames.clear();
    }

    if (dvdvdec)
    {
        delete dvdvdec;
        dvdvdec = NULL;
    }
}

void AvFormatDecoderPrivate::ResetMPEG2()
{
    if (mpeg2dec)
    {
        mpeg2_reset(mpeg2dec, 0);

        avframe_q::iterator it = partialFrames.begin();
        for (; it != partialFrames.end(); ++it)
            delete (*it);
        partialFrames.clear();
    }

    if (dvdvdec)
        dvdvdec->Reset();
}

int AvFormatDecoderPrivate::DecodeMPEG2Video(AVCodecContext *avctx,
                                             AVFrame *picture,
                                             int *got_picture_ptr,
                                             uint8_t *buf, int buf_size)
{
    if (dvdvdec)
    {
        if (!dvdvdec->PreProcessFrame(avctx))
        {
            VERBOSE(VB_ALL, "DVDV::PreProcessFrame() failed");
            DestroyMPEG2();
            return -1;
        }

        int ret = avcodec_decode_video(avctx, picture,
                                       got_picture_ptr, buf, buf_size);

        dvdvdec->PostProcessFrame(avctx, (VideoFrame *)(picture->opaque),
                                  picture->pict_type, *got_picture_ptr);
        return ret;
    }

    *got_picture_ptr = 0;
    const mpeg2_info_t *info = mpeg2_info(mpeg2dec);
    mpeg2_buffer(mpeg2dec, buf, buf + buf_size);
    while (1)
    {
        switch (mpeg2_parse(mpeg2dec))
        {
            case STATE_SEQUENCE:
                // libmpeg2 needs three buffers to do its work.
                // We set up two prediction buffers here, from
                // the set of available video frames.
                mpeg2_custom_fbuf(mpeg2dec, 1);
                for (int i = 0; i < 2; i++)
                {
                    avctx->get_buffer(avctx, picture);
                    mpeg2_set_buf(mpeg2dec, picture->data, picture->opaque);
                }
                break;
            case STATE_PICTURE:
                // This sets up the third buffer for libmpeg2.
                // We use up one of the three buffers for each
                // frame shown. The frames get released once
                // they are drawn (outside this routine).
                avctx->get_buffer(avctx, picture);
                mpeg2_set_buf(mpeg2dec, picture->data, picture->opaque);
                break;
            case STATE_BUFFER:
                // We're finished with the buffer...
                if (partialFrames.size())
                {
                    AVFrame *frm = partialFrames.dequeue();
                    *got_picture_ptr = 1;
                    *picture = *frm;
                    delete frm;
#if 0
                    QString msg("");
                    AvFormatDecoder *nd = (AvFormatDecoder *)(avctx->opaque);
                    if (nd && nd->GetNVP() && nd->GetNVP()->getVideoOutput())
                        msg = nd->GetNVP()->getVideoOutput()->GetFrameStatus();

                    VERBOSE(VB_IMPORTANT, "ret frame: "<<picture->opaque
                            <<"           "<<msg);
#endif
                }
                return buf_size;
            case STATE_INVALID:
                // This is the error state. The decoder must be
                // reset on an error.
                ResetMPEG2();
                return -1;

            case STATE_SLICE:
            case STATE_END:
            case STATE_INVALID_END:
                if (info->display_fbuf)
                {
                    bool exists = false;
                    avframe_q::iterator it = partialFrames.begin();
                    for (; it != partialFrames.end(); ++it)
                        if ((*it)->opaque == info->display_fbuf->id)
                            exists = true;

                    if (!exists)
                    {
                        AVFrame *frm = new AVFrame();
                        frm->data[0] = info->display_fbuf->buf[0];
                        frm->data[1] = info->display_fbuf->buf[1];
                        frm->data[2] = info->display_fbuf->buf[2];
                        frm->data[3] = NULL;
                        frm->opaque  = info->display_fbuf->id;
                        frm->type    = FF_BUFFER_TYPE_USER;
                        frm->top_field_first =
                            !!(info->display_picture->flags &
                               PIC_FLAG_TOP_FIELD_FIRST);
                        frm->interlaced_frame =
                            !(info->display_picture->flags &
                              PIC_FLAG_PROGRESSIVE_FRAME);
                        frm->repeat_pict = 
                            !!(info->display_picture->flags &
                               PIC_FLAG_REPEAT_FIELD); 
                        partialFrames.enqueue(frm);
                        
                    }
                }
                if (info->discard_fbuf)
                {
                    bool exists = false;
                    avframe_q::iterator it = partialFrames.begin();
                    for (; it != partialFrames.end(); ++it)
                    {
                        if ((*it)->opaque == info->discard_fbuf->id)
                        {
                            exists = true;
                            (*it)->data[3] = (unsigned char*) 1;
                        }
                    }

                    if (!exists)
                    {
                        AVFrame frame;
                        frame.opaque = info->discard_fbuf->id;
                        frame.type   = FF_BUFFER_TYPE_USER;
                        avctx->release_buffer(avctx, &frame);
                    }
                }
                break;
            default:
                break;
        }
    }
}

bool AvFormatDecoderPrivate::SetVideoSize(const QSize &video_dim)
{
    if (dvdvdec && !dvdvdec->SetVideoSize(video_dim))
    {
        DestroyMPEG2();
        return false;
    }

    return true;
}

AvFormatDecoder::AvFormatDecoder(NuppelVideoPlayer *parent,
                                 ProgramInfo *pginfo,
                                 bool use_null_videoout,
                                 bool allow_libmpeg2)
    : DecoderBase(parent, pginfo),
      d(new AvFormatDecoderPrivate(allow_libmpeg2)),
      h264_kf_seq(new H264::KeyframeSequencer()),
      ic(NULL),
      frame_decoded(0),             decoded_video_frame(NULL),
      avfRingBuffer(NULL),
      directrendering(false),       drawband(false),
      gopset(false),                seen_gop(false),
      seq_count(0),                 firstgoppos(0),
      prevgoppos(0),                gotvideo(false),
      start_code_state(0xffffffff),
      lastvpts(0),                  lastapts(0),
      lastccptsu(0),
      using_null_videoout(use_null_videoout),
      video_codec_id(kCodec_NONE),
      maxkeyframedist(-1), 
      // Closed Caption & Teletext decoders
      ccd608(new CC608Decoder(parent)),
      ccd708(new CC708Decoder(parent)),
      ttd(new TeletextDecoder()),
      // Interactive TV
      itv(NULL),
      selectedVideoIndex(-1),
      // Audio
      audioSamples(new short int[AVCODEC_MAX_AUDIO_FRAME_SIZE]),
      allow_ac3_passthru(false),    allow_dts_passthru(false),
      disable_passthru(false),      max_channels(2),
      dummy_frame(NULL),
      // DVD
      lastdvdtitle(-1),
      decodeStillFrame(false),
      dvd_xvmc_enabled(false), dvd_video_codec_changed(false),
      dvdTitleChanged(false), mpeg_seq_end_seen(false)
{
    bzero(&params, sizeof(AVFormatParameters));
    bzero(audioSamples, AVCODEC_MAX_AUDIO_FRAME_SIZE * sizeof(short int));
    ccd608->SetIgnoreTimecode(true);

    bool debug = (bool)(print_verbose_messages & VB_LIBAV);
    av_log_set_level((debug) ? AV_LOG_DEBUG : AV_LOG_ERROR);
    av_log_set_callback(myth_av_log);

    allow_ac3_passthru = gContext->GetNumSetting("AC3PassThru", false);
    allow_dts_passthru = gContext->GetNumSetting("DTSPassThru", false);
    max_channels = (uint) gContext->GetNumSetting("MaxChannels", 2);

    audioIn.sample_size = -32; // force SetupAudioStream to run once
    itv = GetNVP()->GetInteractiveTV();

    cc608_build_parity_table(cc608_parity_table);

    no_dts_hack = false;
}

AvFormatDecoder::~AvFormatDecoder()
{
    while (storedPackets.count() > 0)
    {
        AVPacket *pkt = storedPackets.first();
        storedPackets.removeFirst();
        av_free_packet(pkt);
        delete pkt;
    }

    CloseContext();
    delete ccd608;
    delete ccd708;
    delete ttd;
    delete d;
    delete h264_kf_seq;
    if (audioSamples)
        delete [] audioSamples;

    if (dummy_frame)
    {
        delete [] dummy_frame->buf;
        delete dummy_frame;
        dummy_frame = NULL;
    }

    if (avfRingBuffer)
        delete avfRingBuffer;
}

void AvFormatDecoder::CloseCodecs()
{
    if (ic)
    {
        for (uint i = 0; i < ic->nb_streams; i++)
        {
            QMutexLocker locker(&avcodeclock);
            AVStream *st = ic->streams[i];
            if (st->codec->codec)
                avcodec_close(st->codec);
        }
    }
}
    
void AvFormatDecoder::CloseContext()
{
    if (ic)
    {
        CloseCodecs();
        
        AVInputFormat *fmt = ic->iformat;
        ic->iformat->flags |= AVFMT_NOFILE;

        av_free(ic->pb.buffer);
        av_close_input_file(ic);
        ic = NULL;
        fmt->flags &= ~AVFMT_NOFILE;
    }
        
    d->DestroyMPEG2();
    h264_kf_seq->Reset();
}

static int64_t lsb3full(int64_t lsb, int64_t base_ts, int lsb_bits)
{
    int64_t mask = (lsb_bits < 64) ? (1LL<<lsb_bits)-1 : -1LL;
    return  ((lsb - base_ts)&mask);
}

bool AvFormatDecoder::DoRewind(long long desiredFrame, bool discardFrames)
{
    VERBOSE(VB_PLAYBACK, LOC + "DoRewind("
            <<desiredFrame<<", "
            <<( discardFrames ? "do" : "don't" )<<" discard frames)");

    if (recordingHasPositionMap || livetv)
        return DecoderBase::DoRewind(desiredFrame, discardFrames);

    // avformat-based seeking
    return DoFastForward(desiredFrame, discardFrames);
}

bool AvFormatDecoder::DoFastForward(long long desiredFrame, bool discardFrames)
{
    VERBOSE(VB_PLAYBACK, LOC +
            QString("DoFastForward(%1 (%2), %3 discard frames)")
            .arg(desiredFrame).arg(framesPlayed)
            .arg((discardFrames) ? "do" : "don't"));

    if (recordingHasPositionMap || livetv)
        return DecoderBase::DoFastForward(desiredFrame, discardFrames);

    bool oldrawstate = getrawframes;
    getrawframes = false;

    AVStream *st = NULL;
    for (uint i = 0; i < ic->nb_streams; i++)
    {
        AVStream *st1 = ic->streams[i];
        if (st1 && st1->codec->codec_type == CODEC_TYPE_VIDEO)
        {
            st = st1;
            break;
        }
    }
    if (!st)
        return false;

    int64_t frameseekadjust = 0;
    AVCodecContext *context = st->codec;

    if (CODEC_IS_MPEG(context->codec_id))
        frameseekadjust = maxkeyframedist+1;

    // convert framenumber to normalized timestamp
    long double diff = (max(desiredFrame - frameseekadjust, 0LL)) * AV_TIME_BASE;
    long long ts = (long long)( diff / fps );
    if (av_seek_frame(ic, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR 
                <<"av_seek_frame(ic, -1, "<<ts<<", 0) -- error");
        return false;
    }

    // If seeking to start of stream force a DTS and start_time of zero as
    // libav sometimes returns the end of the stream instead.
    if (desiredFrame <= 1)
    {
        av_update_cur_dts(ic, st, 0);
        ic->start_time = 0;
    }

    int normalframes = 0;

    if (st->cur_dts != (int64_t)AV_NOPTS_VALUE)
    {

        int64_t adj_cur_dts = st->cur_dts;

        if (ic->start_time != (int64_t)AV_NOPTS_VALUE)
        {
            int64_t st1 = av_rescale(ic->start_time,
                                    st->time_base.den,
                                    AV_TIME_BASE * (int64_t)st->time_base.num);
            adj_cur_dts = lsb3full(adj_cur_dts, st1, st->pts_wrap_bits);

        }

        long long newts = av_rescale(adj_cur_dts,
                                (int64_t)AV_TIME_BASE * (int64_t)st->time_base.num,
                                st->time_base.den);

        // convert current timestamp to frame number
        lastKey = (long long)((newts*(long double)fps)/AV_TIME_BASE);
        framesPlayed = lastKey;
        framesRead = lastKey;

        normalframes = desiredFrame - framesPlayed;
        normalframes = max(normalframes, 0);
        no_dts_hack = false;
    }
    else
    {
        VERBOSE(VB_GENERAL, "No DTS Seeking Hack!");
        no_dts_hack = true;
        framesPlayed = desiredFrame;
        framesRead = desiredFrame;
        normalframes = 0;
    }

    SeekReset(lastKey, normalframes, discardFrames, discardFrames);

    if (discardFrames)
    {
        GetNVP()->SetFramesPlayed(framesPlayed + 1);
        GetNVP()->getVideoOutput()->SetFramesPlayed(framesPlayed + 1);
    }

    getrawframes = oldrawstate;

    return true;
}

void AvFormatDecoder::SeekReset(long long newKey, uint skipFrames,
                                bool doflush, bool discardFrames)
{
    if (ringBuffer->isDVD())
    {
        if (ringBuffer->InDVDMenuOrStillFrame() ||
            newKey == 0) 
            return;
    }
            
    VERBOSE(VB_PLAYBACK, LOC +
            QString("SeekReset(%1, %2, %3 flush, %4 discard)")
            .arg(newKey).arg(skipFrames)
            .arg((doflush) ? "do" : "don't")
            .arg((discardFrames) ? "do" : "don't"));

    DecoderBase::SeekReset(newKey, skipFrames, doflush, discardFrames);

    if (doflush)
    {
        lastapts = 0;
        lastvpts = 0;
        lastccptsu = 0;
        av_read_frame_flush(ic);

        // Only reset the internal state if we're using our seeking,
        // not when using libavformat's seeking
        if (recordingHasPositionMap || livetv)
        {
            ic->pb.pos = ringBuffer->GetReadPosition();
            ic->pb.buf_ptr = ic->pb.buffer;
            ic->pb.buf_end = ic->pb.buffer;
            ic->pb.eof_reached = 0;
        }

        // Flush the avcodec buffers
        VERBOSE(VB_PLAYBACK, LOC + "SeekReset() flushing");
        for (uint i = 0; i < ic->nb_streams; i++)
        {
            AVCodecContext *enc = ic->streams[i]->codec;
            if (enc->codec)
                avcodec_flush_buffers(enc);
        }
        d->ResetMPEG2();
    }

    // Discard all the queued up decoded frames
    if (discardFrames)
        GetNVP()->DiscardVideoFrames(doflush);

    if (doflush)
    {
        // Free up the stored up packets
        while (storedPackets.count() > 0)
        {
            AVPacket *pkt = storedPackets.first();
            storedPackets.removeFirst();
            av_free_packet(pkt);
            delete pkt;
        }

        prevgoppos = 0;
        gopset = false;
        if (!ringBuffer->isDVD())
        {
            if (!no_dts_hack)
            {
                framesPlayed = lastKey;
                framesRead = lastKey;
            }

            no_dts_hack = false;
        }
    }

    // Skip all the desired number of skipFrames
    for (;skipFrames > 0 && !ateof; skipFrames--)
    {
	GetFrame(0);
        if (decoded_video_frame)
            GetNVP()->DiscardVideoFrame(decoded_video_frame);
    }
}

void AvFormatDecoder::Reset(bool reset_video_data, bool seek_reset)
{
    VERBOSE(VB_PLAYBACK, LOC + QString("Reset(%1, %2)")
            .arg(reset_video_data).arg(seek_reset));
    if (seek_reset)
        SeekReset(0, 0, true, false);

    if (reset_video_data)
    {
        m_positionMap.clear();
        framesPlayed = 0;
        framesRead = 0;
        seen_gop = false;
        seq_count = 0;
    }
}

void AvFormatDecoder::Reset()
{
    DecoderBase::Reset();

    if (ringBuffer->isDVD())
    {
        posmapStarted = false;
        SyncPositionMap();
    }

#if 0
// This is causing problems, and may not be needed anymore since
// we do not reuse the same file for different streams anymore. -- dtk

    // mpeg ts reset
    if (QString("mpegts") == ic->iformat->name)
    {
        AVInputFormat *fmt = (AVInputFormat*) av_mallocz(sizeof(AVInputFormat));
        memcpy(fmt, ic->iformat, sizeof(AVInputFormat));
        fmt->flags |= AVFMT_NOFILE;

        CloseContext();
        ic = av_alloc_format_context();
        if (!ic)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "Reset(): Could not allocate format context.");
            av_free(fmt);
            errored = true;
            return;
        }

        InitByteContext();
        ic->streams_changed = HandleStreamChange;
        ic->stream_change_data = this;

        char *filename = (char *)(ringBuffer->GetFilename().ascii());
        int err = av_open_input_file(&ic, filename, fmt, 0, &params);
        if (err < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Reset(): "
                    "avformat err("<<err<<") on av_open_input_file call.");
            av_free(fmt);
            errored = true;
            return;
        }

        fmt->flags &= ~AVFMT_NOFILE;
    }
#endif
}

bool AvFormatDecoder::CanHandle(char testbuf[kDecoderProbeBufferSize], 
                                const QString &filename, int testbufsize)
{
    avcodeclock.lock();
    av_register_all();
    avcodeclock.unlock();

    AVProbeData probe;

    probe.filename = (char *)(filename.ascii());
    probe.buf = (unsigned char *)testbuf;
    probe.buf_size = testbufsize;

    if (av_probe_input_format(&probe, true))
        return true;
    return false;
}

void AvFormatDecoder::InitByteContext(void)
{
    int streamed = 0;
    if (ringBuffer->isDVD() || ringBuffer->LiveMode())
        streamed = 1;

    readcontext.prot = &AVF_RingBuffer_Protocol;
    readcontext.flags = 0;
    readcontext.is_streamed = streamed;
    readcontext.max_packet_size = 0;
    readcontext.priv_data = avfRingBuffer;

    if (ringBuffer->isDVD())
        ic->pb.buffer_size = 2048;
    else
        ic->pb.buffer_size = 32768;

    ic->pb.buffer = (unsigned char *)av_malloc(ic->pb.buffer_size);
    ic->pb.buf_ptr = ic->pb.buffer;
    ic->pb.write_flag = 0;
    ic->pb.buf_end = ic->pb.buffer;
    ic->pb.opaque = &readcontext;
    ic->pb.read_packet = AVF_Read_Packet;
    ic->pb.write_packet = AVF_Write_Packet;
    ic->pb.seek = AVF_Seek_Packet;
    ic->pb.pos = 0;
    ic->pb.must_flush = 0;
    ic->pb.eof_reached = 0;
    ic->pb.is_streamed = streamed;
    ic->pb.max_packet_size = 0;
}

extern "C" void HandleStreamChange(void* data) {
    AvFormatDecoder* decoder = (AvFormatDecoder*) data;
    int cnt = decoder->ic->nb_streams;

    VERBOSE(VB_PLAYBACK, LOC + "HandleStreamChange(): "
            "streams_changed "<<data<<" -- stream count "<<cnt);

    QMutexLocker locker(&avcodeclock);
    decoder->SeekReset(0, 0, true, true);
    decoder->ScanStreams(false);
}

/**
 *  OpenFile opens a ringbuffer for playback.
 *
 *  OpenFile deletes any existing context then use testbuf to
 *  guess at the stream type. It then calls ScanStreams to find
 *  any valid streams to decode. If possible a position map is
 *  also built for quick skipping.
 *
 *  \param rbuffer pointer to a valid ringuffer.
 *  \param novideo if true then no video is sought in ScanSreams.
 *  \param testbuf this paramater is not used by AvFormatDecoder.
 */
int AvFormatDecoder::OpenFile(RingBuffer *rbuffer, bool novideo, 
                              char testbuf[kDecoderProbeBufferSize],
                              int testbufsize)
{
    CloseContext();

    ringBuffer = rbuffer;

    if (avfRingBuffer)
        delete avfRingBuffer;
    avfRingBuffer = new AVFRingBuffer(rbuffer);

    AVInputFormat *fmt = NULL;
    char *filename = (char *)(rbuffer->GetFilename().ascii());

    AVProbeData probe;
    probe.filename = filename;
    probe.buf = (unsigned char *)testbuf;
    probe.buf_size = testbufsize;

    fmt = av_probe_input_format(&probe, true);
    if (!fmt)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Probe failed for file: \"%1\".").arg(filename));
        return -1;
    }

    fmt->flags |= AVFMT_NOFILE;

    ic = av_alloc_format_context();
    if (!ic)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Could not allocate format context.");
        return -1;
    }

    InitByteContext();

    int err = av_open_input_file(&ic, filename, fmt, 0, &params);
    if (err < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR
                <<"avformat err("<<err<<") on av_open_input_file call.");
        return -1;
    }

    int ret = -1;
    if (ringBuffer->isDVD())
    {
        AVPacket pkt1;
        while (ic->nb_streams == 0)
            ret = av_read_frame(ic,&pkt1);
        av_free_packet(&pkt1);
        ringBuffer->Seek(0, SEEK_SET);
        ringBuffer->DVD()->IgnoreStillOrWait(false);
    }
    else
    {
        QMutexLocker locker(&avcodeclock);
        ret = av_find_stream_info(ic);
    }

    if (ret < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Could not find codec parameters. " +
                QString("file was \"%1\".").arg(filename));
        av_close_input_file(ic);
        ic = NULL;
        return -1;
    }
    ic->streams_changed = HandleStreamChange;
    ic->stream_change_data = this;

    fmt->flags &= ~AVFMT_NOFILE;

    if (!ringBuffer->isDVD() && !livetv)
        av_estimate_timings(ic, 0);

    // Scan for the initial A/V streams
    ret = ScanStreams(novideo);
    if (-1 == ret)
        return ret;

    AutoSelectTracks(); // This is needed for transcoder

    {
        int initialAudio = -1, initialVideo = -1;
        if (itv || (itv = GetNVP()->GetInteractiveTV()))
            itv->GetInitialStreams(initialAudio, initialVideo);
        if (initialAudio >= 0)
            SetAudioByComponentTag(initialAudio);
        if (initialVideo >= 0)
            SetVideoByComponentTag(initialVideo);
    }

    // Try to get a position map from the recorder if we don't have one yet.
    if (!recordingHasPositionMap)
    {
        if ((m_playbackinfo) || livetv || watchingrecording)
        {
            recordingHasPositionMap |= SyncPositionMap();
            if (recordingHasPositionMap && !livetv && !watchingrecording)
            {
                hasFullPositionMap = true;
                gopset = true;
            }
        }
    }

    // If we don't have a position map, set up ffmpeg for seeking
    if (!recordingHasPositionMap)
    {
        VERBOSE(VB_PLAYBACK, LOC +
                "Recording has no position -- using libavformat seeking.");
        int64_t dur = ic->duration / (int64_t)AV_TIME_BASE;

        if (dur > 0)
        {
            GetNVP()->SetFileLength((int)(dur), (int)(dur * fps));
        }
        else
        {
            // the pvr-250 seems to overreport the bitrate by * 2
            float bytespersec = (float)bitrate / 8 / 2;
            float secs = ringBuffer->GetRealFileSize() * 1.0 / bytespersec;
            GetNVP()->SetFileLength((int)(secs), (int)(secs * fps));
        }

        // we will not see a position map from db or remote encoder,
        // set the gop interval to 15 frames.  if we guess wrong, the
        // auto detection will change it.
        keyframedist = 15;
        positionMapType = MARK_GOP_START;

        if (!strcmp(fmt->name, "avi"))
        {
            // avi keyframes are too irregular
            keyframedist = 1;
            positionMapType = MARK_GOP_BYFRAME;
        }

        dontSyncPositionMap = true;
    }

    // Don't build a seek index for MythTV files, the user needs to
    // use mythcommflag to build a proper MythTV position map for these.
    if (livetv || watchingrecording)
        ic->build_index = 0;

    dump_format(ic, 0, filename, 0);

    // print some useful information if playback debugging is on
    if (hasFullPositionMap)
        VERBOSE(VB_PLAYBACK, LOC + "Position map found");
    else if (recordingHasPositionMap)
        VERBOSE(VB_PLAYBACK, LOC + "Partial position map found");
    VERBOSE(VB_PLAYBACK, LOC +
            QString("Successfully opened decoder for file: "
                    "\"%1\". novideo(%2)").arg(filename).arg(novideo));

    // Return true if recording has position map
    return recordingHasPositionMap;
}

static float normalized_fps(AVStream *stream, AVCodecContext *enc)
{
    float fps = 1.0f / av_q2d(enc->time_base);

    // Some formats report fps waaay too high. (wrong time_base)
    if (fps > 121.0f && (enc->time_base.den > 10000) &&
        (enc->time_base.num == 1))
    {
        enc->time_base.num = 1001;  // seems pretty standard
        if (av_q2d(enc->time_base) > 0)
            fps = 1.0f / av_q2d(enc->time_base);
    }
    // If it's still wonky, try the container's time_base
    if (fps > 121.0f || fps < 3.0f)
    {
        float tmpfps = 1.0f / av_q2d(stream->time_base);
        if (tmpfps > 20 && tmpfps < 70)
            fps = tmpfps;
    }

    // If it is still out of range, just assume NTSC...
    fps = (fps > 121.0f) ? (30000.0f / 1001.0f) : fps;
    return fps;
}

void AvFormatDecoder::InitVideoCodec(AVStream *stream, AVCodecContext *enc,
                                     bool selectedStream)
{
    VERBOSE(VB_PLAYBACK, LOC
            <<"InitVideoCodec() "<<enc<<" "
            <<"id("<<codec_id_string(enc->codec_id)
            <<") type ("<<codec_type_string(enc->codec_type)
            <<").");

    float aspect_ratio = 0.0;

    if (ringBuffer->isDVD())
        directrendering = false;

    if (selectedStream)
    {
        fps = normalized_fps(stream, enc);

        if (enc->sample_aspect_ratio.num == 0)
            aspect_ratio = 0.0f;
        else
            aspect_ratio = av_q2d(enc->sample_aspect_ratio) *
                enc->width / enc->height;

        if (aspect_ratio <= 0.0f || aspect_ratio > 6.0f)
            aspect_ratio = (float)enc->width / (float)enc->height;

        current_width = enc->width;
        current_height = enc->height;
        current_aspect = aspect_ratio;
    }

    enc->opaque = (void *)this;
    enc->get_buffer = avcodec_default_get_buffer;
    enc->release_buffer = avcodec_default_release_buffer;
    enc->draw_horiz_band = NULL;
    enc->slice_flags = 0;

    enc->error_resilience = FF_ER_COMPLIANT;
    enc->workaround_bugs = FF_BUG_AUTODETECT;
    enc->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
    enc->idct_algo = FF_IDCT_AUTO;
    enc->debug = 0;
    enc->rate_emu = 0;
    enc->error_rate = 0;

    AVCodec *codec = avcodec_find_decoder(enc->codec_id);    

    if (selectedStream &&
        !gContext->GetNumSetting("DecodeExtraAudio", 0) &&
        codec && !CODEC_IS_HW_ACCEL(codec->id))
    {
        SetLowBuffers(false);
    }

    if (codec && (codec->id == CODEC_ID_MPEG2VIDEO_XVMC ||
                  codec->id == CODEC_ID_MPEG2VIDEO_XVMC_VLD))
    {
        enc->flags |= CODEC_FLAG_EMU_EDGE;
        enc->get_buffer = get_avf_buffer_xvmc;
        enc->release_buffer = release_avf_buffer_xvmc;
        enc->draw_horiz_band = render_slice_xvmc;
        enc->slice_flags = SLICE_FLAG_CODED_ORDER |
            SLICE_FLAG_ALLOW_FIELD;
        if (selectedStream)
            directrendering = true;
    }
    else if (codec && codec->id == CODEC_ID_MPEG2VIDEO_DVDV)
    {
        enc->flags           |= (CODEC_FLAG_EMU_EDGE  |
//                                 CODEC_FLAG_TRUNCATED | 
                                 CODEC_FLAG_LOW_DELAY |
                                 CODEC_FLAG2_FAST);
        enc->get_buffer       = get_avf_buffer;
        enc->release_buffer   = release_avf_buffer;
        enc->draw_horiz_band  = NULL;
        directrendering      |= selectedStream;
    }
    else if (codec && codec->capabilities & CODEC_CAP_DR1)
    {
        enc->flags          |= CODEC_FLAG_EMU_EDGE;
        enc->get_buffer      = get_avf_buffer;
        enc->release_buffer  = release_avf_buffer;
        enc->draw_horiz_band = NULL;
        if (selectedStream)
            directrendering = true;
    }

    if (selectedStream)
    {
        uint width  = enc->width;
        uint height = enc->height;

        if (width == 0 && height == 0)
        {
            VERBOSE(VB_PLAYBACK, LOC + "InitVideoCodec "
                    "invalid dimensions, resetting decoder.");
            width = 640;
            height = 480;
            fps = 29.97;
            aspect_ratio = 4.0 / 3;
        }

        GetNVP()->SetVideoParams(width, height, fps,
                                 keyframedist, aspect_ratio, kScan_Detect, 
                                 dvd_video_codec_changed);
    }
}

#if defined(USING_XVMC) || defined(USING_DVDV)
static int mpeg_version(int codec_id)
{
    switch (codec_id)
    {
        case CODEC_ID_MPEG1VIDEO:
            return 1;
        case CODEC_ID_MPEG2VIDEO:
        case CODEC_ID_MPEG2VIDEO_XVMC:
        case CODEC_ID_MPEG2VIDEO_XVMC_VLD:
        case CODEC_ID_MPEG2VIDEO_DVDV:
            return 2;
        case CODEC_ID_H263:
            return 3;
        case CODEC_ID_MPEG4:
            return 4;
        case CODEC_ID_H264:
            return 5;
    }
    return 0;
}
#endif // defined(USING_XVMC) || defined(USING_DVDV)

#ifdef USING_XVMC
static int xvmc_pixel_format(enum PixelFormat pix_fmt)
{
    (void) pix_fmt;
    int xvmc_chroma = XVMC_CHROMA_FORMAT_420;
#if 0
// We don't support other chromas yet
    if (PIX_FMT_YUV420P == pix_fmt)
        xvmc_chroma = XVMC_CHROMA_FORMAT_420;
    else if (PIX_FMT_YUV422P == pix_fmt)
        xvmc_chroma = XVMC_CHROMA_FORMAT_422;
    else if (PIX_FMT_YUV420P == pix_fmt)
        xvmc_chroma = XVMC_CHROMA_FORMAT_444;
#endif
    return xvmc_chroma;
}
#endif // USING_XVMC

void default_captions(sinfo_vec_t *tracks, int av_index)
{
    if (tracks[kTrackTypeCC608].empty())
    {
        tracks[kTrackTypeCC608].push_back(StreamInfo(av_index, 0, 0, 1));
        tracks[kTrackTypeCC608].push_back(StreamInfo(av_index, 0, 2, 3));
    }
}

// CC Parity checking 
// taken from xine-lib libspucc

static int cc608_parity(uint8_t byte)
{
    int ones = 0;

    for (int i = 0; i < 7; i++)
    {
        if (byte & (1 << i))
            ones++;
    }

    return ones & 1;
}

// CC Parity checking 
// taken from xine-lib libspucc

static void cc608_build_parity_table(int *parity_table)
{
    uint8_t byte;
    int parity_v;
    for (byte = 0; byte <= 127; byte++)
    {
        parity_v = cc608_parity(byte);
        /* CC uses odd parity (i.e., # of 1's in byte is odd.) */
        parity_table[byte] = parity_v;
        parity_table[byte | 0x80] = !parity_v;
    }
}

// CC Parity checking 
// taken from xine-lib libspucc

static int cc608_good_parity(const int *parity_table, uint16_t data)
{
    int ret = parity_table[data & 0xff] && parity_table[(data & 0xff00) >> 8];
    if (!ret)
    {
        VERBOSE(VB_VBI, QString("VBI: Bad parity in EIA-608 data (%1)")
                .arg(data,0,16));
    }
    return ret;
}

void AvFormatDecoder::ScanATSCCaptionStreams(int av_index)
{
    tracks[kTrackTypeCC608].clear();
    tracks[kTrackTypeCC708].clear();

    // Figure out languages of ATSC captions
    if (!ic->cur_pmt_sect)
    {
        default_captions(tracks, av_index);
        return;
    }

    const PESPacket pes = PESPacket::ViewData(ic->cur_pmt_sect);
    const PSIPTable psip(pes);
    const ProgramMapTable pmt(psip);

    uint i;
    for (i = 0; i < pmt.StreamCount(); i++)
    {
        // MythTV remaps OpenCable Video to normal video during recording
        // so "dvb" is the safest choice for system info type, since this
        // will ignore other uses of the same stream id in DVB countries.
        if (pmt.IsVideo(i, "dvb"))
            break;
    }

    if (!pmt.IsVideo(i, "dvb"))
    {
        default_captions(tracks, av_index);
        return;
    }

    const desc_list_t desc_list = MPEGDescriptor::ParseOnlyInclude(
        pmt.StreamInfo(i), pmt.StreamInfoLength(i),
        DescriptorID::caption_service);

    map<int,uint> lang_cc_cnt[2];
    for (uint j = 0; j < desc_list.size(); j++)
    {
        const CaptionServiceDescriptor csd(desc_list[j]);
        for (uint k = 0; k < csd.ServicesCount(); k++)
        {
            int lang = csd.CanonicalLanguageKey(k);
            int type = csd.Type(k) ? 1 : 0;
            int lang_indx = lang_cc_cnt[type][lang];
            lang_cc_cnt[type][lang]++;
            if (type)
            {
                StreamInfo si(av_index, lang, lang_indx,
                              csd.CaptionServiceNumber(k),
                              csd.EasyReader(k),
                              csd.WideAspectRatio(k));

                tracks[kTrackTypeCC708].push_back(si);

                VERBOSE(VB_PLAYBACK, LOC + QString(
                            "EIA-708 caption service #%1 "
                            "is in the %2 language.")
                        .arg(csd.CaptionServiceNumber(k))
                        .arg(iso639_key_toName(lang)));
            }
            else
            {
                int line21 = csd.Line21Field(k) ? 2 : 1;
                StreamInfo si(av_index, lang, lang_indx, line21);
                tracks[kTrackTypeCC608].push_back(si);

                VERBOSE(VB_PLAYBACK, LOC + QString(
                            "EIA-608 caption %1 is in the %2 language.")
                        .arg(line21).arg(iso639_key_toName(lang)));
            }
        }
    }

    default_captions(tracks, av_index);
}

void AvFormatDecoder::ScanTeletextCaptions(int av_index)
{
    // ScanStreams() calls tracks[kTrackTypeTeletextCaptions].clear()
    if (!ic->cur_pmt_sect || tracks[kTrackTypeTeletextCaptions].size())
        return;

    const PESPacket pes = PESPacket::ViewData(ic->cur_pmt_sect);
    const PSIPTable psip(pes);
    const ProgramMapTable pmt(psip);

    for (uint i = 0; i < pmt.StreamCount(); i++)
    {
        if (pmt.StreamType(i) != 6)
            continue;

        const desc_list_t desc_list = MPEGDescriptor::ParseOnlyInclude(
            pmt.StreamInfo(i), pmt.StreamInfoLength(i),
            DescriptorID::teletext);

        for (uint j = 0; j < desc_list.size(); j++)
        {
            const TeletextDescriptor td(desc_list[j]);
            for (uint k = 0; k < td.StreamCount(); k++)
            {
                int type = td.TeletextType(k);
                if (type != 2)
                    continue;

                int language = td.CanonicalLanguageKey(k);
                int magazine = td.TeletextMagazineNum(k)?:8;
                int pagenum  = td.TeletextPageNum(k);
                int lang_idx = (magazine << 8) | pagenum;

                StreamInfo si(av_index, language, lang_idx, 0);
                tracks[kTrackTypeTeletextCaptions].push_back(si);

                VERBOSE(VB_PLAYBACK, LOC + QString(
                            "Teletext caption #%1 is in the %2 language "
                            "on page %3 %4.")
                        .arg(k).arg(iso639_key_toName(language))
                        .arg(magazine).arg(pagenum));
            }
        }

        // Assume there is only one multiplexed teletext stream in PMT..
        if (tracks[kTrackTypeTeletextCaptions].size())
            break;
    }
}

/** \fn AvFormatDecoder::ScanDSMCCStreams(void)
 *  \brief Check to see whether there is a Network Boot Ifo sub-descriptor in the PMT which
 *         requires the MHEG application to reboot.
 */
void AvFormatDecoder::ScanDSMCCStreams(void)
{
    if (!ic->cur_pmt_sect)
        return;

    if (!itv && ! (itv = GetNVP()->GetInteractiveTV()))
        return;

    const PESPacket pes = PESPacket::ViewData(ic->cur_pmt_sect);
    const PSIPTable psip(pes);
    const ProgramMapTable pmt(psip);

    for (uint i = 0; i < pmt.StreamCount(); i++)
    {
        if (! StreamID::IsObjectCarousel(pmt.StreamType(i)))
            continue;

        const desc_list_t desc_list = MPEGDescriptor::ParseOnlyInclude(
            pmt.StreamInfo(i), pmt.StreamInfoLength(i),
            DescriptorID::data_broadcast_id);

        for (uint j = 0; j < desc_list.size(); j++)
        {
            const unsigned char *desc = desc_list[j];
            desc++; // Skip tag
            uint length = *desc++;
            const unsigned char *endDesc = desc+length;
            uint dataBroadcastId = desc[0]<<8 | desc[1];
            if (dataBroadcastId != 0x0106) // ETSI/UK Profile
                continue;
            desc += 2; // Skip data ID
            while (desc != endDesc)
            {
                uint appTypeCode = desc[0]<<8 | desc[1];
                desc += 3; // Skip app type code and boot priority hint
                uint appSpecDataLen = *desc++;
                if (appTypeCode == 0x101) // UK MHEG profile
                {
                    const unsigned char *subDescEnd = desc + appSpecDataLen;
                    while (desc < subDescEnd)
                    {
                        uint sub_desc_tag = *desc++;
                        uint sub_desc_len = *desc++;
                        if (sub_desc_tag == 1) // Network boot info sub-descriptor.
                            itv->SetNetBootInfo(desc, sub_desc_len);
                        desc += sub_desc_len;
                    }
                }
                else desc += appSpecDataLen;
            }
        }
    }
}

int AvFormatDecoder::ScanStreams(bool novideo)
{
    int scanerror = 0;
    bitrate = 0;
    fps = 0;

    tracks[kTrackTypeAudio].clear();
    tracks[kTrackTypeSubtitle].clear();
    tracks[kTrackTypeTeletextCaptions].clear();
    selectedVideoIndex = -1;

    map<int,uint> lang_sub_cnt;
    map<int,uint> lang_aud_cnt;

    if (ringBuffer->isDVD() &&
        ringBuffer->DVD()->AudioStreamsChanged())
    {
        ringBuffer->DVD()->AudioStreamsChanged(false);
        RemoveAudioStreams();
    }

    for (uint i = 0; i < ic->nb_streams; i++)
    {
        AVCodecContext *enc = ic->streams[i]->codec;
        VERBOSE(VB_PLAYBACK, LOC +
                QString("Stream #%1, has id 0x%2 codec id %3, "
                        "type %4, bitrate %5 at 0x")
                .arg(i).arg((int)ic->streams[i]->id)
                .arg(codec_id_string(enc->codec_id))
                .arg(codec_type_string(enc->codec_type))
                .arg(enc->bit_rate)
                <<((void*)ic->streams[i]));

        switch (enc->codec_type)
        {
            case CODEC_TYPE_VIDEO:
            {
                //assert(enc->codec_id);
                if (!enc->codec_id)
                {
                    VERBOSE(VB_IMPORTANT,
                            LOC + QString("Stream #%1 has an unknown video "
                                          "codec id, skipping.").arg(i));
                    continue;
                }

                // HACK -- begin
                // ffmpeg is unable to compute H.264 bitrates in mpegts?
                if (CODEC_ID_H264 == enc->codec_id && enc->bit_rate == 0)
                    enc->bit_rate = 500000;
                // HACK -- end

                bitrate += enc->bit_rate;
                if (novideo)
                    break;

                d->DestroyMPEG2();
                h264_kf_seq->Reset();

                uint width  = max(enc->width, 16);
                uint height = max(enc->height, 16);
                VideoDisplayProfile vdp;
                vdp.SetInput(QSize(width, height));
                QString dec = vdp.GetDecoder();
                uint thread_count = vdp.GetMaxCPUs();
                VERBOSE(VB_PLAYBACK, QString("Using %1 CPUs for decoding")
                        .arg(ENABLE_THREADS ? thread_count : 1));

                if (ENABLE_THREADS && thread_count > 1)
                {
                    avcodec_thread_init(enc, thread_count);
                    enc->thread_count = thread_count;
                }

                bool handled = false;
#ifdef USING_XVMC
                if (!using_null_videoout && mpeg_version(enc->codec_id))
                {
                    // HACK -- begin
                    // Force MPEG2 decoder on MPEG1 streams.
                    // Needed for broken transmitters which mark
                    // MPEG2 streams as MPEG1 streams, and should
                    // be harmless for unbroken ones.
                    if (CODEC_ID_MPEG1VIDEO == enc->codec_id)
                        enc->codec_id = CODEC_ID_MPEG2VIDEO;
                    // HACK -- end

                    bool force_xv = false;
                    if (ringBuffer && ringBuffer->isDVD())
                    {
                        if (dec.left(4) == "xvmc")
                            dvd_xvmc_enabled = true;
                                
                        if (ringBuffer->InDVDMenuOrStillFrame() &&
                            dvd_xvmc_enabled)
                        {
                            force_xv = true;
                            enc->pix_fmt = PIX_FMT_YUV420P;
                        }
                    }

                    MythCodecID mcid;
                    mcid = VideoOutputXv::GetBestSupportedCodec(
                        /* disp dim     */ width, height,
                        /* osd dim      */ /*enc->width*/ 0, /*enc->height*/ 0,
                        /* mpeg type    */ mpeg_version(enc->codec_id),
                        /* xvmc pix fmt */ xvmc_pixel_format(enc->pix_fmt),
                        /* test surface */ kCodec_NORMAL_END > video_codec_id,
                        /* force_xv     */ force_xv);
                    bool vcd, idct, mc;
                    enc->codec_id = (CodecID)
                        myth2av_codecid(mcid, vcd, idct, mc);

                    if (ringBuffer && ringBuffer->isDVD() && 
                        (mcid == video_codec_id) &&
                        dvd_video_codec_changed)
                    {
                        dvd_video_codec_changed = false;
                        dvd_xvmc_enabled = false;
                    }

                    video_codec_id = mcid;
                    if (!force_xv && kCodec_NORMAL_END < mcid && kCodec_STD_XVMC_END > mcid)
                    {
                        enc->pix_fmt = (idct) ?
                            PIX_FMT_XVMC_MPEG2_IDCT : PIX_FMT_XVMC_MPEG2_MC;
                    }
                    handled = true;
                }
#elif USING_DVDV
                if (!using_null_videoout && mpeg_version(enc->codec_id))
                {
                    MythCodecID mcid;
                    mcid = VideoOutputQuartz::GetBestSupportedCodec(
                        /* disp dim     */ width, height,
                        /* osd dim      */ 0, 0,
                        /* mpeg type    */ mpeg_version(enc->codec_id),
                        /* pixel format */
                        (PIX_FMT_YUV420P == enc->pix_fmt) ? FOURCC_I420 : 0);

                    enc->codec_id = (CodecID) myth2av_codecid(mcid);
                    video_codec_id = mcid;

                    handled = true;
                }
#endif // USING_XVMC || USING_DVDV

                if (!handled)
                {
                    if (CODEC_ID_H264 == enc->codec_id)
                        video_codec_id = kCodec_H264;
                    else
                        video_codec_id = kCodec_MPEG2; // default to MPEG2
                }

                if (enc->codec)
                {
                    VERBOSE(VB_IMPORTANT, LOC
                            <<"Warning, video codec "<<enc<<" "
                            <<"id("<<codec_id_string(enc->codec_id)
                            <<") type ("<<codec_type_string(enc->codec_type)
                            <<") already open.");
                }

                // Initialize alternate decoders when needed...
                if (((dec == "libmpeg2") &&
                     (CODEC_ID_MPEG1VIDEO == enc->codec_id ||
                      CODEC_ID_MPEG2VIDEO == enc->codec_id)) ||
                    (CODEC_ID_MPEG2VIDEO_DVDV == enc->codec_id))
                {
                    d->InitMPEG2(dec);
                    
                    // fallback if we can't handle this resolution
                    if (!d->SetVideoSize(QSize(width, height)))
                    {
                        VERBOSE(VB_IMPORTANT, LOC_WARN +
                                "Failed to setup DVDV decoder, falling "
                                "back to software decoding");

                        enc->codec_id  = CODEC_ID_MPEG2VIDEO;
                        video_codec_id = kCodec_MPEG2;
                    }
                }

                enc->decode_cc_dvd  = decode_cc_dvd;

                // Set the default stream to the stream
                // that is found first in the PMT
                if (selectedVideoIndex < 0)
                {
                    selectedVideoIndex = i;
                }

                InitVideoCodec(ic->streams[i], enc,
                               selectedVideoIndex == (int) i);
                
                ScanATSCCaptionStreams(i);
                
                VERBOSE(VB_PLAYBACK, LOC + 
                        QString("Using %1 for video decoding")
                        .arg(GetCodecDecoderName()));

                break;
            }
            case CODEC_TYPE_AUDIO:
            {
                if (enc->codec)
                {
                    VERBOSE(VB_IMPORTANT, LOC
                            <<"Warning, audio codec "<<enc
                            <<" id("<<codec_id_string(enc->codec_id)
                            <<") type ("<<codec_type_string(enc->codec_type)
                            <<") already open, leaving it alone.");
                }
                //assert(enc->codec_id);
                VERBOSE(VB_GENERAL, LOC + QString("codec %1 has %2 channels")
                        .arg(codec_id_string(enc->codec_id))
                        .arg(enc->channels));

#if 0
                // HACK MULTICHANNEL DTS passthru disabled for multichannel,
                // dont know how to handle this
                // HACK BEGIN REALLY UGLY HACK FOR DTS PASSTHRU
                if (enc->codec_id == CODEC_ID_DTS)
                {
                    enc->sample_rate = 48000;
                    enc->channels = 2;
                    // enc->bit_rate = what??;
                }
                // HACK END REALLY UGLY HACK FOR DTS PASSTHRU
#endif

                bitrate += enc->bit_rate;
                break;
            }
            case CODEC_TYPE_SUBTITLE:
            {
                bitrate += enc->bit_rate;
                VERBOSE(VB_PLAYBACK, LOC + QString("subtitle codec (%1)")
                        .arg(codec_type_string(enc->codec_type)));
                break;
            }
            case CODEC_TYPE_DATA:
            {
                ScanTeletextCaptions(i);
                bitrate += enc->bit_rate;
                VERBOSE(VB_PLAYBACK, LOC + QString("data codec (%1)")
                        .arg(codec_type_string(enc->codec_type)));
                break;
            }
            default:
            {
                bitrate += enc->bit_rate;
                VERBOSE(VB_PLAYBACK, LOC + QString("Unknown codec type (%1)")
                        .arg(codec_type_string(enc->codec_type)));
                break;
            }
        }

        if (enc->codec_type != CODEC_TYPE_AUDIO && 
            enc->codec_type != CODEC_TYPE_VIDEO &&
            enc->codec_type != CODEC_TYPE_SUBTITLE)
            continue;

        VERBOSE(VB_PLAYBACK, LOC + QString("Looking for decoder for %1")
                .arg(codec_id_string(enc->codec_id)));
        AVCodec *codec = avcodec_find_decoder(enc->codec_id);
        if (!codec)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + 
                    QString("Could not find decoder for "
                            "codec (%1), ignoring.")
                    .arg(codec_id_string(enc->codec_id)));

            // Nigel's bogus codec-debug. Dump the list of codecs & decoders,
            // and have one last attempt to find a decoder. This is usually
            // only caused by build problems, where libavcodec needs a rebuild
            if (print_verbose_messages & VB_LIBAV)
            {
                AVCodec *p = first_avcodec;
                int      i = 1;
                while (p)
                {
                    if (p->name[0] != '\0')  printf("Codec %s:", p->name);
                    else                     printf("Codec %d, null name,", i);
                    if (p->decode == NULL)   puts("decoder is null");
          
                    if (p->id == enc->codec_id)
                    {   codec = p; break;    }

                    printf("Codec %d != %d\n", p->id, enc->codec_id);
                    p = p->next;
                    ++i;
                }
            }
            if (!codec)
                continue;
        }

        if (!enc->codec)
        {
            QMutexLocker locker(&avcodeclock);

            int open_val = avcodec_open(enc, codec);
            if (open_val < 0)
            {
                VERBOSE(VB_IMPORTANT, LOC_ERR
                        <<"Could not open codec "<<enc<<", "
                        <<"id("<<codec_id_string(enc->codec_id)<<") "
                        <<"type("<<codec_type_string(enc->codec_type)<<") "
                        <<"aborting. reason "<<open_val);
                //av_close_input_file(ic); // causes segfault
                ic = NULL;
                scanerror = -1;
                break;
            }
            else
            {
                VERBOSE(VB_GENERAL, LOC + "Opened codec "<<enc<<", "
                        <<"id("<<codec_id_string(enc->codec_id)<<") "
                        <<"type("<<codec_type_string(enc->codec_type)<<")");
            }
        }

        if (enc->codec_type == CODEC_TYPE_SUBTITLE)
        {
            int lang = get_canonical_lang(ic->streams[i]->language);
            int lang_indx = lang_aud_cnt[lang];
            lang_indx = lang_sub_cnt[lang];
            lang_sub_cnt[lang]++;

            tracks[kTrackTypeSubtitle].push_back(
                StreamInfo(i, lang, lang_indx, ic->streams[i]->id));

            VERBOSE(VB_PLAYBACK, LOC + QString(
                        "Subtitle track #%1 is A/V stream #%2 "
                        "and is in the %3 language(%4).")
                    .arg(tracks[kTrackTypeSubtitle].size()).arg(i)
                    .arg(iso639_key_toName(lang)).arg(lang));
        }

        if (enc->codec_type == CODEC_TYPE_AUDIO)
        {
            int lang = get_canonical_lang(ic->streams[i]->language);
            int lang_indx = lang_aud_cnt[lang];
            lang_aud_cnt[lang]++;

            if (ic->streams[i]->codec->avcodec_dual_language)
            {
                tracks[kTrackTypeAudio].push_back(
                    StreamInfo(i, lang, lang_indx, ic->streams[i]->id, 0));
                tracks[kTrackTypeAudio].push_back(
                    StreamInfo(i, lang, lang_indx, ic->streams[i]->id, 1));
            }
            else
            {
                tracks[kTrackTypeAudio].push_back(
                    StreamInfo(i, lang, lang_indx, ic->streams[i]->id));
            }

            VERBOSE(VB_AUDIO, LOC + QString(
                        "Audio Track #%1 is A/V stream #%2 "
                        "and has %3 channels in the %4 language(%5).")
                    .arg(tracks[kTrackTypeAudio].size()).arg(i)
                    .arg(enc->channels)
                    .arg(iso639_key_toName(lang)).arg(lang));
        }
    }

    if (bitrate > 0)
    {
        bitrate = (bitrate + 999) / 1000;
        if (ringBuffer)
            ringBuffer->UpdateRawBitrate(bitrate);
    }

    if (ringBuffer && ringBuffer->isDVD())
    {
        if (tracks[kTrackTypeAudio].size() > 1)
        {
            qBubbleSort(tracks[kTrackTypeAudio]);
            sinfo_vec_t::iterator it = tracks[kTrackTypeAudio].begin();
            for (; it != tracks[kTrackTypeAudio].end(); ++it)
            {
                it->dvd_track_num =
                        ringBuffer->DVD()->GetAudioTrackNum(it->stream_id);
                VERBOSE(VB_PLAYBACK, LOC + 
                            QString("DVD Audio Track Map "
                                    "Stream id #%1 track #%2 ")
                            .arg(it->stream_id).arg(it->dvd_track_num));
            }
            qBubbleSort(tracks[kTrackTypeAudio]);
            int trackNo = ringBuffer->DVD()->GetTrack(kTrackTypeAudio);
            if (trackNo >= (int)GetTrackCount(kTrackTypeAudio))
                trackNo = GetTrackCount(kTrackTypeAudio) - 1;
            SetTrack(kTrackTypeAudio, trackNo);
        }
        if (tracks[kTrackTypeSubtitle].size() > 0)
        {
            qBubbleSort(tracks[kTrackTypeSubtitle]);
            sinfo_vec_t::iterator it = tracks[kTrackTypeSubtitle].begin();
            for(; it != tracks[kTrackTypeSubtitle].end(); ++it)
            {
                it->dvd_track_num =
                        ringBuffer->DVD()->GetSubTrackNum(it->stream_id);
                VERBOSE(VB_PLAYBACK, LOC +
                        QString("DVD Subtitle Track Map "
                                "Stream id #%1 track #%2 ")
                        .arg(it->stream_id).arg(it->dvd_track_num));
            }
            qBubbleSort(tracks[kTrackTypeSubtitle]);
            int trackNo = ringBuffer->DVD()->GetTrack(kTrackTypeSubtitle);
            uint captionmode = GetNVP()->GetCaptionMode();
            int trackcount = (int)GetTrackCount(kTrackTypeSubtitle);
            if (captionmode == kDisplayAVSubtitle &&
                (trackNo < 0 || trackNo >= trackcount))
            {
                GetNVP()->SetCaptionsEnabled(false, false);
            }
            else if (trackNo >= 0 && trackNo < trackcount && 
                    !ringBuffer->InDVDMenuOrStillFrame())
            {
                    SetTrack(kTrackTypeSubtitle, trackNo);
                    GetNVP()->SetCaptionsEnabled(true, false);
            }
        }
    }

    // Select a new track at the next opportunity.
    ResetTracks();

    // We have to do this here to avoid the NVP getting stuck
    // waiting on audio.
    if (GetNVP()->HasAudioIn() && tracks[kTrackTypeAudio].empty())
    {
        GetNVP()->SetAudioParams(-1, -1, -1, false /* AC3/DTS pass-through */);
        GetNVP()->ReinitAudio();
        if (ringBuffer && ringBuffer->isDVD()) 
            audioIn = AudioInfo();
    }

    // if we don't have a video stream we still need to make sure some
    // video params are set properly
    if (selectedVideoIndex == -1)
    {
        QString tvformat = gContext->GetSetting("TVFormat").lower();
        if (tvformat == "ntsc" || tvformat == "ntsc-jp" ||
            tvformat == "pal-m" || tvformat == "atsc")
        {
            fps = 29.97;
            GetNVP()->SetVideoParams(-1, -1, 29.97, 1);
        }
        else
        {
            fps = 25.0;
            GetNVP()->SetVideoParams(-1, -1, 25.0, 1);
        }
    }

    if (GetNVP()->IsErrored())
        scanerror = -1;

    ScanDSMCCStreams();

    return scanerror;
}

/**
 *  \brief Reacts to DUAL/STEREO changes on the fly and fix streams.
 *
 *  This function should be called when a switch between dual and 
 *  stereo mpeg audio is detected. Such changes can and will happen at
 *  any time.
 *
 *  After this method returns, a new audio stream should be selected
 *  using AvFormatDecoder::autoSelectSubtitleTrack().
 *
 *  \param streamIndex av_stream_index of the stream that has changed
 */
void AvFormatDecoder::SetupAudioStreamSubIndexes(int streamIndex)
{
    QMutexLocker locker(&avcodeclock);

    // Find the position of the streaminfo in tracks[kTrackTypeAudio] 
    sinfo_vec_t::iterator current = tracks[kTrackTypeAudio].begin();
    for (; current != tracks[kTrackTypeAudio].end(); ++current) 
    {
        if (current->av_stream_index == streamIndex)
            break;
    }

    if (current == tracks[kTrackTypeAudio].end())
    {
        VERBOSE(VB_IMPORTANT, LOC_WARN +
                "Invalid stream index passed to "
                "SetupAudioStreamSubIndexes: "<<streamIndex);

        return;
    }

    // Remove the extra substream or duplicate the current substream
    sinfo_vec_t::iterator next = current + 1;
    if (current->av_substream_index == -1)
    {
        // Split stream in two (Language I + Language II)
        StreamInfo lang1 = *current;
        StreamInfo lang2 = *current;
        lang1.av_substream_index = 0;
        lang2.av_substream_index = 1;
        *current = lang1;
        tracks[kTrackTypeAudio].insert(next, lang2);
        return;
    }

    if ((next == tracks[kTrackTypeAudio].end()) ||
        (next->av_stream_index != streamIndex))
    {
        QString msg = QString(
            "Expected substream 1 (Language I) of stream %1\n\t\t\t"
            "following substream 0, found end of list or another stream.")
            .arg(streamIndex);

        VERBOSE(VB_IMPORTANT, LOC_WARN + msg);

        return;
    }

    // Remove extra stream info
    StreamInfo stream = *current;
    stream.av_substream_index = -1;
    *current = stream;
    tracks[kTrackTypeAudio].erase(next);
}

int get_avf_buffer(struct AVCodecContext *c, AVFrame *pic)
{
    AvFormatDecoder *nd = (AvFormatDecoder *)(c->opaque);

    VideoFrame *frame = nd->GetNVP()->GetNextVideoFrame(true);

    for (int i = 0; i < 3; i++)
    {
        pic->data[i]     = frame->buf + frame->offsets[i];
        pic->linesize[i] = frame->pitches[i];
    }

    pic->opaque = frame;
    pic->type = FF_BUFFER_TYPE_USER;

    pic->age = 256 * 256 * 256 * 64;

    return 1;
}

/** \brief remove audio streams from the context
 * used by dvd code during title transitions to remove
 * stale audio streams
 */
void AvFormatDecoder::RemoveAudioStreams()
{
    if (!GetNVP() || !GetNVP()->HasAudioIn())
        return;
 
    QMutexLocker locker(&avcodeclock);
    for (uint i = 0; i < ic->nb_streams;)
    {
        AVStream *st = ic->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_AUDIO)
        {
            av_remove_stream(ic, st->id, 0);
            i--;
        }
        else
            i++;
    }
    av_read_frame_flush(ic);
}

void release_avf_buffer(struct AVCodecContext *c, AVFrame *pic)
{
    (void)c;

    if (pic->type == FF_BUFFER_TYPE_INTERNAL)
    {
        avcodec_default_release_buffer(c, pic);
        return;
    }

    AvFormatDecoder *nd = (AvFormatDecoder *)(c->opaque);
    if (nd && nd->GetNVP() && nd->GetNVP()->getVideoOutput())
        nd->GetNVP()->getVideoOutput()->DeLimboFrame((VideoFrame*)pic->opaque);

    assert(pic->type == FF_BUFFER_TYPE_USER);

    for (uint i = 0; i < 4; i++)
        pic->data[i] = NULL;
}

int get_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic)
{
    AvFormatDecoder *nd = (AvFormatDecoder *)(c->opaque);
    VideoFrame *frame = nd->GetNVP()->GetNextVideoFrame(false);

    pic->data[0] = frame->priv[0];
    pic->data[1] = frame->priv[1];
    pic->data[2] = frame->buf;

    pic->linesize[0] = 0;
    pic->linesize[1] = 0;
    pic->linesize[2] = 0;

    pic->opaque = frame;
    pic->type = FF_BUFFER_TYPE_USER;

    pic->age = 256 * 256 * 256 * 64;

#ifdef USING_XVMC
    xvmc_render_state_t *render = (xvmc_render_state_t *)frame->buf;

    render->state = MP_XVMC_STATE_PREDICTION;
    render->picture_structure = 0;
    render->flags = 0;
    render->start_mv_blocks_num = 0;
    render->filled_mv_blocks_num = 0;
    render->next_free_data_block_num = 0;
#endif

    return 1;
}

void release_avf_buffer_xvmc(struct AVCodecContext *c, AVFrame *pic)
{
    assert(pic->type == FF_BUFFER_TYPE_USER);

#ifdef USING_XVMC
    xvmc_render_state_t *render = (xvmc_render_state_t *)pic->data[2];
    render->state &= ~MP_XVMC_STATE_PREDICTION;
#endif

    AvFormatDecoder *nd = (AvFormatDecoder *)(c->opaque);
    if (nd && nd->GetNVP() && nd->GetNVP()->getVideoOutput())
        nd->GetNVP()->getVideoOutput()->DeLimboFrame((VideoFrame*)pic->opaque);

    for (uint i = 0; i < 4; i++)
        pic->data[i] = NULL;

}

void render_slice_xvmc(struct AVCodecContext *s, const AVFrame *src,
                       int offset[4], int y, int type, int height)
{
    if (!src)
        return;

    (void)offset;
    (void)type;

    if (s && src && s->opaque && src->opaque)
    {
        AvFormatDecoder *nd = (AvFormatDecoder *)(s->opaque);

        int width = s->width;

        VideoFrame *frame = (VideoFrame *)src->opaque;
        nd->GetNVP()->DrawSlice(frame, 0, y, width, height);
    }
    else
    {
        VERBOSE(VB_IMPORTANT, LOC +
                "render_slice_xvmc called with bad avctx or src");
    }
}

void decode_cc_dvd(struct AVCodecContext *s, const uint8_t *buf, int buf_size)
{
    // taken from xine-lib libspucc by Christian Vogler

    AvFormatDecoder *nd = (AvFormatDecoder *)(s->opaque);
    unsigned long long utc = nd->lastccptsu;

    const uint8_t *current = buf;
    int curbytes = 0;
    uint8_t data1, data2;
    uint8_t cc_code;
    int odd_offset = 1;

    while (curbytes < buf_size)
    {
        int skip = 2;

        cc_code = *current++;
        curbytes++;
    
        if (buf_size - curbytes < 2)
            break;
    
        data1 = *current;
        data2 = *(current + 1);
    
        switch (cc_code)
        {
            case 0xfe:
                /* expect 2 byte encoding (perhaps CC3, CC4?) */
                /* ignore for time being */
                skip = 2;
                break;

            case 0xff:
            {
                /* expect EIA-608 CC1/CC2 encoding */
                int tc = utc / 1000;
                int data = (data2 << 8) | data1;
                if (cc608_good_parity(nd->cc608_parity_table, data))
                    nd->ccd608->FormatCCField(tc, 0, data);
                utc += 33367;
                skip = 5;
                break;
            }

            case 0x00:
                /* This seems to be just padding */
                skip = 2;
                break;

            case 0x01:
                odd_offset = data2 & 0x80;
                if (odd_offset)
                    skip = 2;
                else
                    skip = 5;
                break;

            default:
                // rest is not needed?
                goto done;
                //skip = 2;
                //break;
        }
        current += skip;
        curbytes += skip;

    }
  done:
    nd->lastccptsu = utc;
}

void AvFormatDecoder::DecodeDTVCC(const uint8_t *buf)
{
    // closed caption data
    //cc_data() {
    // reserved                1 0.0   1  
    // process_cc_data_flag    1 0.1   bslbf 
    bool process_cc_data = buf[0] & 0x40;
    if (!process_cc_data)
        return; // early exit if process_cc_data_flag false

    // additional_data_flag    1 0.2   bslbf 
    //bool additional_data = buf[0] & 0x20;
    // cc_count                5 0.3   uimsbf 
    uint cc_count = buf[0] & 0x1f;
    // reserved                8 1.0   0xff
    // em_data                 8 2.0

    for (uint cur = 0; cur < cc_count; cur++)
    {
        uint cc_code  = buf[2+(cur*3)];
        bool cc_valid = cc_code & 0x04;
        if (!cc_valid)
            continue;

        uint data1    = buf[3+(cur*3)];
        uint data2    = buf[4+(cur*3)];
        uint data     = (data2 << 8) | data1;
        uint cc_type  = cc_code & 0x03;

        if (cc_type <= 0x1) // EIA-608 field-1/2
        {
            if (cc608_good_parity(cc608_parity_table, data))
                ccd608->FormatCCField(lastccptsu / 1000, cc_type, data);
        }
        else // EIA-708 CC data
            ccd708->decode_cc_data(cc_type, data1, data2);
    }
}

void AvFormatDecoder::HandleGopStart(AVPacket *pkt)
{
    if (prevgoppos != 0 && keyframedist != 1)
    {
        int tempKeyFrameDist = framesRead - 1 - prevgoppos;
        bool reset_kfd = false;

        if (!gopset) // gopset: we've seen 2 keyframes
        {
            VERBOSE(VB_PLAYBACK, LOC + "HandleGopStart: "
                    "gopset not set, syncing positionMap");
            SyncPositionMap();
            if (tempKeyFrameDist > 0)
            {
                VERBOSE(VB_PLAYBACK, LOC + "HandleGopStart: " +
                        QString("Initial key frame distance: %1.")
                        .arg(keyframedist));
                gopset       = true;
                reset_kfd    = true;
            }
        }
        else if (keyframedist != tempKeyFrameDist && tempKeyFrameDist > 0)
        {
            VERBOSE(VB_PLAYBACK, LOC + "HandleGopStart: " +
                    QString("Key frame distance changed from %1 to %2.")
                    .arg(keyframedist).arg(tempKeyFrameDist));
            reset_kfd = true;
        }

        if (reset_kfd)
        {
            keyframedist    = tempKeyFrameDist;
            maxkeyframedist = max(keyframedist, maxkeyframedist);

            // FIXME: this needs to go
            bool is_ivtv    = (keyframedist == 15) || (keyframedist == 12);
            positionMapType = (is_ivtv) ? MARK_GOP_START : MARK_GOP_BYFRAME;

            GetNVP()->SetKeyframeDistance(keyframedist);

#if 0
            // also reset length
            if (!m_positionMap.empty())
            {
                long long index       = m_positionMap.back().index;
                long long totframes   = index * keyframedist;
                uint length = (uint)((totframes * 1.0f) / fps);
                GetNVP()->SetFileLength(length, totframes);
            }
#endif
        }
    }

    lastKey = prevgoppos = framesRead - 1;

    if (!hasFullPositionMap)
    {
        long long last_frame = 0;
        if (!m_positionMap.empty())
            last_frame = m_positionMap.back().index;
        if (keyframedist > 1)
            last_frame *= keyframedist;

        //cerr << "framesRead: " << framesRead << " last_frame: " << last_frame
        //    << " keyframedist: " << keyframedist << endl;

        // if we don't have an entry, fill it in with what we've just parsed
        if (framesRead > last_frame && keyframedist > 0)
        {
            long long startpos = pkt->pos;

            VERBOSE(VB_PLAYBACK|VB_TIMESTAMP, LOC + 
                    QString("positionMap[ %1 ] == %2.")
                    .arg(prevgoppos / keyframedist)
                    .arg(startpos));

            PosMapEntry entry = {prevgoppos / keyframedist,
                                 prevgoppos, startpos};
            m_positionMap.push_back(entry);
        }

#if 0
        // If we are > 150 frames in and saw no positionmap at all, reset
        // length based on the actual bitrate seen so far
        if (framesRead > 150 && !recordingHasPositionMap && !livetv)
        {
            bitrate = (int)((pkt->pos * 8 * fps) / (framesRead - 1));
            float bytespersec = (float)bitrate / 8;
            float secs = ringBuffer->GetRealFileSize() * 1.0 / bytespersec;
            GetNVP()->SetFileLength((int)(secs), (int)(secs * fps));
        }
#endif
    }
}

#define SEQ_START     0x000001b3
#define GOP_START     0x000001b8
#define PICTURE_START 0x00000100
#define SLICE_MIN     0x00000101
#define SLICE_MAX     0x000001af
#define SEQ_END_CODE  0x000001b7

void AvFormatDecoder::MpegPreProcessPkt(AVStream *stream, AVPacket *pkt)
{
    AVCodecContext *context = stream->codec;
    const uint8_t *bufptr = pkt->data;
    const uint8_t *bufend = pkt->data + pkt->size;

    while (bufptr < bufend)
    {
        bufptr = ff_find_start_code(bufptr, bufend, &start_code_state);
       
        if (ringBuffer->isDVD() && start_code_state == SEQ_END_CODE)
        {
            mpeg_seq_end_seen = true;
            return;
        }

        if (start_code_state >= SLICE_MIN && start_code_state <= SLICE_MAX)
            continue;
        else if (SEQ_START == start_code_state)
        {
            if (bufptr + 11 >= pkt->data + pkt->size)
                continue; // not enough valid data...
            SequenceHeader *seq = reinterpret_cast<SequenceHeader*>(
                const_cast<uint8_t*>(bufptr));

            uint  width  = seq->width();
            uint  height = seq->height();
            float aspect = seq->aspect(context->sub_id == 1);
            float seqFPS = seq->fps();

            bool changed = (seqFPS > fps+0.01) || (seqFPS < fps-0.01);
            changed |= (width  != (uint)current_width );
            changed |= (height != (uint)current_height);
            changed |= fabs(aspect - current_aspect) > eps;

            if (changed)
            {
                GetNVP()->SetVideoParams(width, height, seqFPS,
                                         keyframedist, aspect, 
                                         kScan_Detect);
                
                current_width  = width;
                current_height = height;
                current_aspect = aspect;
                fps            = seqFPS;

                d->ResetMPEG2();

                gopset = false;
                prevgoppos = 0;
                lastapts = lastvpts = lastccptsu = 0;

                // fps debugging info
                float avFPS = normalized_fps(stream, context);
                if ((seqFPS > avFPS+0.01) || (seqFPS < avFPS-0.01))
                {
                    VERBOSE(VB_PLAYBACK, LOC +
                            QString("avFPS(%1) != seqFPS(%2)")
                            .arg(avFPS).arg(seqFPS));
                }
            }

            seq_count++;

            if (!seen_gop && seq_count > 1)
            {
                HandleGopStart(pkt);
                pkt->flags |= PKT_FLAG_KEY;
            }
        }
        else if (GOP_START == start_code_state)
        {
            HandleGopStart(pkt);
            seen_gop = true;
            pkt->flags |= PKT_FLAG_KEY;
        }
    }
}

void AvFormatDecoder::H264PreProcessPkt(AVStream *stream, AVPacket *pkt)
{
    AVCodecContext *context = stream->codec;
    const uint8_t  *buf     = pkt->data;
    const uint8_t  *buf_end = pkt->data + pkt->size;

    while (buf < buf_end)
    {
        uint32_t bytes_used = h264_kf_seq->AddBytes(buf, buf_end - buf, 0);
        buf += bytes_used;

        if (!h264_kf_seq->HasStateChanged() || !h264_kf_seq->IsOnKeyframe())
            continue;

        float aspect_ratio;
        if (context->sample_aspect_ratio.num == 0)
            aspect_ratio = 0.0f;
        else
            aspect_ratio = av_q2d(context->sample_aspect_ratio) *
                context->width / context->height;

        if (aspect_ratio <= 0.0f || aspect_ratio > 6.0f)
            aspect_ratio = (float)context->width / context->height;

        uint  width  = context->width;
        uint  height = context->height;
        float seqFPS = normalized_fps(stream, context);

        bool changed = (seqFPS > fps+0.01) || (seqFPS < fps-0.01);
        changed |= (width  != (uint)current_width );
        changed |= (height != (uint)current_height);
        changed |= fabs(aspect_ratio - current_aspect) > eps;

        if (changed)
        {
            GetNVP()->SetVideoParams(width, height, seqFPS,
                                     keyframedist, aspect_ratio,
                                     kScan_Detect);

            current_width  = width;
            current_height = height;
            current_aspect = aspect_ratio;
            fps            = seqFPS;

            gopset = false;
            prevgoppos = 0;
            lastapts = lastvpts = lastccptsu = 0;

            // fps debugging info
            float avFPS = normalized_fps(stream, context);
            if ((seqFPS > avFPS+0.01) || (seqFPS < avFPS-0.01))
            {
                VERBOSE(VB_PLAYBACK, LOC +
                        QString("avFPS(%1) != seqFPS(%2)")
                        .arg(avFPS).arg(seqFPS));
            }
        }

        HandleGopStart(pkt);
        pkt->flags |= PKT_FLAG_KEY;
    }
}

/** \fn AvFormatDecoder::ProcessVBIDataPacket(const AVStream*, const AVPacket*)
 *  \brief Process ivtv proprietary embedded vertical blanking
 *         interval captions.
 *  \sa CC608Decoder, TeletextDecoder
 */
void AvFormatDecoder::ProcessVBIDataPacket(
    const AVStream *stream, const AVPacket *pkt)
{
    (void) stream;

    const uint8_t *buf     = pkt->data;
    uint64_t linemask      = 0;
    unsigned long long utc = lastccptsu;

    // [i]tv0 means there is a linemask
    // [I]TV0 means there is no linemask and all lines are present
    if ((buf[0]=='t') && (buf[1]=='v') && (buf[2] == '0'))
    {
        /// TODO this is almost certainly not endian safe....
        memcpy(&linemask, buf + 3, 8);
        buf += 11;
    }
    else if ((buf[0]=='T') && (buf[1]=='V') && (buf[2] == '0'))
    {
        linemask = 0xffffffffffffffffLL;
        buf += 3;
    }
    else
    {
        VERBOSE(VB_VBI, LOC + QString("Unknown VBI data stream '%1%2%3'")
                .arg(QChar(buf[0])).arg(QChar(buf[1])).arg(QChar(buf[2])));
        return;
    }

    static const uint min_blank = 6;
    for (uint i = 0; i < 36; i++)
    {
        if (!((linemask >> i) & 0x1))
            continue;

        const uint line  = ((i < 18) ? i : i-18) + min_blank;
        const uint field = (i<18) ? 0 : 1; 
        const uint id2 = *buf & 0xf;
        switch (id2)
        {
            case VBI_TYPE_TELETEXT:
                // SECAM lines  6-23 
                // PAL   lines  6-22
                // NTSC  lines 10-21 (rare)
                ttd->Decode(buf+1, VBI_IVTV);
                break;
            case VBI_TYPE_CC:
                // PAL   line 22 (rare)
                // NTSC  line 21
                if (21 == line)
                {
                    int data = (buf[2] << 8) | buf[1];
                    if (cc608_good_parity(cc608_parity_table, data))
                        ccd608->FormatCCField(utc/1000, field, data);
                    utc += 33367;
                }
                break;
            case VBI_TYPE_VPS: // Video Programming System
                // PAL   line 16
                ccd608->DecodeVPS(buf+1); // a.k.a. PDC
                break;
            case VBI_TYPE_WSS: // Wide Screen Signal
                // PAL   line 23
                // NTSC  line 20
                ccd608->DecodeWSS(buf+1);
                break;
        }
        buf += 43;
    }
    lastccptsu = utc;
}

/** \fn AvFormatDecoder::ProcessDVBDataPacket(const AVStream*, const AVPacket*)
 *  \brief Process DVB Teletext.
 *  \sa TeletextDecoder
 */
void AvFormatDecoder::ProcessDVBDataPacket(
    const AVStream*, const AVPacket *pkt)
{
    const uint8_t *buf     = pkt->data;
    const uint8_t *buf_end = pkt->data + pkt->size;


    while (buf < buf_end)
    {
        if (*buf == 0x10)
            buf++; // skip

        if (*buf == 0x02)
        {
            buf += 3;
            ttd->Decode(buf+1, VBI_DVB);
        }
        else if (*buf == 0x03)
        {
            buf += 3;
            ttd->Decode(buf+1, VBI_DVB_SUBTITLE);
        }
        else if (*buf == 0xff)
        {
            buf += 3;
        }
        else
        {
            VERBOSE(VB_VBI, QString("VBI: Unknown descriptor: %1").arg(*buf));
        }

        buf += 43;
    }
}

/** \fn AvFormatDecoder::ProcessDSMCCPacket(const AVStream*, const AVPacket*)
 *  \brief Process DSMCC object carousel packet.
 */
void AvFormatDecoder::ProcessDSMCCPacket(
    const AVStream *str, const AVPacket *pkt)
{
    if (!itv && ! (itv = GetNVP()->GetInteractiveTV()))
        return;

    // The packet may contain several tables.
    uint8_t *data = pkt->data;
    int length = pkt->size;
    avcodeclock.lock();
    int componentTag = str->component_tag; //Contains component tag
    unsigned carouselId = (unsigned)str->codec->sub_id; //Contains carousel Id
    int dataBroadcastId = str->codec->flags; // Contains data broadcast Id.
    avcodeclock.unlock();
    while (length > 3)
    {
        uint16_t sectionLen = (((data[1] & 0xF) << 8) | data[2]) + 3;

        if (sectionLen > length) // This may well be filler
            return;

        itv->ProcessDSMCCSection(data, sectionLen,
                                 componentTag, carouselId,
                                 dataBroadcastId);
        length -= sectionLen;
        data += sectionLen;
    }
}

int AvFormatDecoder::SetTrack(uint type, int trackNo)
{
    bool ret = DecoderBase::SetTrack(type, trackNo);

    if (kTrackTypeAudio == type)
    {
        QString msg = SetupAudioStream() ? "" : "not ";
        VERBOSE(VB_AUDIO, LOC + "Audio stream type "+msg+"changed.");
    }

    return ret;
}

QString AvFormatDecoder::GetTrackDesc(uint type, uint trackNo) const
{
    if (trackNo >= tracks[type].size())
        return "";

    int lang_key = tracks[type][trackNo].language;
    if (kTrackTypeAudio == type)
    {
        if (ringBuffer->isDVD())
            lang_key = ringBuffer->DVD()->GetAudioLanguage(trackNo);

        QString msg = iso639_key_toName(lang_key);

        int av_index = tracks[kTrackTypeAudio][trackNo].av_stream_index;
        AVStream *s = ic->streams[av_index];

        if (!s)
            return QString("%1: %2").arg(trackNo + 1).arg(msg); 

        if (s->codec->codec_id == CODEC_ID_MP3)
            msg += QString(" MP%1").arg(s->codec->sub_id);
        else if (s->codec->codec)
            msg += QString(" %1").arg(s->codec->codec->name).upper();

        int channels = 0;
        if (ringBuffer->isDVD())
            channels = ringBuffer->DVD()->GetNumAudioChannels(trackNo);
        else if (s->codec->channels)
            channels = s->codec->channels;

        if (channels == 0)
            msg += QString(" ?ch");
        else if((channels > 4) && !(channels & 1))
            msg += QString(" %1.1ch").arg(channels - 1);
        else
            msg += QString(" %1ch").arg(channels);

        return QString("%1: %2").arg(trackNo + 1).arg(msg);
    }
    else if (kTrackTypeSubtitle == type)
    {
        if (ringBuffer->isDVD())
            lang_key = ringBuffer->DVD()->GetSubtitleLanguage(trackNo);

        return QObject::tr("Subtitle") + QString(" %1: %2")
            .arg(trackNo + 1).arg(iso639_key_toName(lang_key));
    }
    else
    {
        return DecoderBase::GetTrackDesc(type, trackNo);
    }
}

int AvFormatDecoder::GetTeletextDecoderType(void) const
{
    return ttd->GetDecoderType();
}

void AvFormatDecoder::SetTeletextDecoderViewer(TeletextViewer *view)
{
    ttd->SetViewer(view);
}

QString AvFormatDecoder::GetXDS(const QString &key) const
{
    return ccd608->GetXDS(key);
}

bool AvFormatDecoder::SetAudioByComponentTag(int tag)
{
    for (uint i = 0; i < tracks[kTrackTypeAudio].size(); i++)
    {
        AVStream *s  = ic->streams[tracks[kTrackTypeAudio][i].av_stream_index];
        if (s)
        {
            if (s->component_tag == tag || tag <= 0 && s->component_tag <= 0)
            {
                return SetTrack(kTrackTypeAudio, i);
            }
        }
    }
    return false;
}

bool AvFormatDecoder::SetVideoByComponentTag(int tag)
{
    for (uint i = 0; i < ic->nb_streams; i++)
    {
        AVStream *s  = ic->streams[i];
        if (s)
        {
            if (s->component_tag == tag)
            {
                selectedVideoIndex = i;
                return true;
            }
        }
    }
    return false;
}

// documented in decoderbase.cpp
int AvFormatDecoder::AutoSelectTrack(uint type)
{
    if (kTrackTypeAudio == type)
        return AutoSelectAudioTrack();

    if (ringBuffer->InDVDMenuOrStillFrame())
        return -1;

    return DecoderBase::AutoSelectTrack(type);
}

static vector<int> filter_lang(const sinfo_vec_t &tracks, int lang_key)
{
    vector<int> ret;

    for (uint i = 0; i < tracks.size(); i++)
        if ((lang_key < 0) || tracks[i].language == lang_key)
            ret.push_back(i);

    return ret;
}

static int filter_max_ch(const AVFormatContext *ic,
                         const sinfo_vec_t     &tracks,
                         const vector<int>     &fs,
                         enum CodecID           codecId = CODEC_ID_NONE)
{
    int selectedTrack = -1, max_seen = -1;

    vector<int>::const_iterator it = fs.begin();
    for (; it != fs.end(); ++it)
    {
        const int stream_index = tracks[*it].av_stream_index;
        const AVCodecContext *ctx = ic->streams[stream_index]->codec;
        if ((codecId == CODEC_ID_NONE || codecId == ctx->codec_id) &&
            (max_seen < ctx->channels))
        {
            selectedTrack = *it;
            max_seen = ctx->channels;
        }
    }

    return selectedTrack;
}

/** \fn AvFormatDecoder::AutoSelectAudioTrack(void)
 *  \brief Selects the best audio track.
 *
 *   It is primarily needed for DVB recordings
 *
 *   This function will select the best audio track available
 *   using the following criteria, in order of decreasing
 *   preference:
 *
 *   1) The stream last selected by the user, which is
 *      recalled as the Nth stream in the preferred language
 *      or the Nth substream when audio is in dual language
 *      format (each channel contains a different language track)
 *      If it can not be located we attempt to find a stream
 *      in the same language.
 *
 *   2) If we can not reselect the last user selected stream,
 *      then for each preferred language from most preferred
 *      to least preferred, we try to find a new stream based
 *      on the algorithm below.
 *
 *   3) If we can not select a stream in a preferred language
 *      we try to select a stream irrespective of language
 *      based on the algorithm below.
 *
 *   When searching for a new stream (ie. options 2 and 3
 *   above), the following search is carried out in order:
 *
 *   i)   If DTS passthrough is enabled then the DTS track with
 *        the greatest number of audio channels is selected
 *        (the first will be chosen if there are several the
 *        same). If DTS passthrough is not enabled this step
 *        will be skipped because internal DTS decoding is not
 *        currently supported.
 *
 *   ii)  If no DTS track is chosen, the AC3 track with the
 *        greatest number of audio channels is selected (the
 *        first will be chosen if there are several the same).
 *        Internal decoding of AC3 is supported, so this will
 *        be used irrespective of whether AC3 passthrough is
 *        enabled.
 *
 *   iii) Lastly the track with the greatest number of audio
 *        channels irrespective of type will be selected.
 *  \return track if a track was selected, -1 otherwise
 */
int AvFormatDecoder::AutoSelectAudioTrack(void)
{
    const sinfo_vec_t &atracks = tracks[kTrackTypeAudio];
    StreamInfo        &wtrack  = wantedTrack[kTrackTypeAudio];
    StreamInfo        &strack  = selectedTrack[kTrackTypeAudio];
    int               &ctrack  = currentTrack[kTrackTypeAudio];

    uint numStreams = atracks.size();
    if ((ctrack >= 0) && (ctrack < (int)numStreams))
        return ctrack; // audio already selected

#if 0
    // enable this to print streams
    for (uint i = 0; i < atracks.size(); i++)
    {
        int idx = atracks[i].av_stream_index;
        AVCodecContext *codec_ctx = ic->streams[idx]->codec;
        bool do_ac3_passthru = (allow_ac3_passthru && !transcoding &&
                                !disable_passthru &&
                                (codec_ctx->codec_id == CODEC_ID_AC3));
        bool do_dts_passthru = (allow_dts_passthru && !transcoding &&
                                !disable_passthru &&
                                (codec_ctx->codec_id == CODEC_ID_DTS));
        AudioInfo item(codec_ctx->codec_id,
                       codec_ctx->sample_rate, codec_ctx->channels,
                       do_ac3_passthru || do_dts_passthru);
        VERBOSE(VB_AUDIO, LOC + " * " + item.toString());
    }
#endif

    int selTrack = (1 == numStreams) ? 0 : -1;
    int wlang    = wtrack.language;

    if ((selTrack < 0) && (wtrack.av_substream_index >= 0))
    {
        VERBOSE(VB_AUDIO, LOC + "Trying to reselect audio sub-stream");
        // Dual stream without language information: choose
        // the previous substream that was kept in wtrack,
        // ignoring the stream index (which might have changed). 
        int substream_index = wtrack.av_substream_index;

        for (uint i = 0; i < numStreams; i++)
        {
            if (atracks[i].av_substream_index == substream_index)
            {
                selTrack = i;
                break;
            }
        }
    }

    if ((selTrack < 0) && wlang >= -1 && numStreams)
    {
        VERBOSE(VB_AUDIO, LOC + "Trying to reselect audio track");
        // Try to reselect user selected subtitle stream.
        // This should find the stream after a commercial
        // break and in some cases after a channel change.
        uint windx = wtrack.language_index;
        for (uint i = 0; i < numStreams; i++)
        {
            if (wlang == atracks[i].language)
                selTrack = i;

            if (windx == atracks[i].language_index)
                break;
        }
    }

    if (selTrack < 0 && numStreams)
    {
        VERBOSE(VB_AUDIO, LOC + "Trying to select audio track (w/lang)");
        // try to get best track for most preferred language
        selTrack = -1;
        vector<int>::const_iterator it = languagePreference.begin();
        for (; it !=  languagePreference.end() && selTrack<0; ++it)
        {
            vector<int> flang = filter_lang(atracks, *it);

            if (allow_dts_passthru && !transcoding)
                selTrack = filter_max_ch(ic, atracks, flang, CODEC_ID_DTS);

            if (selTrack < 0)
                selTrack = filter_max_ch(ic, atracks, flang, CODEC_ID_AC3);

            if (selTrack < 0)
                selTrack = filter_max_ch(ic, atracks, flang);
        }
        // try to get best track for any language
        if (selTrack < 0)
        {
            VERBOSE(VB_AUDIO, LOC + "Trying to select audio track (wo/lang)");
            vector<int> flang = filter_lang(atracks, -1);

            if (allow_dts_passthru && !transcoding)
                selTrack = filter_max_ch(ic, atracks, flang, CODEC_ID_DTS);

            if (selTrack < 0)
                selTrack = filter_max_ch(ic, atracks, flang, CODEC_ID_AC3);

            if (selTrack < 0)
                selTrack = filter_max_ch(ic, atracks, flang);
        }
    }

    if (selTrack < 0)
    {
        strack.av_stream_index = -1;
        if (ctrack != selTrack)
        {
            VERBOSE(VB_AUDIO, LOC + "No suitable audio track exists.");
            ctrack = selTrack;
        }
    }
    else
    {
        ctrack = selTrack;
        strack = atracks[selTrack];

        if (wtrack.av_stream_index < 0)
            wtrack = strack;

        VERBOSE(VB_AUDIO, LOC +
                QString("Selected track %1 (A/V Stream #%2)")
                .arg(GetTrackDesc(kTrackTypeAudio, ctrack))
                .arg(strack.av_stream_index));
    }

    SetupAudioStream();
    return selTrack;
}

static void extract_mono_channel(uint channel, AudioInfo *audioInfo,
                                 char *buffer, int bufsize)
{
    // Only stereo -> mono (left or right) is supported
    if (audioInfo->channels != 2)
        return;

    if (channel >= (uint)audioInfo->channels)
        return;

    const uint samplesize = audioInfo->sample_size;
    const uint samples    = bufsize / samplesize;
    const uint halfsample = samplesize >> 1;

    const char *from = (channel == 1) ? buffer + halfsample : buffer;
    char *to         = (channel == 0) ? buffer + halfsample : buffer;

    for (uint sample = 0; sample < samples;
         (sample++), (from += samplesize), (to += samplesize))
    {
        memmove(to, from, halfsample);
    }
}

// documented in decoderbase.h
bool AvFormatDecoder::GetFrame(int onlyvideo)
{
    AVPacket *pkt = NULL;
    int len;
    unsigned char *ptr;
    int data_size = 0;
    long long pts;
    bool firstloop = false, have_err = false;

    gotvideo = false;

    frame_decoded = 0;
    decoded_video_frame = NULL;

    bool allowedquit = false;
    bool storevideoframes = false;

    avcodeclock.lock();
    AutoSelectTracks();
    avcodeclock.unlock();

    bool skipaudio = (lastvpts == 0);

    bool has_video = HasVideo(ic);

    if (!has_video && (onlyvideo >= 0))
    {
        gotvideo = GenerateDummyVideoFrame();
        onlyvideo = -1;
        skipaudio = false;
    }

    uint ofill = 0, ototal = 0, othresh = 0, total_decoded_audio = 0;
    if (GetNVP()->GetAudioBufferStatus(ofill, ototal))
    {
        othresh =  ((ototal>>1) + (ototal>>2));
        allowedquit = (onlyvideo < 0) && (ofill > othresh);
    }

    while (!allowedquit)
    {
        if ((onlyvideo == 0) &&
            ((currentTrack[kTrackTypeAudio] < 0) ||
             (selectedTrack[kTrackTypeAudio].av_stream_index < 0)))
        {
            // disable audio request if there are no audio streams anymore
            // and we have video, otherwise allow decoding to stop
            if (has_video)
                onlyvideo = 1;
            else
                allowedquit = true;
        }

        if (ringBuffer->isDVD())
        {
            int dvdtitle  = 0;
            int dvdpart = 0;
            ringBuffer->DVD()->GetPartAndTitle(dvdpart, dvdtitle);
            bool cellChanged = ringBuffer->DVD()->CellChanged();
            bool inDVDStill = ringBuffer->DVD()->InStillFrame();
            bool inDVDMenu  = ringBuffer->DVD()->IsInMenu();
            selectedVideoIndex = 0;
            if (dvdTitleChanged)
            {
                if ((storedPackets.count() > 10 && !decodeStillFrame) ||
                    decodeStillFrame)
                {
                    storevideoframes = false;
                    dvdTitleChanged = false;
                    ScanStreams(true);
                }
                else
                    storevideoframes = true;
            }
            else
            {
                storevideoframes = false;
                
                if (decodeStillFrame && !inDVDStill)
                    decodeStillFrame = false;
                
                if (storedPackets.count() < 2 && !decodeStillFrame)
                    storevideoframes = true;

                if (inDVDMenu && storedPackets.count() > 0)
                    ringBuffer->DVD()->SetRunSeekCellStart(false);
                else if (inDVDStill)
                    ringBuffer->DVD()->RunSeekCellStart();
            }          
            if (GetNVP()->AtNormalSpeed() &&
                ((cellChanged) || (lastdvdtitle != dvdtitle)))
            {
                if (dvdtitle != lastdvdtitle)
                {
                    VERBOSE(VB_PLAYBACK, LOC + "DVD Title Changed");
                    lastdvdtitle = dvdtitle;
                    if (lastdvdtitle != -1 )
                        dvdTitleChanged = true;
                    if (GetNVP() && GetNVP()->getVideoOutput())
                    {
                        if (ringBuffer->DVD()->InStillFrame())
                            GetNVP()->getVideoOutput()->SetPrebuffering(false);
                        else
                            GetNVP()->getVideoOutput()->SetPrebuffering(true);
                    }
                }
                
                if (ringBuffer->DVD()->PGCLengthChanged())
                {
                    posmapStarted = false;
                    m_positionMap.clear();
                    SyncPositionMap();
                }

                UpdateDVDFramesPlayed();
                VERBOSE(VB_PLAYBACK, QString(LOC + "DVD Cell Changed. "
                                             "Update framesPlayed: %1 ")
                                             .arg(framesPlayed));
            }
        }

        if (gotvideo)
        {
            if (lowbuffers && onlyvideo == 0 &&
                storedPackets.count() < 75 &&
                lastapts < lastvpts + 100 &&
                !ringBuffer->InDVDMenuOrStillFrame())
            {
                storevideoframes = true;
            }
            else if (onlyvideo >= 0)
            {
                if (storedPackets.count() >=75)
                    VERBOSE(VB_IMPORTANT,
                            QString("Audio %1 ms behind video but already %2 "
                               "video frames queued. AV-Sync might be broken.")
                            .arg(lastvpts-lastapts).arg(storedPackets.count()));
                allowedquit = true;
                continue;
            }
        }

        if (!storevideoframes && storedPackets.count() > 0)
        {
            if (pkt)
            {
                av_free_packet(pkt);
                delete pkt;
            }
            pkt = storedPackets.first();
            storedPackets.removeFirst();
        }
        else
        {
            if (!pkt)
            {
                pkt = new AVPacket;
                bzero(pkt, sizeof(AVPacket));
                av_init_packet(pkt);
            }

            if (!ic || (av_read_frame(ic, pkt) < 0))
            {
                ateof = true;
                GetNVP()->SetEof();
                if (pkt)
                    delete pkt;
                return false;
            }

            if (waitingForChange && pkt->pos >= readAdjust)
                FileChanged();

            if (pkt->pos > readAdjust)
                pkt->pos -= readAdjust;
        }

        if (pkt->stream_index > (int) ic->nb_streams)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Bad stream");
            av_free_packet(pkt);
            continue;
        }

        len = pkt->size;
        ptr = pkt->data;
        pts = 0;

        AVStream *curstream = ic->streams[pkt->stream_index];

        if (pkt->dts != (int64_t)AV_NOPTS_VALUE)
            pts = (long long)(av_q2d(curstream->time_base) * pkt->dts * 1000);

        if (ringBuffer->isDVD() && 
            curstream->codec->codec_type == CODEC_TYPE_VIDEO)
        {
            MpegPreProcessPkt(curstream, pkt);

            if (mpeg_seq_end_seen && storevideoframes)
            {
                ringBuffer->DVD()->InStillFrame(true);
            }

            bool inDVDStill = ringBuffer->DVD()->InStillFrame();

            if (!decodeStillFrame && inDVDStill)
            {
                decodeStillFrame = true;
                gContext->RestoreScreensaver();
                d->ResetMPEG2();
            }
            
            if (mpeg_seq_end_seen)
            {
                mpeg_seq_end_seen = false;
                av_free_packet(pkt);
                pkt = NULL;
                continue;
            }

            if (!d->HasMPEG2Dec())
            {
                int current_width = curstream->codec->width;
                int video_width = GetNVP()->GetVideoSize().width();
                if (dvd_xvmc_enabled && GetNVP() && GetNVP()->getVideoOutput())
                {
                    bool dvd_xvmc_active = false;
                    if (video_codec_id > kCodec_NORMAL_END &&
                        video_codec_id < kCodec_VLD_END)
                    {
                        dvd_xvmc_active = true;
                    }

                    bool indvdmenu   = ringBuffer->InDVDMenuOrStillFrame();
                    if ((indvdmenu && dvd_xvmc_active) ||
                        ((!indvdmenu && !dvd_xvmc_active)))
                    {
                        VERBOSE(VB_PLAYBACK, LOC + QString("DVD Codec Change "
                                    "indvdmenu %1 dvd_xvmc_active %2")
                                .arg(indvdmenu).arg(dvd_xvmc_active));
                        dvd_video_codec_changed = true;
                    }
                }
                
                if ((video_width > 0 && video_width != current_width) ||
                    dvd_video_codec_changed)
                {
                    VERBOSE(VB_PLAYBACK, LOC + QString("DVD Stream/Codec Change "
                                "video_width %1 current_width %2 "
                                "dvd_video_codec_changed %3")
                            .arg(video_width).arg(current_width)
                            .arg(dvd_video_codec_changed));
                    av_free_packet(pkt);
                    CloseCodecs();
                    ScanStreams(false);
                    allowedquit = true;
                    dvd_video_codec_changed = false;
                    continue;
                }
            }
        }

        if (storevideoframes &&
            curstream->codec->codec_type == CODEC_TYPE_VIDEO)
        {
            av_dup_packet(pkt);
            storedPackets.append(pkt);
            pkt = NULL;
            continue;
        }

        if (len > 0 && curstream->codec->codec_type == CODEC_TYPE_VIDEO &&
            pkt->stream_index == selectedVideoIndex)
        {
            AVCodecContext *context = curstream->codec;

            if (context->codec_id == CODEC_ID_MPEG1VIDEO ||
                context->codec_id == CODEC_ID_MPEG2VIDEO ||
                context->codec_id == CODEC_ID_MPEG2VIDEO_XVMC ||
                context->codec_id == CODEC_ID_MPEG2VIDEO_XVMC_VLD)
            {
                if (!ringBuffer->isDVD())
                    MpegPreProcessPkt(curstream, pkt);
            }
            else if (context->codec_id == CODEC_ID_H264)
            {
                H264PreProcessPkt(curstream, pkt);
            }
            else
            {
                if (pkt->flags & PKT_FLAG_KEY)
                {
                    HandleGopStart(pkt);
                    seen_gop = true;
                }
                else
                {
                    seq_count++;
                    if (!seen_gop && seq_count > 1)
                    {
                        HandleGopStart(pkt);
                    }
                }
            }

            if (framesRead == 0 && !justAfterChange &&
                !(pkt->flags & PKT_FLAG_KEY))
            {
                av_free_packet(pkt);
                continue;
            }

            framesRead++;
            justAfterChange = false;

            if (exitafterdecoded)
                gotvideo = 1;

            // If the resolution changed in XXXPreProcessPkt, we may
            // have a fatal error, so check for this before continuing.
            if (GetNVP()->IsErrored())
            {
                av_free_packet(pkt);
                if (pkt)
                    delete pkt;
                return false;
            }
        }

        if (len > 0 &&
            curstream->codec->codec_type == CODEC_TYPE_DATA &&
            curstream->codec->codec_id   == CODEC_ID_MPEG2VBI)
        {
            ProcessVBIDataPacket(curstream, pkt);

            av_free_packet(pkt);
            continue;
        }

        if (len > 0 &&
            curstream->codec->codec_type == CODEC_TYPE_DATA &&
            curstream->codec->codec_id   == CODEC_ID_DVB_VBI)
        {
            ProcessDVBDataPacket(curstream, pkt);

            av_free_packet(pkt);
            continue;
        }

        if (len > 0 &&
            curstream->codec->codec_type == CODEC_TYPE_DATA &&
            curstream->codec->codec_id   == CODEC_ID_DSMCC_B)
        {
            ProcessDSMCCPacket(curstream, pkt);

            av_free_packet(pkt);

            // Have to return regularly to ensure that the OSD is updated.
            // This applies both to MHEG and also channel browsing.
            if (onlyvideo < 0)
            {
                allowedquit |= (itv && itv->ImageHasChanged());
                OSD *osd = NULL;
                if (!allowedquit && GetNVP() && (osd = GetNVP()->GetOSD()))
                    allowedquit |=  osd->HasChanged();
            }

            continue;
        }

        // we don't care about other data streams
        if (curstream->codec->codec_type == CODEC_TYPE_DATA)
        {
            av_free_packet(pkt);
            continue;
        }

        if (!curstream->codec->codec)
        {
            VERBOSE(VB_PLAYBACK, LOC +
                    QString("No codec for stream index %1, type(%2) id(%3:%4)")
                    .arg(pkt->stream_index)
                    .arg(codec_type_string(curstream->codec->codec_type))
                    .arg(codec_id_string(curstream->codec->codec_id))
                    .arg(curstream->codec->codec_id));
            av_free_packet(pkt);
            continue;
        }

        firstloop = true;
        have_err = false;

        avcodeclock.lock();
        int ctype  = curstream->codec->codec_type;
        int audIdx = selectedTrack[kTrackTypeAudio].av_stream_index;
        int audSubIdx = selectedTrack[kTrackTypeAudio].av_substream_index;
        int subIdx = selectedTrack[kTrackTypeSubtitle].av_stream_index;
        avcodeclock.unlock();

        while (!have_err && len > 0)
        {
            int ret = 0;
            switch (ctype)
            {
                case CODEC_TYPE_AUDIO:
                {
                    bool reselectAudioTrack = false;

                    /// HACK HACK HACK -- begin See #3731
                    if (!GetNVP()->HasAudioIn())
                    {
                        VERBOSE(VB_AUDIO, LOC + "Audio is disabled - trying to restart it");
                        reselectAudioTrack = true;
                    }
                    /// HACK HACK HACK -- end

                    // detect switches between stereo and dual languages
                    bool wasDual = audSubIdx != -1;
                    bool isDual = curstream->codec->avcodec_dual_language;
                    if ((wasDual && !isDual) || (!wasDual &&  isDual))
                    {
                        SetupAudioStreamSubIndexes(audIdx);
                        reselectAudioTrack = true;
                    }                            

                    bool do_ac3_passthru =
                        (allow_ac3_passthru && !transcoding &&
                         (curstream->codec->codec_id == CODEC_ID_AC3));
                    bool do_dts_passthru =
                        (allow_dts_passthru && !transcoding &&
                         (curstream->codec->codec_id == CODEC_ID_DTS));
                    bool using_passthru = do_ac3_passthru || do_dts_passthru;

                    // detect channels on streams that need
                    // to be decoded before we can know this
                    bool already_decoded = false;
                    if (!curstream->codec->channels)
                    {
                        QMutexLocker locker(&avcodeclock);
                        VERBOSE(VB_IMPORTANT, LOC +
                                QString("Setting channels to %1")
                                .arg(audioOut.channels));

                        if (using_passthru)
                        {
                            // for passthru let it select the max number
                            // of channels
                            curstream->codec->channels = 0;
                            curstream->codec->request_channels = 0;
                        }
                        else
                        {
                            curstream->codec->channels = audioOut.channels;
                            curstream->codec->request_channels =
                                audioOut.channels;
                        }
                        ret = avcodec_decode_audio(
                            curstream->codec, audioSamples,
                            &data_size, ptr, len);
                        already_decoded = true;

                        reselectAudioTrack |= curstream->codec->channels;
                    }

                    if (reselectAudioTrack)
                    {
                        QMutexLocker locker(&avcodeclock);
                        currentTrack[kTrackTypeAudio] = -1;
                        selectedTrack[kTrackTypeAudio]
                            .av_stream_index = -1;
                        audIdx = -1;
                        audSubIdx = -1;
                        AutoSelectAudioTrack();
                        audIdx = selectedTrack[kTrackTypeAudio]
                            .av_stream_index;
                        audSubIdx = selectedTrack[kTrackTypeAudio]
                            .av_substream_index;
                    }

                    if (firstloop && pkt->pts != (int64_t)AV_NOPTS_VALUE)
                        lastapts = (long long)(av_q2d(curstream->time_base) *
                                               pkt->pts * 1000);

                    if ((onlyvideo > 0) || (pkt->stream_index != audIdx))
                    {
                        ptr += len;
                        len = 0;
                        continue;
                    }

                    if (skipaudio)
                    {
                        if ((lastapts < lastvpts - (10.0 / fps)) || 
                            lastvpts == 0)
                        {
                            ptr += len;
                            len = 0;
                            continue;
                        }
                        else
                            skipaudio = false;
                    }

                    avcodeclock.lock();
                    data_size = 0;
                    if (audioOut.do_passthru)
                    {
                        data_size = pkt->size;
                        bool dts = CODEC_ID_DTS == curstream->codec->codec_id;
                        ret = encode_frame(dts, ptr, len,
                                           audioSamples, data_size);
                    }
                    else
                    {
                        AVCodecContext *ctx = curstream->codec;

                        if ((ctx->channels == 0) ||
                            (ctx->channels > audioOut.channels))
                        {
                            ctx->channels = audioOut.channels;
                        }

                        if (!already_decoded)
                        {
                            curstream->codec->request_channels =
                                audioOut.channels;
                            ret = avcodec_decode_audio(
                                ctx, audioSamples, &data_size, ptr, len);
                        }

                        // When decoding some audio streams the number of
                        // channels, etc isn't known until we try decoding it.
                        if ((ctx->sample_rate != audioOut.sample_rate) ||
                            (ctx->channels    != audioOut.channels))
                        {
                            VERBOSE(VB_IMPORTANT, "audio stream changed");
                            currentTrack[kTrackTypeAudio] = -1;
                            selectedTrack[kTrackTypeAudio]
                                .av_stream_index = -1;
                            audIdx = -1;
                            AutoSelectAudioTrack();
                            data_size = 0;
                        }
                    }
                    avcodeclock.unlock();

                    // BEGIN Is this really safe? -- dtk
                    if (data_size <= 0)
                    {
                        ptr += ret;
                        len -= ret;
                        continue;
                    }
                    // END Is this really safe? -- dtk

                    long long temppts = lastapts;

                    // calc for next frame
                    lastapts += (long long)((double)(data_size * 1000) /
                                (curstream->codec->channels * 2) / 
                                curstream->codec->sample_rate);

                    VERBOSE(VB_PLAYBACK|VB_TIMESTAMP,
                            LOC + QString("audio timecode %1 %2 %3 %4") 
                            .arg(pkt->pts).arg(pkt->dts)
                            .arg(temppts).arg(lastapts)); 

                    if (audSubIdx != -1)
                    {
                        extract_mono_channel(audSubIdx, &audioOut,
                                             (char*)audioSamples, data_size);
                    }

                    GetNVP()->AddAudioData(
                        (char *)audioSamples, data_size, temppts);

                    total_decoded_audio += data_size;

                    allowedquit |= ringBuffer->InDVDMenuOrStillFrame();
                    allowedquit |= (onlyvideo < 0) &&
                        (ofill + total_decoded_audio > othresh);

                    // top off audio buffers initially in audio only mode
                    if (!allowedquit && (onlyvideo < 0))
                    {
                        uint fill, total;
                        GetNVP()->GetAudioBufferStatus(fill, total);
                        total /= 6; // HACK needed for some audio files
                        allowedquit =
                            (fill == 0) || (fill > (total>>1)) ||
                            ((total - fill) < (uint) data_size) ||
                            (ofill + total_decoded_audio > (total>>2)) ||
                            ((total - fill) < (uint) data_size * 2);
                    }

                    break;
                }
                case CODEC_TYPE_VIDEO:
                {
                    if (pkt->stream_index != selectedVideoIndex)
                    {
                        ptr += pkt->size;
                        len -= pkt->size;
                        continue;
                    }
                    
                    if (firstloop && pts != (int64_t) AV_NOPTS_VALUE)
                    {
                        lastccptsu = (long long)
                            (av_q2d(curstream->time_base)*pkt->pts*1000000);
                    }
                    if (onlyvideo < 0)
                    {
                        framesPlayed++;
                        gotvideo = 1;
                        ptr += pkt->size;
                        len -= pkt->size;
                        continue;
                    }

                    AVCodecContext *context = curstream->codec;
                    AVFrame mpa_pic;
                    bzero(&mpa_pic, sizeof(AVFrame));

                    int gotpicture = 0;

                    avcodeclock.lock();
                    if (d->HasDecoder())
                    {
                        if (decodeStillFrame)
                        {
                            int count = 0;
                            // HACK
                            while (!gotpicture && count < 5)
                            {
                                ret = d->DecodeMPEG2Video(context, &mpa_pic,
                                                  &gotpicture, ptr, len);
                                count++;
                            }
                        }
                        else
                        {
                            ret = d->DecodeMPEG2Video(context, &mpa_pic,
                                                &gotpicture, ptr, len);
                        }
                    }
                    else
                    {
                        ret = avcodec_decode_video(context, &mpa_pic,
                                                   &gotpicture, ptr, len);
                        // Reparse it to not drop the DVD still frame
                        if (decodeStillFrame)
                            ret = avcodec_decode_video(context, &mpa_pic,
                                                        &gotpicture, ptr, len);
                    }
                    avcodeclock.unlock();

                    if (ret < 0)
                    {
                        VERBOSE(VB_IMPORTANT, LOC_ERR +
                                "Unknown decoding error");
                        have_err = true;
                        continue;
                    }

                    if (!gotpicture)
                    {
                        ptr += ret;
                        len -= ret;
                        continue;
                    }

                    // Decode ATSC captions
                    for (uint i = 0; i < (uint)mpa_pic.atsc_cc_len;
                         i += ((mpa_pic.atsc_cc_buf[i] & 0x1f) * 3) + 2)
                    {
                        DecodeDTVCC(mpa_pic.atsc_cc_buf + i);
                    }

                    // Decode DVB captions from MPEG user data
                    if (mpa_pic.dvb_cc_len > 0)
                    {
                        unsigned long long utc = lastccptsu;

                        for (uint i = 0; i < (uint)mpa_pic.dvb_cc_len; i += 2)
                        {
                            uint8_t cc_lo = mpa_pic.dvb_cc_buf[i];
                            uint8_t cc_hi = mpa_pic.dvb_cc_buf[i+1];

                            uint16_t cc_dt = (cc_hi << 8) | cc_lo;

                            if (cc608_good_parity(cc608_parity_table, cc_dt))
                            {
                                ccd608->FormatCCField(utc/1000, 0, cc_dt);
                                utc += 33367;
                            }
                        }
                        lastccptsu = utc;
                    }

                    VideoFrame *picframe = (VideoFrame *)(mpa_pic.opaque);

                    if (!directrendering)
                    {
                        AVPicture tmppicture;
 
                        VideoFrame *xf = picframe;
                        picframe = GetNVP()->GetNextVideoFrame(false);

                        unsigned char *buf = picframe->buf;
                        tmppicture.data[0] = buf + picframe->offsets[0];
                        tmppicture.data[1] = buf + picframe->offsets[1];
                        tmppicture.data[2] = buf + picframe->offsets[2];
                        tmppicture.linesize[0] = picframe->pitches[0];
                        tmppicture.linesize[1] = picframe->pitches[1];
                        tmppicture.linesize[2] = picframe->pitches[2];

                        img_convert(&tmppicture, PIX_FMT_YUV420P, 
                                    (AVPicture *)&mpa_pic,
                                    context->pix_fmt,
                                    context->width,
                                    context->height);

                        if (xf)
                        {
                            // Set the frame flags, but then discard it
                            // since we are not using it for display.
                            xf->interlaced_frame = mpa_pic.interlaced_frame;
                            xf->top_field_first = mpa_pic.top_field_first;
                            xf->frameNumber = framesPlayed;
                            GetNVP()->DiscardVideoFrame(xf);
                        }
                    }

                    long long temppts = pts;

                    // Validate the video pts against the last pts. If it's
                    // a little bit smaller or equal, compute it from the last.
                    // Otherwise assume a wraparound.
                    if (!ringBuffer->isDVD() && 
                        temppts <= lastvpts &&
                        (temppts + 10000 > lastvpts || temppts < 0))
                    {
                        temppts = lastvpts;
                        temppts += (long long)(1000 * av_q2d(context->time_base));
                        // MPEG2 frames can be repeated, update pts accordingly
                        temppts += (long long)(mpa_pic.repeat_pict * 500
                                      * av_q2d(curstream->codec->time_base));
                    }

                    VERBOSE(VB_PLAYBACK|VB_TIMESTAMP, LOC +
                            QString("video timecode %1 %2 %3 %4")
                            .arg(pkt->pts).arg(pkt->dts).arg(temppts)
                            .arg(lastvpts));

/* XXX: Broken.
                    if (mpa_pic.qscale_table != NULL && mpa_pic.qstride > 0 &&
                        context->height == picframe->height)
                    {
                        int tblsize = mpa_pic.qstride *
                                      ((picframe->height + 15) / 16);

                        if (picframe->qstride != mpa_pic.qstride ||
                            picframe->qscale_table == NULL)
                        {
                            picframe->qstride = mpa_pic.qstride;
                            if (picframe->qscale_table)
                                delete [] picframe->qscale_table;
                            picframe->qscale_table = new unsigned char[tblsize];
                        }

                        memcpy(picframe->qscale_table, mpa_pic.qscale_table,
                               tblsize);
                    }
*/

                    picframe->interlaced_frame = mpa_pic.interlaced_frame;
                    picframe->top_field_first = mpa_pic.top_field_first;
                    picframe->repeat_pict = mpa_pic.repeat_pict;

                    picframe->frameNumber = framesPlayed;
                    GetNVP()->ReleaseNextVideoFrame(picframe, temppts);
                    if (d->HasMPEG2Dec() && mpa_pic.data[3])
                        context->release_buffer(context, &mpa_pic);

                    decoded_video_frame = picframe;
                    gotvideo = 1;
                    framesPlayed++;

                    if (decodeStillFrame)
                        decodeStillFrame = false;

                    lastvpts = temppts;
                    break;
                }
                case CODEC_TYPE_SUBTITLE:
                {
                    int gotSubtitles = 0;
                    AVSubtitle subtitle;
                    memset(&subtitle, 0, sizeof(AVSubtitle));

                    if (ringBuffer->isDVD())
                    {
                        if (ringBuffer->DVD()->NumMenuButtons() > 0)
                        {
                            ringBuffer->DVD()->GetMenuSPUPkt(ptr, len,
                                                             curstream->id);
                        }
                        else
                        {
                            if (pkt->stream_index == subIdx)
                            {
                                QMutexLocker locker(&avcodeclock);
                                ringBuffer->DVD()->DecodeSubtitles(&subtitle, 
                                                                   &gotSubtitles,
                                                                   ptr, len);
                            }
                        }
                    }
                    else if (pkt->stream_index == subIdx)
                    {
                        QMutexLocker locker(&avcodeclock);
                        avcodec_decode_subtitle(curstream->codec,
                                                &subtitle, &gotSubtitles,
                                                ptr, len);
                    }

                    // the subtitle decoder always consumes the whole packet
                    ptr += len;
                    len = 0;

                    if (gotSubtitles) 
                    {
                        subtitle.start_display_time += pts;
                        subtitle.end_display_time += pts;
                        GetNVP()->AddAVSubtitle(subtitle);
                    }

                    break;
                }
                default:
                {
                    AVCodecContext *enc = curstream->codec;
                    VERBOSE(VB_IMPORTANT, LOC_ERR +
                            QString("Decoding - id(%1) type(%2)")
                            .arg(codec_id_string(enc->codec_id))
                            .arg(codec_type_string(enc->codec_type)));
                    have_err = true;
                    break;
                }
            }

            if (!have_err)
            {
                ptr += ret;
                len -= ret;
                frame_decoded = 1;
                firstloop = false;
            }
        }

        av_free_packet(pkt);
    }

    if (pkt)
        delete pkt;

    return true;
}

bool AvFormatDecoder::HasVideo(const AVFormatContext *ic)
{
    if (!ic || !ic->cur_pmt_sect)
        return true;

    const PESPacket pes = PESPacket::ViewData(ic->cur_pmt_sect);
    const PSIPTable psip(pes);
    const ProgramMapTable pmt(psip);

    bool has_video = false;
    for (uint i = 0; i < pmt.StreamCount(); i++)
    {
        // MythTV remaps OpenCable Video to normal video during recording
        // so "dvb" is the safest choice for system info type, since this
        // will ignore other uses of the same stream id in DVB countries.
        has_video |= pmt.IsVideo(i, "dvb");

        // MHEG may explictly select a private stream as video
        has_video |= ((i == (uint)selectedVideoIndex) &&
                      (pmt.StreamType(i) == StreamID::PrivData));
    }

    return has_video;
}

bool AvFormatDecoder::GenerateDummyVideoFrame(void)
{
    if (!GetNVP()->getVideoOutput())
        return false;

    VideoFrame *frame = GetNVP()->GetNextVideoFrame(true);
    if (!frame)
        return false;

    if (dummy_frame && !compatible(frame, dummy_frame))
    {
        delete [] dummy_frame->buf;
        delete dummy_frame;
        dummy_frame = NULL;
    }

    if (!dummy_frame)
    {
        dummy_frame = new VideoFrame;
        init(dummy_frame,
             frame->codec, new unsigned char[frame->size],
             frame->width, frame->height, frame->bpp, frame->size,
             frame->pitches, frame->offsets);

        clear(dummy_frame, GUID_YV12_PLANAR);
        // Note: instead of clearing the frame to black, one
        // could load an image or a series of images...

        dummy_frame->interlaced_frame = 0; // not interlaced
        dummy_frame->top_field_first  = 1; // top field first
        dummy_frame->repeat_pict      = 0; // not a repeated picture
    }

    copy(frame, dummy_frame);

    frame->frameNumber = framesPlayed;

    GetNVP()->ReleaseNextVideoFrame(frame, lastvpts);
    GetNVP()->getVideoOutput()->DeLimboFrame(frame);

    decoded_video_frame = frame;
    framesPlayed++;

    return true;
}

QString AvFormatDecoder::GetCodecDecoderName(void) const
{
    if (d && d->HasMPEG2Dec())
        return "libmpeg2";

    if ((video_codec_id > kCodec_VLD_END) &&
        (video_codec_id < kCodec_DVDV_END))
        return "macaccel";

    if ((video_codec_id > kCodec_NORMAL_END) &&
        (video_codec_id < kCodec_STD_XVMC_END))
        return "xvmc";

    if ((video_codec_id > kCodec_STD_XVMC_END) &&
        (video_codec_id < kCodec_VLD_END))
        return "xvmc-vld";

    return "ffmpeg";
}

void *AvFormatDecoder::GetVideoCodecPrivate(void)
{
    return d->GetDVDVDecoder();
}

void AvFormatDecoder::SetDisablePassThrough(bool disable)
{
    // can only disable never reenable as once
    // timestretch is on its on for the session
    if (disable_passthru)
        return;

    if (selectedTrack[kTrackTypeAudio].av_stream_index < 0)
    {
        disable_passthru = disable;
        return;
    }

    if (disable != disable_passthru)
    {
        disable_passthru = disable;
        QString msg = (disable) ? "Disabling" : "Allowing";
        VERBOSE(VB_AUDIO, LOC + msg + " pass through");

        // Force pass through state to be reanalyzed
        QMutexLocker locker(&avcodeclock);
        SetupAudioStream();
    }
}

/** \fn AvFormatDecoder::SetupAudioStream(void)
 *  \brief Reinitializes audio if it needs to be reinitialized.
 *
 *   NOTE: The avcodeclock must be held when this is called.
 *
 *  \return true if audio changed, false otherwise
 */
bool AvFormatDecoder::SetupAudioStream(void)
{
    AudioInfo info; // no_audio
    AVStream *curstream = NULL;
    AVCodecContext *codec_ctx = NULL;
    AudioInfo old_in  = audioIn;
    AudioInfo old_out = audioOut;
    bool using_passthru = false;

    if ((currentTrack[kTrackTypeAudio] >= 0) &&
        (selectedTrack[kTrackTypeAudio].av_stream_index <=
         (int) ic->nb_streams) &&
        (curstream = ic->streams[selectedTrack[kTrackTypeAudio]
                                 .av_stream_index]))
    {
        assert(curstream);
        assert(curstream->codec);
        codec_ctx = curstream->codec;        
        bool do_ac3_passthru = (allow_ac3_passthru && !transcoding &&
                                (codec_ctx->codec_id == CODEC_ID_AC3));
        bool do_dts_passthru = (allow_dts_passthru && !transcoding &&
                                (codec_ctx->codec_id == CODEC_ID_DTS));
        using_passthru = do_ac3_passthru || do_dts_passthru;
        info = AudioInfo(codec_ctx->codec_id,
                         codec_ctx->sample_rate, codec_ctx->channels,
                         using_passthru && !disable_passthru);
    }

    if (info == audioIn)
        return false; // no change

    QString ptmsg = (using_passthru) ? " using passthru" : "";
    VERBOSE(VB_AUDIO, LOC + "Initializing audio parms from " +
            QString("audio track #%1").arg(currentTrack[kTrackTypeAudio]+1));

    audioOut = audioIn = info;
    if (using_passthru)
    {
        // A passthru stream looks like a 48KHz 2ch (@ 16bit) to the sound card
        AudioInfo digInfo = audioOut;
        if (!disable_passthru)
        {
            digInfo.channels    = 2;
            digInfo.sample_rate = 48000;
            digInfo.sample_size = 4;
        }
        if (audioOut.channels > (int) max_channels)
        {
            audioOut.channels = (int) max_channels;
            audioOut.sample_size = audioOut.channels * 2;
            codec_ctx->channels = audioOut.channels;
        }
        VERBOSE(VB_AUDIO, LOC + "Audio format changed digital passthrough " +
                QString("%1\n\t\t\tfrom %2 ; %3\n\t\t\tto   %4 ; %5")
                .arg(digInfo.toString())
                .arg(old_in.toString()).arg(old_out.toString())
                .arg(audioIn.toString()).arg(audioOut.toString()));

        if (digInfo.sample_rate > 0)
            GetNVP()->SetEffDsp(digInfo.sample_rate * 100);

        GetNVP()->SetAudioParams(digInfo.bps(), digInfo.channels,
                                 digInfo.sample_rate, audioIn.do_passthru);
        // allow the audio stuff to reencode
        GetNVP()->SetAudioCodec(codec_ctx);
        GetNVP()->ReinitAudio();
        return true;
    }
    else
    {
        if (audioOut.channels > (int) max_channels)
        {
            audioOut.channels = (int) max_channels;
            audioOut.sample_size = audioOut.channels * 2;
            codec_ctx->channels = audioOut.channels;
        }
    }

    VERBOSE(VB_AUDIO, LOC + "Audio format changed " +
            QString("\n\t\t\tfrom %1 ; %2\n\t\t\tto   %3 ; %4")
            .arg(old_in.toString()).arg(old_out.toString())
            .arg(audioIn.toString()).arg(audioOut.toString()));

    if (audioOut.sample_rate > 0)
        GetNVP()->SetEffDsp(audioOut.sample_rate * 100);

    GetNVP()->SetAudioParams(audioOut.bps(), audioOut.channels,
                             audioOut.sample_rate,
                             audioIn.do_passthru);

    // allow the audio stuff to reencode
    GetNVP()->SetAudioCodec(using_passthru?codec_ctx:NULL);
    QString errMsg = GetNVP()->ReinitAudio();
    bool audiook = errMsg.isEmpty();

    return true;
}

static int encode_frame(bool dts, unsigned char *data, int len,
                        short *samples, int &samples_size)
{
    int enc_len;
    int flags, sample_rate, bit_rate;
    unsigned char* ucsamples = (unsigned char*) samples;

    // we don't do any length/crc validation of the AC3 frame here; presumably
    // the receiver will have enough sense to do that.  if someone has a
    // receiver that doesn't, here would be a good place to put in a call
    // to a52_crc16_block(samples+2, data_size-2) - but what do we do if the
    // packet is bad?  we'd need to send something that the receiver would
    // ignore, and if so, may as well just assume that it will ignore
    // anything with a bad CRC...

    uint nr_samples = 0, block_len;
    if (dts)
    {
        enc_len = dts_syncinfo(data, &flags, &sample_rate, &bit_rate);
        int rate, sfreq, nblks;
        dts_decode_header(data, &rate, &nblks, &sfreq);
        nr_samples = nblks * 32;
        block_len = nr_samples * 2 * 2;
    }
    else
    {
        AC3HeaderInfo hdr;
        if (!ff_ac3_parse_header(data, &hdr))
        {
            enc_len = hdr.frame_size;
        }
        else
        {
            // creates endless loop
            enc_len = 0;
        }
        block_len = MAX_AC3_FRAME_SIZE;
    }

    if (enc_len == 0 || enc_len > len)
    {
        samples_size = 0;
        return len;
    }

    enc_len = min((uint)enc_len, block_len - 8);

    swab((const char*) data, (char*) (ucsamples + 8), enc_len);

    // the following values come from libmpcodecs/ad_hwac3.c in mplayer.
    // they form a valid IEC958 AC3 header.
    ucsamples[0] = 0x72;
    ucsamples[1] = 0xF8;
    ucsamples[2] = 0x1F;
    ucsamples[3] = 0x4E;
    ucsamples[4] = 0x01;
    if (dts)
    {
        switch(nr_samples)
        {
            case 512:
                ucsamples[4] = 0x0B;      /* DTS-1 (512-sample bursts) */
                break;

            case 1024:
                ucsamples[4] = 0x0C;      /* DTS-2 (1024-sample bursts) */
                break;

            case 2048:
                ucsamples[4] = 0x0D;      /* DTS-3 (2048-sample bursts) */
                break;

            default:
                VERBOSE(VB_IMPORTANT, LOC +
                        QString("DTS: %1-sample bursts not supported")
                        .arg(nr_samples));
                ucsamples[4] = 0x00;
                break;
        }
    }
    ucsamples[5] = 0x00;
    ucsamples[6] = (enc_len << 3) & 0xFF;
    ucsamples[7] = (enc_len >> 5) & 0xFF;
    memset(ucsamples + 8 + enc_len, 0, block_len - 8 - enc_len);
    samples_size = block_len;

    return enc_len;
}

static int DTS_SAMPLEFREQS[16] =
{
    0,      8000,   16000,  32000,  64000,  128000, 11025,  22050,
    44100,  88200,  176400, 12000,  24000,  48000,  96000,  192000
};

static int DTS_BITRATES[30] =
{
    32000,    56000,    64000,    96000,    112000,   128000,
    192000,   224000,   256000,   320000,   384000,   448000,
    512000,   576000,   640000,   768000,   896000,   1024000,
    1152000,  1280000,  1344000,  1408000,  1411200,  1472000,
    1536000,  1920000,  2048000,  3072000,  3840000,  4096000
};

static int dts_syncinfo(uint8_t *indata_ptr, int */*flags*/,
                        int *sample_rate, int *bit_rate)
{
    int nblks;
    int rate;
    int sfreq;

    int fsize = dts_decode_header(indata_ptr, &rate, &nblks, &sfreq);
    if (fsize >= 0)
    {
        if (rate >= 0 && rate <= 29)
            *bit_rate = DTS_BITRATES[rate];
        else
            *bit_rate = 0;
        if (sfreq >= 1 && sfreq <= 15)
            *sample_rate = DTS_SAMPLEFREQS[sfreq];
        else
            *sample_rate = 0;
    }
    return fsize;
}

static int dts_decode_header(uint8_t *indata_ptr, int *rate,
                             int *nblks, int *sfreq)
{
    uint id = ((indata_ptr[0] << 24) | (indata_ptr[1] << 16) |
               (indata_ptr[2] << 8)  | (indata_ptr[3]));

    if (id != 0x7ffe8001)
        return -1;

    int ftype = indata_ptr[4] >> 7;

    int surp = (indata_ptr[4] >> 2) & 0x1f;
    surp = (surp + 1) % 32;

    *nblks = (indata_ptr[4] & 0x01) << 6 | (indata_ptr[5] >> 2);
    ++*nblks;

    int fsize = (indata_ptr[5] & 0x03) << 12 |
                (indata_ptr[6]         << 4) | (indata_ptr[7] >> 4);
    ++fsize;

    *sfreq = (indata_ptr[8] >> 2) & 0x0f;
    *rate = (indata_ptr[8] & 0x03) << 3 | ((indata_ptr[9] >> 5) & 0x07);

    if (ftype != 1)
    {
        VERBOSE(VB_IMPORTANT, LOC +
                QString("DTS: Termination frames not handled (ftype %1)")
                .arg(ftype));
        return -1;
    }

    if (*sfreq != 13)
    {
        VERBOSE(VB_IMPORTANT, LOC +
                QString("DTS: Only 48kHz supported (sfreq %1)").arg(*sfreq));
        return -1;
    }

    if ((fsize > 8192) || (fsize < 96))
    {
        VERBOSE(VB_IMPORTANT, LOC +
                QString("DTS: fsize: %1 invalid").arg(fsize));
        return -1;
    }

    if (*nblks != 8 && *nblks != 16 && *nblks != 32 &&
        *nblks != 64 && *nblks != 128 && ftype == 1)
    {
        VERBOSE(VB_IMPORTANT, LOC +
                QString("DTS: nblks %1 not valid for normal frame")
                .arg(*nblks));
        return -1;
    }

    return fsize;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
