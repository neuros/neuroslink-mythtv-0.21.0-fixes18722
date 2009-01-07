#ifndef VIDEOOUT_XV_H_
#define VIDEOOUT_XV_H_

#include <set>
#include <qwindowdefs.h>

#include "DisplayRes.h"
#include "videooutbase.h"

#include "util-x11.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xvlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>

#undef HAVE_AV_CONFIG_H
extern "C" {
#include "../libavcodec/avcodec.h"
}

class NuppelVideoPlayer;
class ChromaKeyOSD;

class OpenGLContext;
class OpenGLVideo;

class XvMCBufferSettings;
class XvMCSurfaceTypes;
class XvMCTextures;
class XvMCOSD;

#ifdef USING_XVMC
#   include "XvMCSurfaceTypes.h"
#   include "../libavcodec/xvmc_render.h"
    typedef struct
    {
        XvMCSurface         surface;
        XvMCBlockArray      blocks;
        XvMCMacroBlockArray macro_blocks;
    } xvmc_vo_surf_t;
#else // if !USING_XVMC
    typedef int xvmc_vo_surf_t;
    typedef int XvMCSurfaceInfo;
    struct XvMCContext;
#endif // !USING_XVMC

typedef enum VideoOutputSubType
{
    XVUnknown = 0, Xlib, XShm, OpenGL, XVideo, XVideoMC, XVideoIDCT, XVideoVLD,
} VOSType;

class VideoOutputXv : public VideoOutput
{
    friend class ChromaKeyOSD;
    friend class OpenGLVideoSync;
    friend class XvMCOSD;
  public:
    VideoOutputXv(MythCodecID av_codec_id);
   ~VideoOutputXv();

    bool Init(int width, int height, float aspect, WId winid,
              int winx, int winy, int winw, int winh, WId embedid = 0);

    bool SetDeinterlacingEnabled(bool);
    bool SetupDeinterlace(bool interlaced, const QString& ovrf="");
    bool ApproveDeintFilter(const QString& filtername) const;

    void ProcessFrame(VideoFrame *frame, OSD *osd,
                      FilterChain *filterList,
                      NuppelVideoPlayer *pipPlayer);
    void PrepareFrame(VideoFrame*, FrameScanType);
    void DrawSlice(VideoFrame*, int x, int y, int w, int h);
    void Show(FrameScanType);

    void ClearAfterSeek(void);

    void MoveResize(void);
    bool InputChanged(const QSize &input_size,
                      float        aspect,
                      MythCodecID  av_codec_id,
                      void        *codec_private);
    void Zoom(ZoomDirection direction);
    void VideoAspectRatioChanged(float aspect);
    void EmbedInWidget(WId wid, int x, int y, int w, int h);
    void StopEmbedding(void);
    void ResizeForGui(void); 
    void ResizeForVideo(void);
    void DrawUnusedRects(bool sync = true);
    void UpdatePauseFrame(void);
    int  SetPictureAttribute(PictureAttribute attribute, int newValue);
    void InitPictureAttributes(void);

    /// Monitor refresh time in microseconds
    int  GetRefreshRate(void);

    virtual bool hasMCAcceleration(void) const
        { return XVideoMC <= VideoOutputSubType(); }
    virtual bool hasIDCTAcceleration(void) const
        { return XVideoIDCT <= VideoOutputSubType(); }
    virtual bool hasVLDAcceleration(void) const
        { return XVideoVLD == VideoOutputSubType(); }

    void CheckFrameStates(void);
    QRect GetPIPRect(int location, NuppelVideoPlayer *pipplayer = NULL);

    virtual void ShutdownVideoResize(void);

    static MythCodecID GetBestSupportedCodec(uint width, uint height,
                                             uint osd_width, uint osd_height,
                                             uint stream_type, int xvmc_chroma,
                                             bool test_surface, bool force_xv);

    static int GrabSuitableXvPort(Display* disp, Window root,
                                  MythCodecID type,
                                  uint width, uint height,
                                  int xvmc_chroma = 0,
                                  XvMCSurfaceInfo* si = NULL,
                                  QString *adaptor_name = NULL);

    static XvMCContext* CreateXvMCContext(Display* disp, int port,
                                          int surf_type,
                                          int width, int height);
    static void DeleteXvMCContext(Display* disp, XvMCContext*& ctx);


    static QStringList GetAllowedRenderers(MythCodecID myth_codec_id,
                                           const QSize &video_dim);

  private:
    VOSType VideoOutputSubType() const { return video_output_subtype; }
    virtual QRect GetVisibleOSDBounds(float&, float&, float) const;
    virtual QRect GetTotalOSDBounds(void) const;

    VideoFrame *GetNextFreeFrame(bool allow_unsafe);
    void DiscardFrame(VideoFrame*);
    void DiscardFrames(bool next_frame_keyframe);
    void DoneDisplayingFrame(void);

    void ProcessFrameXvMC(VideoFrame *frame, OSD *osd);
    void ProcessFrameOpenGL(VideoFrame *frame, OSD *osd,
                            FilterChain *filterList,
                            NuppelVideoPlayer *pipPlayer);
    void ProcessFrameMem(VideoFrame *frame, OSD *osd,
                         FilterChain *filterList,
                         NuppelVideoPlayer *pipPlayer);

    void PrepareFrameXvMC(VideoFrame *, FrameScanType);
    void PrepareFrameXv(VideoFrame *);
    void PrepareFrameOpenGL(VideoFrame *, FrameScanType);
    void PrepareFrameMem(VideoFrame *, FrameScanType);

    void ShowXvMC(FrameScanType scan);
    void ShowXVideo(FrameScanType scan);

    void ShowPip(VideoFrame *frame, NuppelVideoPlayer *pipplayer);
    virtual int DisplayOSD(VideoFrame *frame, OSD *osd,
                           int stride = -1, int revision = -1);

    void ResizeForVideo(uint width, uint height);
    void InitDisplayMeasurements(uint width, uint height);
    void InitColorKey(bool turnoffautopaint);

    bool InitVideoBuffers(MythCodecID, bool use_xv,
                          bool use_shm, bool use_opengl);

    bool InitXvMC(MythCodecID);
    bool InitXVideo(void);
    bool InitOpenGL(void);
    bool InitXShm(void);
    bool InitXlib(void);
    bool InitOSD(const QString&);
    bool CheckOSDInit(void);

    bool CreateXvMCBuffers(void);
    bool CreateBuffers(VOSType subtype);
    vector<void*> CreateXvMCSurfaces(uint num, bool surface_has_vld);
    vector<unsigned char*> CreateShmImages(uint num, bool use_xv);
    void CreatePauseFrame(VOSType subtype);
    void CopyFrame(VideoFrame *to, const VideoFrame *from);

    void DeleteBuffers(VOSType subtype, bool delete_pause_frame);

    bool InitSetupBuffers(void);

    // XvMC specific helper functions
    static bool IsDisplaying(VideoFrame* frame);
    static bool IsRendering(VideoFrame* frame);
    static void SyncSurface(VideoFrame* frame, int past_future = 0);
    static void FlushSurface(VideoFrame* frame);

#ifdef USING_XVMC 
    XvMCOSD* GetAvailableOSD();
    void ReturnAvailableOSD(XvMCOSD*);
#endif // USING_XVMC

    // OpenGL specific helper functions
    bool SetDeinterlacingEnabledOpenGL(bool enable);
    bool SetupDeinterlaceOpenGL(
        bool interlaced, const QString &overridefilter);


    // Misc.
    MythCodecID          myth_codec_id;
    VOSType              video_output_subtype;
    DisplayRes          *display_res;
    QMutex               global_lock;

    // Basic X11 info
    Window               XJ_root;
    Window               XJ_win;
    Window               XJ_curwin;
    GC                   XJ_gc;
    Screen              *XJ_screen;
    Display             *XJ_disp;
    int                  XJ_screen_num;
    unsigned long        XJ_white;
    unsigned long        XJ_black;
    unsigned long        XJ_letterbox_colour;
    int                  XJ_depth;
    int                  XJ_screenx;
    int                  XJ_screeny;
    int                  XJ_screenwidth;
    int                  XJ_screenheight;
    bool                 XJ_started;

    // Used for all non-XvMC drawing
    VideoFrame           av_pause_frame;
    vector<XShmSegmentInfo*> XJ_shm_infos;
    vector<YUVInfo>      XJ_yuv_infos;

    // Basic non-Xv drawing info
    XImage              *XJ_non_xv_image;
    long long            non_xv_frames_shown;
    int                  non_xv_show_frame;
    int                  non_xv_fps;
    int                  non_xv_av_format;
    time_t               non_xv_stop_time;

    // Basic XvMC drawing info
    XvMCBufferSettings  *xvmc_buf_attr;
    int                  xvmc_chroma;
    XvMCContext         *xvmc_ctx;
    vector<void*>        xvmc_surfs;
    QMutex               xvmc_osd_lock;
    MythDeque<XvMCOSD*>  xvmc_osd_available;
#ifdef USING_XVMC 
    XvMCSurfaceInfo      xvmc_surf_info;
#endif // USING_XVMC

    // Support for nVidia XvMC copy to texture feature
    XvMCTextures        *xvmc_tex;

    // Basic Xv drawing info
    int                  xv_port;
    int                  xv_hue_base;
    int                  xv_colorkey;
    bool                 xv_draw_colorkey;
    int                  xv_chroma;
    buffer_map_t         xv_buffers;
    bool                 xv_need_bobdeint_repaint;
    QMap<PictureAttribute,int> xv_attribute_min;
    QMap<PictureAttribute,int> xv_attribute_max;

    // OpenGL drawing info
    QMutex               gl_context_lock;
    OpenGLContext       *gl_context;
    OpenGLVideo         *gl_videochain;
    OpenGLVideo         *gl_pipchain;
    OpenGLVideo         *gl_osdchain;
    bool                 gl_use_osd_opengl2;
    bool                 gl_pip_ready;
    bool                 gl_osd_ready;

    // Chromakey OSD info
    ChromaKeyOSD        *chroma_osd;
};

#endif // VIDEOOUT_XV_H_
