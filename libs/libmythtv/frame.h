#ifndef _FRAME_H
#define _FRAME_H 

#include <string.h>
#include "fourcc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FrameType_
{
    FMT_NONE = -1,
    FMT_RGB24 = 0,
    FMT_YV12,
    FMT_XVMC_IDCT_MPEG2,
    FMT_XVMC_MOCO_MPEG2,
    FMT_VIA_HWSLICE,
    FMT_IA44,
    FMT_AI44,
    FMT_ARGB32,
    FMT_RGBA32,
    FMT_YUV422P,
    FMT_ALPHA,
} VideoFrameType;

typedef struct VideoFrame_
{
    VideoFrameType codec;
    unsigned char *buf;

    int width;
    int height;
    int bpp;
    int size;

    long long frameNumber;
    long long timecode;

    unsigned char *priv[4]; // random empty storage

    unsigned char *qscale_table;
    int            qstride;

    int interlaced_frame; // 1 if interlaced.
    int top_field_first; // 1 if top field is first.
    int repeat_pict;
    int forcekey; // hardware encoded .nuv

    int pitches[3]; // Y, U, & V pitches
    int offsets[3]; // Y, U, & V offsets
} VideoFrame;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static inline void init(
    VideoFrame *vf,
    VideoFrameType _codec, unsigned char *_buf,
    int _width, int _height, int _bpp, int _size,
    const int *p = 0, const int *o = 0) __attribute__ ((unused));
static inline void clear(VideoFrame *vf, int fourcc) __attribute__ ((unused));
static inline bool compatible(
    const VideoFrame *a, const VideoFrame *b) __attribute__ ((unused));

static inline void init(
    VideoFrame *vf,
    VideoFrameType _codec, unsigned char *_buf,
    int _width, int _height, int _bpp, int _size,
    const int *p, const int *o)
{
    vf->codec  = _codec;
    vf->buf    = _buf;
    vf->width  = _width;
    vf->height = _height;

    vf->bpp          = _bpp;
    vf->size         = _size;
    vf->frameNumber  = 0;
    vf->timecode     = 0;

    vf->qscale_table = 0;
    vf->qstride      = 0;

    vf->interlaced_frame = 1;
    vf->top_field_first  = 1;
    vf->repeat_pict      = 0;
    vf->forcekey         = 0;

    // MS Windows doesn't like bzero()..
    memset(vf->priv, 0, 4 * sizeof(unsigned char *));

    if (p)
    {
        memcpy(vf->pitches, p, 3 * sizeof(int));
    }
    else
    {
        if (FMT_YV12 == _codec || FMT_YUV422P == _codec)
        {
            vf->pitches[0] = _width;
            vf->pitches[1] = vf->pitches[2] = _width >> 1;
        }
        else
        {
            vf->pitches[0] = (_width * _bpp) >> 3;
            vf->pitches[1] = vf->pitches[2] = 0;
        }
    }
        
    if (o)
    {
        memcpy(vf->offsets, o, 3 * sizeof(int));
    }
    else
    {
        if (FMT_YV12 == _codec)
        {
            vf->offsets[0] = 0;
            vf->offsets[1] = _width * _height;
            vf->offsets[2] = vf->offsets[1] + (vf->offsets[1] >> 2);
        }
        else if (FMT_YUV422P == _codec)
        {
            vf->offsets[0] = 0;
            vf->offsets[1] = _width * _height;
            vf->offsets[2] = vf->offsets[1] + (vf->offsets[1] >> 1);
        }
        else
        {
            vf->offsets[0] = vf->offsets[1] = vf->offsets[2] = 0;
        }
    }
}

static inline void clear(VideoFrame *vf, int fourcc)
{
    if (!vf)
        return;

    if ((GUID_I420_PLANAR == fourcc) || (GUID_IYUV_PLANAR == fourcc) ||
        (GUID_YV12_PLANAR == fourcc))
    {
        int uv_height = vf->height >> 1;
        // MS Windows doesn't like bzero()..
        memset(vf->buf + vf->offsets[0],   0, vf->pitches[0] * vf->height);
        memset(vf->buf + vf->offsets[1], 127, vf->pitches[1] * uv_height);
        memset(vf->buf + vf->offsets[2], 127, vf->pitches[2] * uv_height);
    }
}

static inline bool compatible(const VideoFrame *a, const VideoFrame *b)
{
    return a && b &&
        (a->codec      == b->codec)      &&
        (a->width      == b->width)      &&
        (a->height     == b->height)     &&
        (a->size       == b->size)       &&
        (a->offsets[0] == b->offsets[0]) &&
        (a->offsets[1] == b->offsets[1]) &&
        (a->offsets[2] == b->offsets[2]) &&
        (a->pitches[0] == b->pitches[0]) &&
        (a->pitches[1] == b->pitches[1]) &&
        (a->pitches[2] == b->pitches[2]);
}

static inline void copy(VideoFrame *dst, const VideoFrame *src)
{
    VideoFrameType codec = dst->codec;
    if (dst->codec != src->codec)
        return;

    if (FMT_YV12 == codec)
    {
        int height0 = (dst->height < src->height) ? dst->height : src->height;
        int height1 = height0 >> 1;
        int height2 = height0 >> 1;
        int pitch0  = ((dst->pitches[0] < src->pitches[0]) ?
                       dst->pitches[0] : src->pitches[0]);
        int pitch1  = ((dst->pitches[1] < src->pitches[1]) ?
                       dst->pitches[1] : src->pitches[1]);
        int pitch2  = ((dst->pitches[2] < src->pitches[2]) ?
                       dst->pitches[2] : src->pitches[2]);

        memcpy(dst->buf + dst->offsets[0],
               src->buf + src->offsets[0], pitch0 * height0);
        memcpy(dst->buf + dst->offsets[1],
               src->buf + src->offsets[1], pitch1 * height1);
        memcpy(dst->buf + dst->offsets[2],
               src->buf + src->offsets[2], pitch2 * height2);
    }
}

#endif /* __cplusplus */

#endif

