// Copyright (c) 2005, Daniel Thor Kristjansson
// based on earlier work in MythTV's videout_xvmc.cpp

#ifdef USING_XVMC

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "osdxvmc.h"
#include "videoout_xv.h"

extern "C" XvImage *XvShmCreateImage(Display*, XvPortID, int, char*,
                                     int, int, XShmSegmentInfo*);

#define NO_SUBPICTURE      0
#define OVERLAY_SUBPICTURE 1
#define BLEND_SUBPICTURE   2
#define BACKEND_SUBPICTURE 3

static inline xvmc_render_state_t *GetRender(VideoFrame *frame);

XvMCOSD::XvMCOSD(Display *disp, int port, int surface_type_id,
                 int xvmc_surf_flags)
    : XJ_disp(disp), XJ_width(0), XJ_height(0),
      xv_port(port), osd_palette(NULL), osd_xv_image(NULL),
      osd_subpict_mode(NO_SUBPICTURE), osd_subpict_clear_color(0),
      osd_subpict_alloc(false)
{
    bzero(&osd_subpict, sizeof(osd_subpict));
    // subpicture init
    int num = 0;
    XvImageFormatValues *xvfmv = NULL;
    X11S(xvfmv = XvMCListSubpictureTypes(XJ_disp, xv_port,
                                         surface_type_id, &num));

    for (int i = (xvfmv) ? 0 : num; i < num; i++)
    {
        if (GUID_IA44_PACKED == xvfmv[i].id || GUID_AI44_PACKED == xvfmv[i].id)
        {
            osd_subpict_info  = xvfmv[i];
            bool be = (XVMC_BACKEND_SUBPICTURE & xvmc_surf_flags);
            osd_subpict_mode = (be) ? BACKEND_SUBPICTURE : BLEND_SUBPICTURE;
            break;
        }
    }

    if (xvfmv)
        X11S(XFree(xvfmv));
}

void XvMCOSD::CreateBuffer(XvMCContext &xvmc_ctx, int width, int height)
{
    if (NO_SUBPICTURE == osd_subpict_mode)
    {
        VERBOSE(VB_IMPORTANT, "XvMCOSD::CreateBuffer() failed because "
                "no subpicture is available");
        osd_subpict_alloc = false;
        return;
    }

    XJ_width = width;
    XJ_height = height;

    osd_subpict_clear_color = 0;
    int ret = Success;
    X11S(ret = XvMCCreateSubpicture(XJ_disp, &xvmc_ctx, &osd_subpict,
                                    XJ_width, XJ_height, osd_subpict_info.id));

    if (ret != Success)
    {
        VERBOSE(VB_IMPORTANT, "XvMCOSD::CreateBuffer() failed on XvMCCreateSubpicture");
        osd_subpict_mode = NO_SUBPICTURE;
        osd_subpict_alloc = false;
        return;
    }

    X11L;
    XvMCClearSubpicture(XJ_disp, &osd_subpict, 0, 0, XJ_width,
                        XJ_height, osd_subpict_clear_color);

    osd_xv_image = XvShmCreateImage(XJ_disp, xv_port,
                                    osd_subpict_info.id, NULL,
                                    XJ_width, XJ_height,
                                    &XJ_osd_shm_info);
    X11U;

    if (!osd_xv_image)
    {
        VERBOSE(VB_IMPORTANT, "XvMCOSD::CreateBuffer() failed on XvShmCreateImage");
        osd_subpict_mode = NO_SUBPICTURE;
        osd_subpict_alloc = false;
        return;
    }
    XJ_osd_shm_info.shmid = shmget(IPC_PRIVATE, osd_xv_image->data_size,
                                   IPC_CREAT | 0777);
    XJ_osd_shm_info.shmaddr = (char *)shmat(XJ_osd_shm_info.shmid, 0, 0);
    XJ_osd_shm_info.readOnly = False;

    osd_xv_image->data = XJ_osd_shm_info.shmaddr;

    X11S(XShmAttach(XJ_disp, &XJ_osd_shm_info));

    shmctl(XJ_osd_shm_info.shmid, IPC_RMID, 0);

    if (osd_subpict.num_palette_entries > 0)
    {
        int snum = osd_subpict.num_palette_entries;
        int seb = osd_subpict.entry_bytes;

        osd_palette = new unsigned char[snum * seb];

        for (int i = 0; i < snum; i++)
        {
            int Y = i * (1 << osd_subpict_info.y_sample_bits) / snum;
            int U = 1 << (osd_subpict_info.u_sample_bits - 1);
            int V = 1 << (osd_subpict_info.v_sample_bits - 1);
            for (int j = 0; j < seb; j++)
            {
                switch (osd_subpict.component_order[j]) 
                {
                    case 'U': osd_palette[i * seb + j] = U; break;
                    case 'V': osd_palette[i * seb + j] = V; break;
                    case 'Y':
                    default:  osd_palette[i * seb + j] = Y; break;
                }
            }
        }

        X11S(XvMCSetSubpicturePalette(XJ_disp, &osd_subpict, osd_palette));
    }
    osd_subpict_alloc = true;
}

void XvMCOSD::DeleteBuffer()
{
    if (!osd_subpict_alloc)
        return;

    X11L;
    XvMCDestroySubpicture(XJ_disp, &osd_subpict);

    XShmDetach(XJ_disp, &XJ_osd_shm_info);
    shmdt(XJ_osd_shm_info.shmaddr);

    osd_subpict_alloc = false;
    XFree(osd_xv_image);
    XFlush(XJ_disp);
    X11U;
    usleep(50);

    X11S(XSync(XJ_disp, false));

    if (osd_palette)
        delete [] osd_palette;
}

void XvMCOSD::CompositeOSD(VideoFrame* frame, VideoFrame* osdframe)
{
    if (!osd_subpict_alloc)
        return;

    X11L;
    XvMCCompositeSubpicture(XJ_disp, &osd_subpict, 
                            osd_xv_image, 0, 0,
                            XJ_width, XJ_height, 0, 0);
    // delay sync until after getnextfreeframe...
    XvMCFlushSubpicture(XJ_disp, &osd_subpict);
    X11U;

    if (osd_subpict_mode == BLEND_SUBPICTURE && osdframe)
    {
        xvmc_render_state_t *render = GetRender(frame);
        xvmc_render_state_t *osdren = GetRender(osdframe);

        X11S(XvMCSyncSubpicture(XJ_disp, &osd_subpict));
        VideoOutputXv::SyncSurface(frame);
        
        X11L;
        XvMCBlendSubpicture2(XJ_disp, render->p_surface,
                             osdren->p_surface, &osd_subpict,
                             0, 0, XJ_width, XJ_height,
                             0, 0, XJ_width, XJ_height);
        XvMCFlushSurface(XJ_disp, osdren->p_surface);
        X11U;
    }
    else if (osd_subpict_mode == BACKEND_SUBPICTURE)
    {
        X11L;
        XvMCSyncSubpicture(XJ_disp, &osd_subpict);
        XvMCBlendSubpicture(XJ_disp, GetRender(frame)->p_surface,
                            &osd_subpict, 0, 0, XJ_width,
                            XJ_height, 0, 0, XJ_width, XJ_height);
        XvMCFlushSurface(XJ_disp, GetRender(frame)->p_surface);
        X11U;
    }

}

bool XvMCOSD::NeedFrame()
{
    return osd_subpict_mode == BLEND_SUBPICTURE;
}

bool XvMCOSD::IsValid()
{
    return osd_subpict_mode != NO_SUBPICTURE &&
        osd_subpict_mode != OVERLAY_SUBPICTURE;
}

static inline xvmc_render_state_t *GetRender(VideoFrame *frame)
{
    if (frame)
        return (xvmc_render_state_t*) frame->buf;
    else
        return NULL;
}

#endif // USING_XVMC
