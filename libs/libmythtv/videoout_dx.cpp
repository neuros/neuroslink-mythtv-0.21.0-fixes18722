#include <map>
#include <iostream>
#include <algorithm>
using namespace std;

#define _WIN32_WINNT 0x500
#include "mythcontext.h"
#include "videoout_dx.h"
#include "filtermanager.h"
#include "fourcc.h"
#include "videodisplayprofile.h"

#include "mmsystem.h"
#include "tv.h"

#undef UNICODE

extern "C" {
#include "../libavcodec/avcodec.h"
}

const int kNumBuffers = 31;
const int kNeedFreeFrames = 1;
const int kPrebufferFramesNormal = 12;
const int kPrebufferFramesSmall = 4;
const int kKeepPrebuffer = 2;

/*****************************************************************************
 * DirectDraw GUIDs.
 * Defining them here allows us to get rid of the dxguid library during
 * the linking stage.
 *****************************************************************************/
#include <initguid.h>
DEFINE_GUID( IID_IDirectDraw2, 0xB3A6F3E0,0x2B43,0x11CF,0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56 );
DEFINE_GUID( IID_IDirectDrawSurface2, 0x57805885,0x6eec,0x11cf,0x94,0x41,0xa8,0x23,0x03,0xc1,0x0e,0x27 );
DEFINE_GUID( IID_IDirectDrawColorControl,     0x4B9F0EE0,0x0D7E,0x11D0,0x9B,0x06,0x00,0xA0,0xC9,0x03,0xA3,0xB8 );


VideoOutputDX::VideoOutputDX(void)
    : VideoOutput(), XJ_width(0), XJ_height(0)
{
    HMODULE user32;

    XJ_started = 0; 

    pauseFrame.buf = NULL;
    
    ddobject = NULL;
    display = NULL;
    display_driver = NULL;
//    current_surface = NULL;
    clipper = NULL;
    ccontrol = NULL;
    wnd = NULL;
    
//    hw_yuv = true;
    using_overlay = true;
    overlay_3buf = true;
    use_sysmem = false;
    
    front_surface = NULL;
    back_surface = NULL;

    /* Multimonitor stuff */
    monitor = NULL;
    display_driver = NULL;
    MonitorFromWindow = NULL;
    GetMonitorInfo = NULL;
    if ((user32 = GetModuleHandle("USER32")))
    {
        MonitorFromWindow = (HMONITOR (WINAPI*)(HWND, DWORD))
            GetProcAddress(user32, "MonitorFromWindow");
        GetMonitorInfo = (BOOL (WINAPI*)(HMONITOR, LPMONITORINFO))
            GetProcAddress(user32, "GetMonitorInfoA");
    }
}

VideoOutputDX::~VideoOutputDX()
{
    if (pauseFrame.buf)
        delete [] pauseFrame.buf;

    DirectXCloseSurface();
    DirectXCloseDisplay();
    DirectXCloseDDraw();

    Exit();
}

// this is documented in videooutbase.cpp
void VideoOutputDX::Zoom(ZoomDirection direction)
{
    VideoOutput::Zoom(direction);
    MoveResize();
}

void VideoOutputDX::MoveResize()
{
    VideoOutput::MoveResize();
    
    DirectXUpdateOverlay();
}

bool VideoOutputDX::InputChanged(const QSize &input_size,
                                 float        aspect,
                                 MythCodecID  av_codec_id,
                                 void        *codec_private)
{
    VideoOutput::InputChanged(input_size, aspect, av_codec_id, codec_private);

    db_vdisp_profile->SetVideoRenderer("directx");

    XJ_width  = video_dim.width();
    XJ_height = video_dim.height();

    vbuffers.DeleteBuffers();
    
    DirectXCloseSurface();
    MakeSurface();
    
    vbuffers.CreateBuffers(XJ_width, XJ_height);

    MoveResize();

    if (pauseFrame.buf)
        delete [] pauseFrame.buf;

    pauseFrame.height = vbuffers.GetScratchFrame()->height;
    pauseFrame.width  = vbuffers.GetScratchFrame()->width;
    pauseFrame.bpp    = vbuffers.GetScratchFrame()->bpp;
    pauseFrame.size   = vbuffers.GetScratchFrame()->size;
    pauseFrame.buf    = new unsigned char[pauseFrame.size];
    pauseFrame.frameNumber = vbuffers.GetScratchFrame()->frameNumber;

    return true;
}

int VideoOutputDX::GetRefreshRate(void)
{
    if (ddobject)
    {
        DWORD rate;
        
        IDirectDraw_GetMonitorFrequency(ddobject, &rate);
        
        return 1000000 / rate;
    }

    return 0;
}

void VideoOutputDX::WaitForVSync(void)
{
    if (!ddobject)
        return;
        
    IDirectDraw_WaitForVerticalBlank(ddobject, DDWAITVB_BLOCKBEGIN, NULL);
}

bool VideoOutputDX::Init(int width, int height, float aspect,
                           WId winid, int winx, int winy, int winw, 
                           int winh, WId embedid)
{
    db_vdisp_profile->SetVideoRenderer("directx");

    vbuffers.Init(kNumBuffers, true, kNeedFreeFrames, 
                  kPrebufferFramesNormal, kPrebufferFramesSmall, 
                  kKeepPrebuffer);
    VideoOutput::Init(width, height, aspect, winid,
                      winx, winy, winw, winh, embedid);

    wnd = winid;

    XJ_width  = video_dim.width();
    XJ_height = video_dim.height();

    vbuffers.CreateBuffers(XJ_width, XJ_height);
    MoveResize();

    /* Initialise DirectDraw if not already done.
     * We do this here because on multi-monitor systems we may have to
     * re-create the directdraw surfaces. */
    if (!ddobject && DirectXInitDDraw() != 0)
    {
        VERBOSE(VB_IMPORTANT, "cannot initialize DirectDraw");
        return false;
    }

    /* Create the directx display */
    if (!display && DirectXCreateDisplay() != 0)
    {
        VERBOSE(VB_IMPORTANT, "cannot initialize DirectDraw");
        return false;
    }

    MakeSurface();

    if (!outputpictures)
        return false;

    InitPictureAttributes();

    MoveResize();

    pauseFrame.height = vbuffers.GetScratchFrame()->height;
    pauseFrame.width  = vbuffers.GetScratchFrame()->width;
    pauseFrame.bpp    = vbuffers.GetScratchFrame()->bpp;
    pauseFrame.size   = vbuffers.GetScratchFrame()->size;
    pauseFrame.buf    = new unsigned char[pauseFrame.size];
    pauseFrame.frameNumber = vbuffers.GetScratchFrame()->frameNumber;
    
    XJ_started = true;

    return true;
}

void VideoOutputDX::Exit(void)
{
    if (XJ_started) 
    {
        XJ_started = false;

        vbuffers.DeleteBuffers();
    }
}

void VideoOutputDX::EmbedInWidget(WId wid, int x, int y, int w, 
                                    int h)
{
    if (embedding)
        return;

    VideoOutput::EmbedInWidget(wid, x, y, w, h);
}
 
void VideoOutputDX::StopEmbedding(void)
{
    if (!embedding)
        return;

    VideoOutput::StopEmbedding();
}

void VideoOutputDX::PrepareFrame(VideoFrame *buffer, FrameScanType t)
{
    if (IsErrored())
    {
        VERBOSE(VB_IMPORTANT, "VideoOutputDX::PrepareFrame() called while IsErrored is true.");
        return;
    }

    unsigned char *picbuf;
    int stride;
    
    if (!buffer)
        buffer = vbuffers.GetScratchFrame();

    framesPlayed = buffer->frameNumber + 1;

    if (DirectXLockSurface((void**) &picbuf, &stride) == 0)
    {
        if (chroma == FOURCC_IYUV || chroma == FOURCC_I420)
        {
            for (int i = 0; i < XJ_height; i++)
                memcpy(picbuf + (i * stride), buffer->buf + (i * XJ_width), XJ_width);
            for (int i = 0; i < XJ_height / 2; i++)
                memcpy(picbuf + (XJ_height * stride) + (i * stride / 2),
                        buffer->buf + XJ_height * XJ_width + (i * XJ_width / 2),
                        XJ_width / 2);
            for (int i = 0; i < XJ_height / 2; i++)
                memcpy(picbuf + (XJ_height * stride * 5 / 4) + (i * stride / 2),
                        buffer->buf + XJ_height * XJ_width * 5 / 4 + (i * XJ_width / 2),
                        XJ_width / 2);
        }
        else if (chroma == FOURCC_YV12)
        {
            for (int i = 0; i < XJ_height; i++)
                memcpy(picbuf + (i * stride), buffer->buf + (i * XJ_width), XJ_width);
            for (int i = 0; i < XJ_height / 2; i++)
                memcpy(picbuf + (XJ_height * stride) + (i * stride / 2),
                        buffer->buf + XJ_height * XJ_width * 5 / 4 + (i * XJ_width / 2),
                        XJ_width / 2);
            for (int i = 0; i < XJ_height / 2; i++)
                memcpy(picbuf + (XJ_height * stride * 5 / 4) + (i * stride / 2),
                        buffer->buf + XJ_height * XJ_width + (i * XJ_width / 2),
                        XJ_width / 2);
        } else {
        
            AVPicture image_in, image_out;
            int av_format;

            switch (chroma)
            {
                case FOURCC_YUY2:
                case FOURCC_YUYV:
                case FOURCC_YUNV: av_format = PIX_FMT_YUV422; break;
                case FOURCC_RV15: av_format = PIX_FMT_RGB555; break;
                case FOURCC_RV16: av_format = PIX_FMT_RGB565; break;
                case FOURCC_RV24: av_format = PIX_FMT_RGB24;  break;
                case FOURCC_RV32: av_format = PIX_FMT_RGBA32; break;
                default: 
                    VERBOSE(VB_IMPORTANT, "VODX: Non Xv mode only supports 16, 24, and 32 bpp displays");
                    errored = true;
                    return;
            }
            
            avpicture_fill(&image_out, picbuf, av_format, XJ_width, XJ_height);
            image_out.linesize[0] = stride;

            avpicture_fill(&image_in, buffer->buf, PIX_FMT_YUV420P, XJ_width, XJ_height);

            img_convert(&image_out, av_format, &image_in, PIX_FMT_YUV420P, XJ_width, XJ_height);


        }
    
        DirectXUnlockSurface();
    }
    else
        VERBOSE(VB_IMPORTANT, "Could not lock surface!");    
}

void VideoOutputDX::Show(FrameScanType )
{
    HRESULT dxresult;

    if (IsErrored())
    {
        VERBOSE(VB_IMPORTANT, "VideoOutputDX::Show() called while IsErrored istrue.");
        return;
    }

    if ((display == NULL))
    {
        VERBOSE(VB_IMPORTANT, "no display!!");
        return;
    }

    /* Our surface can be lost so be sure to check this
     * and restore it if need be */
    if (IDirectDrawSurface2_IsLost(display) == DDERR_SURFACELOST)
    {
        if (IDirectDrawSurface2_Restore(display) == DD_OK && using_overlay)
            DirectXUpdateOverlay();
    }

    if (!using_overlay)
    {
        DDBLTFX ddbltfx;
        RECT rect_src;
        RECT rect_dest;
        
        rect_src.left   = video_rect.left();
        rect_src.right  = XJ_width;
        rect_src.top    = video_rect.top();
        rect_src.bottom = XJ_height;

        if (display_video_rect.left()  < display_visible_rect.left() ||
            display_video_rect.right() > display_visible_rect.right())
        {
            rect_dest.left   = display_visible_rect.left();
            rect_dest.right  = display_visible_rect.right();

            int diff_x  = display_visible_rect.left();
            diff_x     -= display_video_rect.left();
            int diff_w  = display_video_rect.width();
            diff_x     -= display_visible_rect.width();

            rect_src.left  += (XJ_width * diff_x) / display_video_rect.width();
            rect_src.right -= ((XJ_width * (diff_w - diff_x)) /
                               display_video_rect.width());
        }
        else
        {
            rect_dest.left  = display_video_rect.left();
            rect_dest.right = display_video_rect.right();
        }

        if (display_video_rect.top()    < display_visible_rect.top() ||
            display_video_rect.bottom() > display_visible_rect.bottom())
        {
            rect_dest.top    = display_visible_rect.top();
            rect_dest.bottom = display_visible_rect.bottom();

            int diff_y  = display_visible_rect.top();
            diff_y     -= display_video_rect.top();
            int diff_h  = display_video_rect.height();
            diff_h     -= display_visible_rect.height();

            rect_src.top += (XJ_height * diff_y) / display_video_rect.height();
            rect_src.bottom -= ((XJ_height * (diff_h - diff_y)) /
                                display_video_rect.height());
        }
        else
        {
            rect_dest.top    = display_video_rect.top();
            rect_dest.bottom = display_video_rect.bottom();
        }

        /* We ask for the "NOTEARING" option */
        memset(&ddbltfx, 0, sizeof(DDBLTFX));
        ddbltfx.dwSize = sizeof(DDBLTFX);
        ddbltfx.dwDDFX = DDBLTFX_NOTEARING;

        /* Blit video surface to display */
        dxresult = IDirectDrawSurface2_Blt(display, &rect_dest, back_surface,
                                            &rect_src, DDBLT_ASYNC, &ddbltfx);
        if (dxresult != DD_OK)
        {
            VERBOSE(VB_IMPORTANT, "could not blit surface (error " << hex << dxresult << dec << ")");
            return;
        }

    }
    else /* using overlay */
    {
        /* Flip the overlay buffers if we are using back buffers */
        if (front_surface == back_surface)
        {
            return;
        }

        dxresult = IDirectDrawSurface2_Flip(front_surface, NULL, DDFLIP_WAIT);
        if (dxresult != DD_OK)
        {
            VERBOSE(VB_IMPORTANT, "could not flip overlay (error " << hex << dxresult << dec << ")");
        }

        /* set currently displayed pic */
        //p_vout->p_sys->p_current_surface = p_pic->p_sys->p_front_surface;

    }
}

void VideoOutputDX::DrawUnusedRects(bool)
{
}

void VideoOutputDX::UpdatePauseFrame(void)
{
    VideoFrame *pauseb = vbuffers.GetScratchFrame();
    VideoFrame *pauseu = vbuffers.head(kVideoBuffer_used);
    if (pauseu)
        memcpy(pauseFrame.buf, pauseu->buf, pauseu->size);
    else
        memcpy(pauseFrame.buf, pauseb->buf, pauseb->size);
}

void VideoOutputDX::ProcessFrame(VideoFrame *frame, OSD *osd,
                                   FilterChain *filterList,
                                   NuppelVideoPlayer *pipPlayer)
{
    if (IsErrored())
    {
        VERBOSE(VB_IMPORTANT, "VideoOutputDX::ProcessFrame() called while IsErrored is true.");
        return;
    }

    if (!frame)
    {
        frame = vbuffers.GetScratchFrame();
        CopyFrame(vbuffers.GetScratchFrame(), &pauseFrame);
    }

    if (m_deinterlacing && m_deintFilter != NULL)
	m_deintFilter->ProcessFrame(frame);
    if (filterList)
        filterList->ProcessFrame(frame);

    ShowPip(frame, pipPlayer);
    DisplayOSD(frame, osd);
}

// this is documented in videooutbase.cpp
int VideoOutputDX::SetPictureAttribute(
    PictureAttribute attribute, int newValue)
{
    if (ccontrol == NULL)
        return -1;
        
    newValue = min(max(newValue, 0), 100);
        
    DDCOLORCONTROL ddcc;
    memset(&ddcc, 0, sizeof(DDCOLORCONTROL));
    ddcc.dwSize = sizeof(DDCOLORCONTROL);
        
    switch (attribute)
    {
        case kPictureAttribute_Brightness:
            ddcc.dwFlags = DDCOLOR_BRIGHTNESS;
            ddcc.lBrightness = (newValue * newValue * 17) / 10 - 70 * newValue;
            break;
        case kPictureAttribute_Contrast:
            ddcc.dwFlags = DDCOLOR_CONTRAST;
            ddcc.lContrast = newValue * 200;
            break;
        case kPictureAttribute_Colour:
            ddcc.dwFlags = DDCOLOR_SATURATION;
            ddcc.lSaturation = newValue * 200;
            break;
        case kPictureAttribute_Hue:
            ddcc.dwFlags = DDCOLOR_HUE;
            ddcc.lHue = newValue * 36 / 10;
            if (ddcc.lHue > 180)
                ddcc.lHue -= 361;
            break;
    }
    
    HRESULT dxresult;
    
    dxresult = IDirectDrawColorControl_SetColorControls(ccontrol, &ddcc);
    
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "Could not update colour controls: "
                << hex << dxresult << dec);
        return -1;
    }

    SetPictureAttributeDBValue(attribute, newValue);
    
    return newValue;
}

float VideoOutputDX::GetDisplayAspect(void) const
{
    float width  = display_visible_rect.width();
    float height = display_visible_rect.height();

    if (height <= 0.0001f)
        return 4.0f / 3.0f;

    return width / height;
}

static const DWORD pref_chromas[] = { FOURCC_IYUV,
                                      FOURCC_I420,
                                      FOURCC_YV12,
//                                      FOURCC_UYVY,
//                                      FOURCC_UYNV,
//                                      FOURCC_Y422,
                                      FOURCC_YUY2,
                                      FOURCC_YUYV,
                                      FOURCC_YUNV,
//                                      FOURCC_YVYU,
                                      FOURCC_RGBX,
                                        0xFFFFFFFF };

void VideoOutputDX::MakeSurface()
{
    outputpictures = 0;
    
    if (using_overlay)
    {
        for (int i = 0; !outputpictures && (pref_chromas[i] != 0xFFFFFFFF); i++)
        {
            chroma = pref_chromas[i];

            NewPicture();
        }
    }

    if (!outputpictures)
    {
        /* If it didn't work then don't try to use an overlay */
        using_overlay = false;
        for (int i = 0; !outputpictures && (pref_chromas[i] != 0xFFFFFFFF); i++)
        {
            chroma = pref_chromas[i];

            NewPicture();
        }
    }
    
    if (!outputpictures)
    {
        /* If it _still_ didn't work then don't try to use vidmem */
        use_sysmem = true;
        for (int i = 0; !outputpictures && (pref_chromas[i] != 0xFFFFFFFF); i++)
        {
            chroma = pref_chromas[i];

            NewPicture();
        }
    }
}




typedef HRESULT (WINAPI *LPFNDDC)(GUID *,LPDIRECTDRAW *,IUnknown *);
typedef HRESULT (WINAPI *LPFNDDEE)(LPDDENUMCALLBACKEXA, LPVOID, DWORD);


/*****************************************************************************
 * DirectXInitDDraw: Takes care of all the DirectDraw initialisations
 *****************************************************************************
 * This function initialise and allocate resources for DirectDraw.
 *****************************************************************************/
int VideoOutputDX::DirectXInitDDraw()
{
    HRESULT dxresult;
    LPFNDDC OurDirectDrawCreate;
    LPFNDDEE OurDirectDrawEnumerateEx;
    LPDIRECTDRAW p_ddobject;

    VERBOSE(VB_IMPORTANT, "DirectXInitDDraw");

    /* Load direct draw DLL */
    ddraw_dll = LoadLibrary("DDRAW.DLL");
    if (ddraw_dll == NULL)
    {
        VERBOSE(VB_IMPORTANT, "DirectXInitDDraw failed loading ddraw.dll");
        goto error;
    }

    OurDirectDrawCreate =
      (LPFNDDC)GetProcAddress(ddraw_dll, "DirectDrawCreate");
    if (OurDirectDrawCreate == NULL )
    {
        VERBOSE(VB_IMPORTANT, "DirectXInitDDraw failed GetProcAddress");
        goto error;
    }

    OurDirectDrawEnumerateEx =
      (LPFNDDEE)GetProcAddress(ddraw_dll, "DirectDrawEnumerateExA");

    if (OurDirectDrawEnumerateEx && MonitorFromWindow )
    {
        monitor = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);

        /* Enumerate displays * /
        OurDirectDrawEnumerateEx( DirectXEnumCallback, p_vout, 
                                  DDENUM_ATTACHEDSECONDARYDEVICES );*/
    }

    /* Initialize DirectDraw now */
    dxresult = OurDirectDrawCreate(display_driver, &p_ddobject, NULL);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "DirectXInitDDraw cannot initialize DDraw");
        goto error;
    }

    /* Get the IDirectDraw2 interface */
    dxresult = IDirectDraw_QueryInterface(p_ddobject, IID_IDirectDraw2, (LPVOID *) &ddobject);
    /* Release the unused interface */
    IDirectDraw_Release(p_ddobject);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot get IDirectDraw2 interface" );
        goto error;
    }

    /* Set DirectDraw Cooperative level, ie what control we want over Windows
     * display */
    dxresult = IDirectDraw2_SetCooperativeLevel(ddobject, wnd, DDSCL_NORMAL);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot set direct draw cooperative level");
        goto error;
    }

    /* Get the size of the current display device */
    if (monitor && GetMonitorInfo)
    {
        MONITORINFO monitor_info;
        monitor_info.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(monitor, &monitor_info);
        rect_display = monitor_info.rcMonitor;
    }
    else
    {
        rect_display.left = 0;
        rect_display.top = 0;
        rect_display.right  = GetSystemMetrics(SM_CXSCREEN);
        rect_display.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    VERBOSE(VB_IMPORTANT, "screen dimensions ("
              << rect_display.left << "x"
              << rect_display.top << ","
              << rect_display.right << "x"
              << rect_display.bottom << ")");

    /* Probe the capabilities of the hardware */
    DirectXGetDDrawCaps();

    VERBOSE(VB_IMPORTANT, "End DirectXInitDDraw");
    return 0;

 error:
    if (ddobject)
        IDirectDraw2_Release(ddobject);
    if (ddraw_dll)
        FreeLibrary(ddraw_dll);
    ddraw_dll = NULL;
    ddobject = NULL;
    return -1;
}

/*****************************************************************************
 * DirectXCreateClipper: Create a clipper that will be used when blitting the
 *                       RGB surface to the main display.
 *****************************************************************************
 * This clipper prevents us to modify by mistake anything on the screen
 * which doesn't belong to our window. For example when a part of our video
 * window is hidden by another window.
 *****************************************************************************/
int VideoOutputDX::DirectXCreateClipper()
{
    HRESULT dxresult;

    VERBOSE(VB_IMPORTANT, "DirectXCreateClipper");

    /* Create the clipper */
    dxresult = IDirectDraw2_CreateClipper(ddobject, 0, &clipper, NULL);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot create clipper (error " << hex << dxresult << dec << ")");
        goto error;
    }

    /* Associate the clipper to the window */
    dxresult = IDirectDrawClipper_SetHWnd(clipper, 0, wnd);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot attach clipper to window (error " << hex << dxresult << dec << ")");
        goto error;
    }

    /* associate the clipper with the surface */
    dxresult = IDirectDrawSurface_SetClipper(display, clipper);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot attach clipper to surface (error " << hex << dxresult << dec << ")");
        goto error;
    }

    return 0;

 error:
    if (clipper)
    {
        IDirectDrawClipper_Release(clipper);
    }
    clipper = NULL;
    return -1;
}

/*****************************************************************************
 * DirectXCreateDisplay: create the DirectDraw display.
 *****************************************************************************
 * Create and initialize display according to preferences specified in the vout
 * thread fields.
 *****************************************************************************/
int VideoOutputDX::DirectXCreateDisplay()
{
    HRESULT              dxresult;
    DDSURFACEDESC        ddsd;
    LPDIRECTDRAWSURFACE  p_display;

    VERBOSE(VB_IMPORTANT, "DirectXCreateDisplay");

    /* Now get the primary surface. This surface is what you actually see
     * on your screen */
    memset(&ddsd, 0, sizeof(DDSURFACEDESC));
    ddsd.dwSize = sizeof(DDSURFACEDESC);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    dxresult = IDirectDraw2_CreateSurface(ddobject, &ddsd, &p_display, NULL );
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot get primary surface (error %li)" << dxresult);
        return -1;
    }

    dxresult = IDirectDrawSurface_QueryInterface(p_display, IID_IDirectDrawSurface2, (LPVOID *) &display);
    /* Release the old interface */
    IDirectDrawSurface_Release(p_display);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot query IDirectDrawSurface2 interface " <<
                         "(error %li)" << dxresult);
        return -1;
    }

    /* The clipper will be used only in non-overlay mode */
    DirectXCreateClipper();

    /* Make sure the colorkey will be painted */
    colorkey = 0;
    rgb_colorkey = DirectXFindColorkey(colorkey);

    VERBOSE(VB_IMPORTANT, "colour key = " << hex << rgb_colorkey << dec);

    VERBOSE(VB_IMPORTANT, "brushing");

    /* Create the actual brush */
    SetClassLong(wnd, GCL_HBRBACKGROUND,
                  (LONG)CreateSolidBrush(rgb_colorkey));
    InvalidateRect(wnd, NULL, TRUE);
    //DirectXUpdateRects(true);

    VERBOSE(VB_IMPORTANT, "display created");

    return 0;
}

/*****************************************************************************
 * DirectXCreateSurface: create an YUV overlay or RGB surface for the video.
 *****************************************************************************
 * The best method of display is with an YUV overlay because the YUV->RGB
 * conversion is done in hardware.
 * You can also create a plain RGB surface.
 * ( Maybe we could also try an RGB overlay surface, which could have hardware
 * scaling and which would also be faster in window mode because you don't
 * need to do any blitting to the main display...)
 *****************************************************************************/
int VideoOutputDX::DirectXCreateSurface(LPDIRECTDRAWSURFACE2 *pp_surface_final,
                                 DWORD i_chroma, bool b_overlay, int i_backbuffers)
{
    HRESULT dxresult;
    LPDIRECTDRAWSURFACE p_surface;
    DDSURFACEDESC ddsd;

    /* Create the video surface */
    if (b_overlay)
    {
        /* Now try to create the YUV overlay surface.
         * This overlay will be displayed on top of the primary surface.
         * A color key is used to determine whether or not the overlay will be
         * displayed, ie the overlay will be displayed in place of the primary
         * surface wherever the primary surface will have this color.
         * The video window has been created with a background of this color so
         * the overlay will be only displayed on top of this window */

        memset(&ddsd, 0, sizeof(DDSURFACEDESC));
        ddsd.dwSize = sizeof(DDSURFACEDESC);
        ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
        ddsd.ddpfPixelFormat.dwFourCC = i_chroma;
        ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
        ddsd.dwFlags |= (i_backbuffers ? DDSD_BACKBUFFERCOUNT : 0);
        ddsd.ddsCaps.dwCaps = DDSCAPS_OVERLAY | DDSCAPS_VIDEOMEMORY;
        ddsd.ddsCaps.dwCaps |= (i_backbuffers ? DDSCAPS_COMPLEX | DDSCAPS_FLIP : 0);
        ddsd.dwHeight = XJ_height;
        ddsd.dwWidth = XJ_width;
        ddsd.dwBackBufferCount = i_backbuffers;
    }
    else  // !b_overlay
    {
        bool b_rgb_surface = (i_chroma == FOURCC_RGB2)
            || (i_chroma == FOURCC_RV15) || (i_chroma == FOURCC_RV16)
            || (i_chroma == FOURCC_RV24) || (i_chroma == FOURCC_RV32);

        memset(&ddsd, 0, sizeof(DDSURFACEDESC));
        ddsd.dwSize = sizeof(DDSURFACEDESC);
        ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        ddsd.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_CAPS;
        ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        ddsd.dwHeight = XJ_height;
        ddsd.dwWidth = XJ_width;

        if (use_sysmem)
            ddsd.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
        else
            ddsd.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY;

        if (!b_rgb_surface)
        {
            ddsd.dwFlags |= DDSD_PIXELFORMAT;
            ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
            ddsd.ddpfPixelFormat.dwFourCC = i_chroma;
        }
    }

    VERBOSE(VB_IMPORTANT, "VideoOutputDX::DirectXCreateSurface() x: "
                          << XJ_width << " y: " << XJ_height
                          << " chrom: " << fourcc_str(i_chroma));

    dxresult = IDirectDraw2_CreateSurface(ddobject, &ddsd, &p_surface, NULL );
    if (dxresult != DD_OK )
    {
        VERBOSE(VB_IMPORTANT, "DD_CreateSurface failed " << hex << dxresult << dec);
        *pp_surface_final = NULL;
        return -1;
    }

    /* Now that the surface is created, try to get a newer DirectX interface */
    dxresult = IDirectDrawSurface_QueryInterface(p_surface, IID_IDirectDrawSurface2, (LPVOID *) pp_surface_final);
    IDirectDrawSurface_Release(p_surface);    /* Release the old interface */
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot query IDirectDrawSurface2 interface " <<
                         "(error " << hex << dxresult << dec << ")");
        *pp_surface_final = NULL;
        return -1;
    }

    if (b_overlay)
    {
        back_surface = *pp_surface_final;

        /* Reset the front buffer memory */
        char *picbuf;
        int stride;
        // TODO: fix
        if (DirectXLockSurface((void **) &picbuf, &stride) == DD_OK)
        {
            memset(picbuf, 127, stride * XJ_height * 3 / 2);

            DirectXUnlockSurface();
        }

        back_surface = NULL;

        /* Check the overlay is useable as some graphics cards allow creating
         * several overlays but only one can be used at one time. */
        front_surface = *pp_surface_final;
        if (DirectXUpdateOverlay() != 0)
        {
            IDirectDrawSurface2_Release(*pp_surface_final);
            *pp_surface_final = NULL;
            front_surface = NULL;
            VERBOSE(VB_IMPORTANT, "overlay unuseable (might already be in use)" );
            return -1;
        }
    }

    /* Get the IDirectDraw2 interface */
    dxresult = IDirectDraw_QueryInterface(*pp_surface_final, IID_IDirectDrawColorControl, (LPVOID *) &ccontrol);
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "cannot get colour control interface" );
        ccontrol = NULL;
    }

    return 0;
}


/*****************************************************************************
 * DirectXCloseDDraw: Release the DDraw object allocated by DirectXInitDDraw
 *****************************************************************************
 * This function returns all resources allocated by DirectXInitDDraw.
 *****************************************************************************/
void VideoOutputDX::DirectXCloseDDraw()
{
    VERBOSE(VB_IMPORTANT, "DirectXCloseDDraw");
    if (ddobject != NULL)
    {
        IDirectDraw2_Release(ddobject);
        ddobject = NULL;
    }

    if (ddraw_dll != NULL)
    {
        FreeLibrary(ddraw_dll);
        ddraw_dll = NULL;
    }

    if (display_driver != NULL)
    {
        free(display_driver);
        display_driver = NULL;
    }

    monitor = NULL;
}

/*****************************************************************************
 * DirectXCloseDisplay: close and reset the DirectX display device
 *****************************************************************************
 * This function returns all resources allocated by DirectXCreateDisplay.
 *****************************************************************************/
void VideoOutputDX::DirectXCloseDisplay()
{
    VERBOSE(VB_IMPORTANT, "DirectXCloseDisplay");

    if (clipper != NULL)
    {
        VERBOSE(VB_IMPORTANT, "DirectXCloseDisplay clipper");
        IDirectDrawClipper_Release(clipper);
        clipper = NULL;
    }

    if (display != NULL)
    {
        VERBOSE(VB_IMPORTANT, "DirectXCloseDisplay display");
        IDirectDrawSurface2_Release(display);
        display = NULL;
    }
}


/*****************************************************************************
 * DirectXCloseSurface: close the YUV overlay or RGB surface.
 *****************************************************************************
 * This function returns all resources allocated for the surface.
 *****************************************************************************/
void VideoOutputDX::DirectXCloseSurface()
{
    VERBOSE(VB_IMPORTANT, "DirectXCloseSurface");
    
    if (ccontrol != NULL)
    {
        IDirectDrawColorControl_Release(ccontrol);
        ccontrol = NULL;
    }
    
    if (front_surface != NULL)
    {
        IDirectDrawSurface2_Release(front_surface);
        front_surface = NULL;
    }
}


/*****************************************************************************
 * NewPictureVec: allocate a vector of identical pictures
 *****************************************************************************
 * Returns 0 on success, -1 otherwise
 *****************************************************************************/
int VideoOutputDX::NewPicture()
{
    int i;
    int i_ret = -1;
    LPDIRECTDRAWSURFACE2 p_surface;

    VERBOSE(VB_IMPORTANT, "NewPicture overlay");

    outputpictures = 0;

//    using_overlay = true;
    overlay_3buf = true;

    /* First we try to use an YUV overlay surface.
     * The overlay surface that we create won't be used to decode directly
     * into it because accessing video memory directly is way to slow (remember
     * that pictures are decoded macroblock per macroblock). Instead the video
     * will be decoded in picture buffers in system memory which will then be
     * memcpy() to the overlay surface. */
    if (using_overlay)
    {
        /* Triple buffering rocks! it doesn't have any processing overhead
         * (you don't have to wait for the vsync) and provides for a very nice
         * video quality (no tearing). */
        if (overlay_3buf)
            i_ret = DirectXCreateSurface(&p_surface, chroma, using_overlay,
                                          2 /* number of backbuffers */ );

        if (!overlay_3buf || i_ret != 0)
        {
            /* Try to reduce the number of backbuffers */
            i_ret = DirectXCreateSurface(&p_surface, chroma, using_overlay,
                                          0 /* number of backbuffers */ );
        }

        if (i_ret == 0)
        {
            DDSCAPS dds_caps;

            /* set front buffer */
            front_surface = p_surface;

            /* Get the back buffer */
            memset(&dds_caps, 0, sizeof( DDSCAPS));
            dds_caps.dwCaps = DDSCAPS_BACKBUFFER;
            if (DD_OK != IDirectDrawSurface2_GetAttachedSurface(p_surface, &dds_caps, &back_surface))
            {
                VERBOSE(VB_IMPORTANT, "NewPicture could not get back buffer");
                /* front buffer is the same as back buffer */
                back_surface = p_surface;
            }

            DirectXUpdateOverlay();
            outputpictures = 1;
            VERBOSE(VB_IMPORTANT, "YUV overlay created successfully");
        }
    }

    /* As we can't have an overlay, we'll try to create a plain offscreen
     * surface. This surface will reside in video memory because there's a
     * better chance then that we'll be able to use some kind of hardware
     * acceleration like rescaling, blitting or YUV->RGB conversions.
     * We then only need to blit this surface onto the main display when we
     * want to display it */
    if (!using_overlay )
    {
        if (chroma != FOURCC_RGBX)
        {
            DWORD i_codes;
            DWORD *pi_codes;
            bool b_result = false;

            bool is_rgb = ((chroma & 0xFF) == 'R');

            /* Check if the chroma is supported first. This is required
             * because a few buggy drivers don't mind creating the surface
             * even if they don't know about the chroma. */
            if (!is_rgb && IDirectDraw2_GetFourCCCodes(ddobject, &i_codes, NULL) == DD_OK)
            {
                pi_codes = (DWORD*) malloc(i_codes * sizeof(DWORD));
                if (pi_codes && IDirectDraw2_GetFourCCCodes(ddobject, &i_codes, pi_codes) == DD_OK)
                {
                    for( i = 0; i < (int)i_codes; i++ )
                    {
                        if (chroma == pi_codes[i])
                        {
                            b_result = true;
                            break;
                        }
                    }
                }
            }

            if (is_rgb || b_result)
                i_ret = DirectXCreateSurface(&p_surface, chroma,
                                              0 /* no overlay */,
                                              0 /* no back buffers */ );
        }
        else
        {
            /* Our last choice is to use a plain RGB surface */
            DDPIXELFORMAT ddpfPixelFormat;

            ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
            IDirectDrawSurface2_GetPixelFormat(display, &ddpfPixelFormat);

            if (ddpfPixelFormat.dwFlags & DDPF_RGB)
            {
                switch(ddpfPixelFormat.dwRGBBitCount)
                {
//                case 8: /* FIXME: set the palette */
//                    chroma = FOURCC_RGB2;
//                    break;
                case 15:
                    chroma = FOURCC_RV15;
                    break;
                case 16:
                    chroma = FOURCC_RV16;
                    break;
                case 24:
                    chroma = FOURCC_RV24;
                    break;
                case 32:
                    chroma = FOURCC_RV32;
                    break;
                default:
                    VERBOSE(VB_IMPORTANT, "unknown screen depth");
                    return 0;
                }
            }

//            hw_yuv = false;

            i_ret = DirectXCreateSurface(&p_surface, chroma,
                                          0 /* no overlay */,
                                          0 /* no back buffers */);

        }

        if (i_ret == 0)
        {
            /* Allocate internal structure */
 
            front_surface = back_surface = p_surface;

            outputpictures = 1;

            VERBOSE(VB_IMPORTANT, "created plain surface");
        }
    }

    if (outputpictures) {
        /* Now that we've got all our direct-buffers, we can finish filling in the
         * picture_t structures */
        if (DirectXLockSurface(NULL, NULL) != 0)
        {
            /* AAARRGG */
            outputpictures = 0;
            VERBOSE(VB_IMPORTANT, "cannot lock surface");
            return -1;
        }
        DirectXUnlockSurface();
    }

    VERBOSE(VB_IMPORTANT, "End NewPictureVec (" <<
             (outputpictures ? "succeeded" : "failed") << ")");

    return 0;
}

/*****************************************************************************
 * DirectXGetDDrawCaps: Probe the capabilities of the hardware
 *****************************************************************************
 * It is nice to know which features are supported by the hardware so we can
 * find ways to optimize our rendering.
 *****************************************************************************/
void VideoOutputDX::DirectXGetDDrawCaps()
{
    DDCAPS ddcaps;
    HRESULT dxresult;

    /* This is just an indication of whether or not we'll support overlay,
     * but with this test we don't know if we support YUV overlay */
    memset(&ddcaps, 0, sizeof(DDCAPS));
    ddcaps.dwSize = sizeof(DDCAPS);
    dxresult = IDirectDraw2_GetCaps(ddobject, &ddcaps, NULL);
    if (dxresult != DD_OK )
    {
        VERBOSE(VB_IMPORTANT, "cannot get caps");
    }
    else
    {
        BOOL bHasOverlay, bHasOverlayFourCC, bCanDeinterlace,
             bHasColorKey, bCanStretch, bCanBltFourcc;

        /* Determine if the hardware supports overlay surfaces */
        bHasOverlay = ((ddcaps.dwCaps & DDCAPS_OVERLAY) ==
                       DDCAPS_OVERLAY) ? TRUE : FALSE;
        /* Determine if the hardware supports overlay surfaces */
        bHasOverlayFourCC = ((ddcaps.dwCaps & DDCAPS_OVERLAYFOURCC) ==
                       DDCAPS_OVERLAYFOURCC) ? TRUE : FALSE;
        /* Determine if the hardware supports overlay deinterlacing */
        bCanDeinterlace = ((ddcaps.dwCaps & DDCAPS2_CANFLIPODDEVEN) ==
                       0 ) ? TRUE : FALSE;
        /* Determine if the hardware supports colorkeying */
        bHasColorKey = ((ddcaps.dwCaps & DDCAPS_COLORKEY) ==
                        DDCAPS_COLORKEY) ? TRUE : FALSE;
        /* Determine if the hardware supports scaling of the overlay surface */
        bCanStretch = ((ddcaps.dwCaps & DDCAPS_OVERLAYSTRETCH) ==
                       DDCAPS_OVERLAYSTRETCH) ? TRUE : FALSE;
        /* Determine if the hardware supports color conversion during a blit */
        bCanBltFourcc = ((ddcaps.dwCaps & DDCAPS_BLTFOURCC ) ==
                        DDCAPS_BLTFOURCC) ? TRUE : FALSE;

        VERBOSE(VB_IMPORTANT, "DirectDraw Capabilities: overlay=" << bHasOverlay
                        << " yuvoverlay=" << bHasOverlayFourCC
                        << " can_deinterlace_overlay=" << bCanDeinterlace
                        << " colorkey=" << bHasColorKey
                        << " stretch=" << bCanStretch
                        << " bltfourcc=" << bCanBltFourcc);

        /* Don't ask for troubles */
//        if (!bCanBltFourcc) hw_yuv = true; 
    }
}

/*****************************************************************************
 * DirectXFindColorkey: Finds out the 32bits RGB pixel value of the colorkey
 *****************************************************************************/
DWORD VideoOutputDX::DirectXFindColorkey(uint32_t i_color)
{
    DDSURFACEDESC ddsd;
    HRESULT dxresult;
    COLORREF i_rgb = 0;
    uint32_t i_pixel_backup;
    HDC hdc;

    VERBOSE(VB_IMPORTANT, "determining colour key");

    ddsd.dwSize = sizeof(ddsd);
    dxresult = IDirectDrawSurface2_Lock(display, NULL, &ddsd, DDLOCK_WAIT, NULL );
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT, "surface lock failed: 0x" << hex << dxresult << dec);
        return 0;
    }

    VERBOSE(VB_IMPORTANT, "surface locked");

    i_pixel_backup = *(uint32_t *)ddsd.lpSurface;

    switch(ddsd.ddpfPixelFormat.dwRGBBitCount)
    {
    case 4:
        *(uint8_t *)ddsd.lpSurface = i_color;
        break;
    case 8:
        *(uint8_t *)ddsd.lpSurface = i_color;
        break;
    case 16:
        *(uint16_t *)ddsd.lpSurface = i_color;
        break;
    default:
        *(uint32_t *)ddsd.lpSurface = i_color;
        break;
    }

    IDirectDrawSurface2_Unlock(display, NULL);

    VERBOSE(VB_IMPORTANT, "surface unlocked");

    if (IDirectDrawSurface2_GetDC(display, &hdc) == DD_OK )
    {
        i_rgb = GetPixel(hdc, 0, 0);
        IDirectDrawSurface2_ReleaseDC(display, hdc);
    }

    ddsd.dwSize = sizeof(ddsd);
    dxresult = IDirectDrawSurface2_Lock(display, NULL, &ddsd, DDLOCK_WAIT, NULL );
    if (dxresult != DD_OK )
    {
        VERBOSE(VB_IMPORTANT, "surface lock failed: 0x" << hex << dxresult << dec);
        return i_rgb;
    }

    VERBOSE(VB_IMPORTANT, "surface locked");

    *(uint32_t *)ddsd.lpSurface = i_pixel_backup;

    IDirectDrawSurface2_Unlock(display, NULL);

    VERBOSE(VB_IMPORTANT, "surface unlocked");

    return i_rgb;
}

/*****************************************************************************
 * DirectXUpdateOverlay: Move or resize overlay surface on video display.
 *****************************************************************************
 * This function is used to move or resize an overlay surface on the screen.
 * Ususally the overlay is moved by the user and thus, by a move or resize
 * event (in Manage).
 *****************************************************************************/
int VideoOutputDX::DirectXUpdateOverlay()
{
    DDOVERLAYFX     ddofx;
    DWORD           dwFlags;
    HRESULT         dxresult;

    /* Coordinates of src and dest images (used when blitting to display) */
    RECT         rect_src;
    RECT         rect_dest;

    if (front_surface == NULL || !using_overlay )
        return -1;

    /* The new window dimensions should already have been computed by the
     * caller of this function */

    rect_src.left   = video_rect.left();
    rect_src.right  = video_rect.right();
    rect_src.top    = video_rect.top();
    rect_src.bottom = video_rect.bottom();

    if (display_video_rect.left()  < display_visible_rect.left() ||
        display_video_rect.right() > display_visible_rect.right())
    {
        rect_dest.left  = display_visible_rect.left();
        rect_dest.right = display_visible_rect.right();

        int diff_x  = display_visible_rect.left();
        diff_x     -= display_video_rect.left();
        int diff_w  = display_video_rect.width();
        diff_x     -= display_visible_rect.width();

        rect_src.left  += (XJ_width * diff_x) / display_video_rect.width();
        rect_src.right -= ((XJ_width * (diff_w - diff_x)) /
                           display_video_rect.width());
    }
    else
    {
        rect_dest.left  = display_video_rect.left();
        rect_dest.right = display_video_rect.right();
    }
    
    if (display_video_rect.top()    < display_visible_rect.top() ||
        display_video_rect.bottom() > display_visible_rect.bottom())
    {
        rect_dest.top    = display_visible_rect.top();
        rect_dest.bottom = display_visible_rect.bottom();

        int diff_y  = display_visible_rect.top();
        diff_y     -= display_video_rect.top();
        int diff_h  = display_video_rect.height();
        diff_h     -= display_visible_rect.height();

        rect_src.top += ((video_rect.width() * diff_y) /
                         display_video_rect.height());

        rect_src.bottom -= (video_rect.height() * (diff_h - diff_y) /
                            display_video_rect.height());
    }
    else
    {
        rect_dest.top    = display_video_rect.top();
        rect_dest.bottom = display_video_rect.bottom();
    }

    VERBOSE(VB_IMPORTANT, "rect_src ("
              << rect_src.left << "x"
              << rect_src.top << ","
              << rect_src.right << "x"
              << rect_src.bottom << ")");
    
    VERBOSE(VB_IMPORTANT, "rect_dest ("
              << rect_dest.left << "x"
              << rect_dest.top << ","
              << rect_dest.right << "x"
              << rect_dest.bottom << ")");


    /* Position and show the overlay */
    memset(&ddofx, 0, sizeof(DDOVERLAYFX));
    ddofx.dwSize = sizeof(DDOVERLAYFX);
    ddofx.dckDestColorkey.dwColorSpaceLowValue = colorkey;
    ddofx.dckDestColorkey.dwColorSpaceHighValue = colorkey;

    dwFlags = DDOVER_SHOW | DDOVER_KEYDESTOVERRIDE;

    dxresult = IDirectDrawSurface2_UpdateOverlay(front_surface,
                                         &rect_src,
                                         display,
                                         &rect_dest,
                                         dwFlags, &ddofx );
    if (dxresult != DD_OK)
    {
        VERBOSE(VB_IMPORTANT,
                  "DirectXUpdateOverlay cannot move or resize overlay: " << hex << dxresult << dec);
        return -1;
    }

    return 0;
}

/*****************************************************************************
 * DirectXLockSurface: Lock surface and get picture data pointer
 *****************************************************************************
 * This function locks a surface and get the surface descriptor which amongst
 * other things has the pointer to the picture data.
 *****************************************************************************/
int VideoOutputDX::DirectXLockSurface(void **picbuf, int *stride)
{
    HRESULT dxresult;
    DDSURFACEDESC ddsd;

    /* Lock the surface to get a valid pointer to the picture buffer */
    memset(&ddsd, 0, sizeof(DDSURFACEDESC));
    ddsd.dwSize = sizeof(DDSURFACEDESC);
    dxresult = IDirectDrawSurface2_Lock(back_surface,
                                         NULL, &ddsd,
                                         DDLOCK_NOSYSLOCK | DDLOCK_WAIT,
                                         NULL );
    if (dxresult != DD_OK)
    {
        if (dxresult == DDERR_INVALIDPARAMS)
        {
            /* DirectX 3 doesn't support the DDLOCK_NOSYSLOCK flag, resulting
             * in an invalid params error */
            dxresult = IDirectDrawSurface2_Lock(back_surface, NULL,
                                             &ddsd,
                                             DDLOCK_WAIT, NULL);
        }
        if (dxresult == DDERR_SURFACELOST)
        {
            /* Your surface can be lost so be sure
             * to check this and restore it if needed */

            /* When using overlays with back-buffers, we need to restore
             * the front buffer so the back-buffers get restored as well. */
            if (using_overlay )
                IDirectDrawSurface2_Restore(front_surface);
            else
                IDirectDrawSurface2_Restore(back_surface);

            dxresult = IDirectDrawSurface2_Lock(back_surface, NULL,
                                                 &ddsd,
                                                 DDLOCK_WAIT, NULL);
            if (dxresult == DDERR_SURFACELOST)
                VERBOSE(VB_IMPORTANT, "DirectXLockSurface: DDERR_SURFACELOST");
        }
        if (dxresult != DD_OK)
        {
            return -1;
        }
    }

    if (picbuf)
        *picbuf = ddsd.lpSurface;
        
       if (stride)
           *stride = ddsd.lPitch;
        
    return 0;
}

/*****************************************************************************
 * DirectXUnlockSurface: Unlock a surface locked by DirectXLockSurface().
 *****************************************************************************/
int VideoOutputDX::DirectXUnlockSurface()
{
    /* Unlock the Surface */
    if (IDirectDrawSurface2_Unlock(back_surface, NULL) == DD_OK)
        return 0;
    else
        return -1;
}

QStringList VideoOutputDX::GetAllowedRenderers(
    MythCodecID myth_codec_id, const QSize &video_dim)
{
    QStringList list;
    list += "directx";
    return list;
}

