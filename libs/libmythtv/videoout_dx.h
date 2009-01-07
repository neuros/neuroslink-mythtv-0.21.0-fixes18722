#ifndef VIDEOOUT_DX_H_
#define VIDEOOUT_DX_H_

/* ACK! <windows.h> and <ddraw.h> should only be in cpp's compiled in
 * windows only. Some of the variables in VideoOutputDX need to be
 * moved to a private class before removing these includes though.
 */
#include <windows.h> // HACK HACK HACK
#include <ddraw.h>   // HACK HACK HACK

// MythTV headers
#ifdef CONFIG_CYGWIN
#undef max
#endif
#include "videooutbase.h"

class VideoOutputDX : public VideoOutput
{
  public:
    VideoOutputDX();
   ~VideoOutputDX();

    bool Init(int width, int height, float aspect, WId winid,
              int winx, int winy, int winw, int winh, WId embedid = 0);
    void PrepareFrame(VideoFrame *buffer, FrameScanType);
    void Show(FrameScanType );

    bool InputChanged(const QSize &input_size,
                      float        aspect,
                      MythCodecID  av_codec_id,
                      void        *codec_private);
    void Zoom(ZoomDirection direction);

    void EmbedInWidget(WId wid, int x, int y, int w, int h);
    void StopEmbedding(void);

    int GetRefreshRate(void);

    void DrawUnusedRects(bool sync = true);

    void UpdatePauseFrame(void);
    void ProcessFrame(VideoFrame *frame, OSD *osd,
                      FilterChain *filterList,
                      NuppelVideoPlayer *pipPlayer);

    void MoveResize(void);
    int  SetPictureAttribute(PictureAttribute attribute, int newValue);

    float GetDisplayAspect(void) const;

    void WaitForVSync(void);

    static QStringList GetAllowedRenderers(MythCodecID myth_codec_id,
                                           const QSize &video_dim);

  private:
    void Exit(void);

    bool XJ_started;

    VideoFrame pauseFrame;
    
    HWND wnd;
    
    LPDIRECTDRAW2        ddobject;                    /* DirectDraw object */
    LPDIRECTDRAWSURFACE2 display;                        /* Display device */
//    LPDIRECTDRAWSURFACE2 current_surface;   /* surface currently displayed */
    LPDIRECTDRAWCLIPPER  clipper;             /* clipper used for blitting */
    LPDIRECTDRAWCOLORCONTROL ccontrol;        /* colour controls object */
    HINSTANCE            ddraw_dll;       /* handle of the opened ddraw dll */

    /* Multi-monitor support */
    HMONITOR             monitor;          /* handle of the current monitor */
    GUID                 *display_driver;
    HMONITOR             (WINAPI* MonitorFromWindow)(HWND, DWORD);
    BOOL                 (WINAPI* GetMonitorInfo)(HMONITOR, LPMONITORINFO);

    RECT         rect_display;

    LPDIRECTDRAWSURFACE2 front_surface;
    LPDIRECTDRAWSURFACE2 back_surface;

    DWORD chroma;

    int XJ_width, XJ_height;

    int colorkey;
    int rgb_colorkey;

    bool using_overlay;
    bool overlay_3buf;
//    bool hw_yuv;
    bool use_sysmem;
    
    int outputpictures;
    
    void MakeSurface();
    
    int DirectXInitDDraw();
    int DirectXCreateDisplay();
    int DirectXCreateClipper();
    int DirectXCreateSurface(LPDIRECTDRAWSURFACE2 *pp_surface_final,
                                 DWORD i_chroma, bool b_overlay, int i_backbuffers);
    void DirectXCloseDDraw();
    void DirectXCloseDisplay();
    void DirectXCloseSurface();
    void DirectXGetDDrawCaps();
    DWORD DirectXFindColorkey(uint32_t i_color);
    int DirectXUpdateOverlay();
    int DirectXLockSurface(void **picbuf, int *stride);
    int DirectXUnlockSurface();
    
    int NewPicture();
};

#endif
