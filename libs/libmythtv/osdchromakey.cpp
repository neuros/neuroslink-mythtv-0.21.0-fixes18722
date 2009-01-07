#include "osdchromakey.h"

/* Based on xqcam.c by Paul Chinn <loomer@svpal.org> */

// C headers
#include <cmath>

// POSIX headers
#include <sys/ipc.h>
#include <sys/shm.h>

// MythTV headers
#include "osd.h"
#include "osdsurface.h"
#include "osdchromakey.h"

#include "mythcontext.h"
#include "videoout_xv.h"
#include "util-x11.h"

#define LOC QString("OSDChroma: ")
#define LOC_ERR QString("OSDChroma Error: ")

void ChromaKeyOSD::AllocImage(int i)
{
    X11L;
    XImage *shm_img =
        XShmCreateImage(videoOutput->XJ_disp,
                        DefaultVisual(videoOutput->XJ_disp,
                                      videoOutput->XJ_screen_num),
                        videoOutput->XJ_depth, ZPixmap, 0,
                        &shm_infos[i],
                        videoOutput->display_visible_rect.width(),
                        videoOutput->display_visible_rect.height());
    uint size = shm_img->bytes_per_line * (shm_img->height+1) + 128;
    X11U;

    if (shm_img)
    {
        shm_infos[i].shmid = shmget(IPC_PRIVATE, size, IPC_CREAT|0777);
        if (shm_infos[i].shmid >= 0)
        {
            shm_infos[i].shmaddr = (char*) shmat(shm_infos[i].shmid, 0, 0);

            shm_img->data = shm_infos[i].shmaddr;
            shm_infos[i].readOnly = False;

            X11L;
            XShmAttach(videoOutput->XJ_disp, &shm_infos[i]);
            XSync(videoOutput->XJ_disp, False); // needed for FreeBSD?
            X11U;

            // Mark for delete immediately.
            // It won't actually be removed until after we detach it.
            shmctl(shm_infos[i].shmid, IPC_RMID, 0);
        }
    }

    img[i] = shm_img;
    bzero((vf+i), sizeof(VideoFrame));
    vf[i].buf = (unsigned char*) shm_infos[i].shmaddr;
    vf[i].codec  = FMT_ARGB32;
    vf[i].height = videoOutput->display_visible_rect.height();
    vf[i].width  = videoOutput->display_visible_rect.width();
    vf[i].bpp    = 32;
}

void ChromaKeyOSD::FreeImage(int i)
{
    if (!img[i])
        return;

    X11L;
    XShmDetach(videoOutput->XJ_disp, &(shm_infos[i]));
    XFree(img[i]);
    img[i] = NULL;
    X11U;

    if (shm_infos[i].shmaddr)
        shmdt(shm_infos[i].shmaddr);
    if (shm_infos[i].shmid > 0)
        shmctl(shm_infos[0].shmid, IPC_RMID, 0);

    bzero((shm_infos+i), sizeof(XShmSegmentInfo));
    bzero((vf+i),        sizeof(VideoFrame));
}

void ChromaKeyOSD::Reinit(int i)
{
    // Make sure the buffer is the right size...
    QSize new_res(vf[i].width, vf[i].height);
    if (new_res != videoOutput->display_visible_rect.size())
    {
        FreeImage(i);
        AllocImage(i);
    }

    uint key = videoOutput->xv_colorkey;
    uint bpl = img[i]->bytes_per_line;

    // create chroma key line
    char *cln = (char*)av_malloc(bpl + 128);
    bzero(cln, bpl);
    int j  = max(videoOutput->display_video_rect.left() -
                 videoOutput->display_visible_rect.left(), 0);
    int ej = min(videoOutput->display_video_rect.left() +
                 videoOutput->display_video_rect.width(), vf[i].width);
    for (; j < ej; ++j)
        ((uint*)cln)[j] = key;

    // boboff assumes the smallest interlaced resolution is 480 lines - 5%
    int boboff = (int) round(
        ((double)videoOutput->display_video_rect.height()) / 456 - 0.00001);
    boboff = (videoOutput->m_deinterlacing &&
              videoOutput->m_deintfiltername == "bobdeint") ? boboff : 0;

    // calculate beginning and end of chromakey
    int cstart = min(max(videoOutput->display_video_rect.top() + boboff, 0),
                     vf[i].height - 1);
    int cend   = min(max(videoOutput->display_video_rect.top() +
                         videoOutput->display_video_rect.height(), 0),
                     vf[i].height);

    // Paint with borders and chromakey
    char *buf = shm_infos[i].shmaddr;
    int ldispy = min(max(videoOutput->display_visible_rect.top(), 0),
                     vf[i].height - 1);

    VERBOSE(VB_PLAYBACK, LOC + "cstart: "<<cstart<<"  cend: "<<cend);
    VERBOSE(VB_PLAYBACK, LOC + "ldispy: "<<ldispy<<" height: "<<vf[i].height);

    if (cstart > ldispy)
        bzero(buf + (ldispy * bpl), (cstart - ldispy) * bpl);
    for (j = cstart; j < cend; ++j)
        memcpy(buf + (j*bpl), cln, bpl);
    if (cend < vf[i].height)
        bzero(buf + (cend * bpl), (vf[i].height - cend) * bpl);

    av_free(cln);
}

/** \fn ChromaKeyOSD::ProcessOSD(OSD*)
 * 
 *  \return true if we need a repaint, false otherwise
 */
bool ChromaKeyOSD::ProcessOSD(OSD *osd)
{
    OSDSurface *osdsurf = NULL;
    if (osd)
        osdsurf = osd->Display();

    int next = (current+1) & 0x1;
    if (!osdsurf && current >= 0)
    {
        Reset();
        return true;
    }
    else if (!osdsurf || (revision == osdsurf->GetRevision()))
        return false;

    // first create a blank frame with the chroma key
    Reinit(next);

    // then blend the OSD onto it
    unsigned char *buf = (unsigned char*) shm_infos[next].shmaddr;
    osdsurf->BlendToARGB(buf, img[next]->bytes_per_line, vf[next].height,
                         false/*blend_to_black*/, 16);

    // then set it as the current OSD image
    revision = osdsurf->GetRevision();
    current  = next;

    return true;
}

