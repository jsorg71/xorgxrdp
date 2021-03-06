/*
Copyright 2014 Laxmikant Rashinkar
Copyright 2014-2016 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

capture

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpReg.h"
#include "rdpMisc.h"
#include "rdpCapture.h"

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

#define RDP_MAX_TILES 1024

/******************************************************************************/
static int
rdpLimitRects(RegionPtr reg, int max_rects, BoxPtr *rects)
{
    int nrects;

    nrects = REGION_NUM_RECTS(reg);
    if (nrects > max_rects)
    {
        nrects = 1;
        *rects = rdpRegionExtents(reg);
    }
    else
    {
        *rects = REGION_RECTS(reg);
    }
    return nrects;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a8r8g8b8(rdpClientCon *clientCon,
                                const char *src, int src_stride, int srcx, int srcy,
                                char *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    int index;
    int jndex;
    int bytes;
    int height;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 4;
        bytes = box->x2 - box->x1;
        bytes *= 4;
        height = box->y2 - box->y1;
        for (jndex = 0; jndex < height; jndex++)
        {
            g_memcpy(d8, s8, bytes);
            d8 += dst_stride;
            s8 += src_stride;
        }
    }
    return 0;
}

/******************************************************************************/
static int
rdpFillBox_yuvalp(int ax, int ay,
                  char *dst, int dst_stride)
{
    dst = dst + (ay << 8) * (dst_stride >> 8) + (ax << 8);
    g_memset(dst, 0, 64 * 64 * 4);
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking
 * convert ARGB32 to 64x64 linear planar YUVA */
/* http://msdn.microsoft.com/en-us/library/ff635643.aspx
 * 0.299   -0.168935    0.499813
 * 0.587   -0.331665   -0.418531
 * 0.114    0.50059    -0.081282
   y = r *  0.299000 + g *  0.587000 + b *  0.114000;
   u = r * -0.168935 + g * -0.331665 + b *  0.500590;
   v = r *  0.499813 + g * -0.418531 + b * -0.081282; */
/* 19595  38470   7471
  -11071 -21736  32807
   32756 -27429  -5327 */
static int
rdpCopyBox_a8r8g8b8_to_yuvalp(int ax, int ay,
                              const char *src, int src_stride,
                              char *dst, int dst_stride,
                              BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    char *yptr;
    char *uptr;
    char *vptr;
    char *aptr;
    int *s32;
    int index;
    int jndex;
    int kndex;
    int width;
    int height;
    int pixel;
    int a;
    int r;
    int g;
    int b;
    int y;
    int u;
    int v;
    BoxPtr box;

    dst = dst + (ay << 8) * (dst_stride >> 8) + (ax << 8);
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + box->y1 * src_stride;
        s8 += box->x1 * 4;
        d8 = dst + (box->y1 - ay) * 64;
        d8 += box->x1 - ax;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        for (jndex = 0; jndex < height; jndex++)
        {
            s32 = (int *) s8;
            yptr = d8;
            uptr = yptr + 64 * 64;
            vptr = uptr + 64 * 64;
            aptr = vptr + 64 * 64;
            kndex = 0;
            while (kndex < width)
            {
                pixel = *(s32++);
                a = (pixel >> 24) & 0xff;
                r = (pixel >> 16) & 0xff;
                g = (pixel >>  8) & 0xff;
                b = (pixel >>  0) & 0xff;
                y = (r *  19595 + g *  38470 + b *   7471) >> 16;
                u = (r * -11071 + g * -21736 + b *  32807) >> 16;
                v = (r *  32756 + g * -27429 + b *  -5327) >> 16;
                u = u + 128;
                v = v + 128;
                y = RDPCLAMP(y, 0, 255);
                u = RDPCLAMP(u, 0, 255);
                v = RDPCLAMP(v, 0, 255);
                *(yptr++) = y;
                *(uptr++) = u;
                *(vptr++) = v;
                *(aptr++) = a;
                kndex++;
            }
            d8 += 64;
            s8 += src_stride;
        }
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_a8b8g8r8_box(const char *s8, int src_stride,
                         char *d8, int dst_stride,
                         int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    unsigned int *s32;
    unsigned int *d32;

    for (index = 0; index < height; index++)
    {
        s32 = (unsigned int *) s8;
        d32 = (unsigned int *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d32 = COLOR24(red, green, blue);
            s32++;
            d32++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a8b8g8r8(rdpClientCon *clientCon,
                                const char *src, int src_stride, int srcx, int srcy,
                                char *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = clientCon->dev->a8r8g8b8_to_a8b8g8r8_box;
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 4;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_r5g6b5_box(const char *s8, int src_stride,
                       char *d8, int dst_stride,
                       int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    unsigned int *s32;
    unsigned short *d16;

    for (index = 0; index < height; index++)
    {
        s32 = (unsigned int *) s8;
        d16 = (unsigned short *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d16 = COLOR16(red, green, blue);
            s32++;
            d16++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_r5g6b5(rdpClientCon *clientCon,
                              const char *src, int src_stride, int srcx, int srcy,
                              char *dst, int dst_stride, int dstx, int dsty,
                              BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_r5g6b5_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 2;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_a1r5g5b5_box(const char *s8, int src_stride,
                         char *d8, int dst_stride,
                         int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    const unsigned int *s32;
    unsigned short *d16;

    for (index = 0; index < height; index++)
    {
        s32 = (const unsigned int *) s8;
        d16 = (unsigned short *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d16 = COLOR15(red, green, blue);
            s32++;
            d16++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a1r5g5b5(rdpClientCon *clientCon,
                                const char *src, int src_stride, int srcx, int srcy,
                                char *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_a1r5g5b5_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 2;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_r3g3b2_box(const char *s8, int src_stride,
                       char *d8, int dst_stride,
                       int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    unsigned int *s32;
    unsigned char *ld8;

    for (index = 0; index < height; index++)
    {
        s32 = (unsigned int *) s8;
        ld8 = (unsigned char *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *ld8 = COLOR8(red, green, blue);
            s32++;
            ld8++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_r3g3b2(rdpClientCon *clientCon,
                              const char *src, int src_stride, int srcx, int srcy,
                              char *dst, int dst_stride, int dstx, int dsty,
                              BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_r3g3b2_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 1;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_nv12_box(const char *s8, int src_stride,
                     char *d8_y, int dst_stride_y,
                     char *d8_uv, int dst_stride_uv,
                     int width, int height)
{
    int index;
    int jndex;
    int R;
    int G;
    int B;
    int Y;
    int U;
    int V;
    int U_sum;
    int V_sum;
    int pixel;
    const int *s32a;
    const int *s32b;
    char *d8ya;
    char *d8yb;
    char *d8uv;

    for (jndex = 0; jndex < height; jndex += 2)
    {
        s32a = (const int *) (s8 + src_stride * jndex);
        s32b = (const int *) (s8 + src_stride * (jndex + 1));
        d8ya = d8_y + dst_stride_y * jndex;
        d8yb = d8_y + dst_stride_y * (jndex + 1);
        d8uv = d8_uv + dst_stride_uv * (jndex / 2);
        for (index = 0; index < width; index += 2)
        {
            U_sum = 0;
            V_sum = 0;

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            d8uv[0] = (U_sum + 2) / 4;
            d8uv++;
            d8uv[0] = (V_sum + 2) / 4;
            d8uv++;
        }
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_nv12(rdpClientCon *clientCon,
                            const char *src, int src_stride, int srcx, int srcy,
                            char *dst_y, int dst_stride_y,
                            char *dst_uv, int dst_stride_uv,
                            int dstx, int dsty,
                            BoxPtr rects, int num_rects)
{
    const char *s8;
    char *d8_y;
    char *d8_uv;
    int index;
    int width;
    int height;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8_y = dst_y + (box->y1 - dsty) * dst_stride_y;
        d8_y += (box->x1 - dstx) * 1;
        d8_uv = dst_uv + ((box->y1 - dsty) / 2) * dst_stride_uv;
        d8_uv += (box->x1 - dstx) * 1;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        clientCon->dev->a8r8g8b8_to_nv12_box(s8, src_stride,
                                             d8_y, dst_stride_y,
                                             d8_uv, dst_stride_uv,
                                             width, height);
    }
    return 0;
}

/******************************************************************************/
static Bool
rdpCapture0(rdpClientCon *clientCon,
            RegionPtr in_reg, BoxPtr *out_rects, int *num_out_rects,
            const char *src, int src_left, int src_top,
            int src_width, int src_height,
            int src_stride, int src_format,
            char *dst, int dst_width, int dst_height,
            int dst_stride, int dst_format, int max_rects)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    RegionRec reg;
    int num_rects;
    int i;
    Bool rv;

    LLOGLN(10, ("rdpCapture0:"));

    rv = TRUE;

    rect.x1 = src_left;
    rect.y1 = src_top;
    rect.x2 = src_left + src_width;
    rect.x2 = RDPMIN(dst_width, rect.x2);
    rect.y2 = src_top + src_height;
    rect.y2 = RDPMIN(dst_height, rect.y2);
    rdpRegionInit(&reg, &rect, 0);
    rdpRegionIntersect(&reg, in_reg, &reg);

    psrc_rects = 0;
    num_rects = rdpLimitRects(&reg, max_rects, &psrc_rects);
    if (num_rects < 1)
    {
        rdpRegionUninit(&reg);
        return FALSE;
    }

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects);
    for (i = 0; i < num_rects; i++)
    {
        rect = psrc_rects[i];
        (*out_rects)[i] = rect;
    }

    if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_a8r8g8b8))
    {
        rdpCopyBox_a8r8g8b8_to_a8r8g8b8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, src_left, src_top,
                                        psrc_rects, num_rects);
    }
    else if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_a8b8g8r8))
    {
        rdpCopyBox_a8r8g8b8_to_a8b8g8r8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, src_left, src_top,
                                        psrc_rects, num_rects);
    }
    else if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_r5g6b5))
    {
        rdpCopyBox_a8r8g8b8_to_r5g6b5(clientCon,
                                      src, src_stride, 0, 0,
                                      dst, dst_stride, src_left, src_top,
                                      psrc_rects, num_rects);
    }
    else if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_a1r5g5b5))
    {
        rdpCopyBox_a8r8g8b8_to_a1r5g5b5(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, src_left, src_top,
                                        psrc_rects, num_rects);
    }
    else if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_r3g3b2))
    {
        rdpCopyBox_a8r8g8b8_to_r3g3b2(clientCon,
                                      src, src_stride, 0, 0,
                                      dst, dst_stride, src_left, src_top,
                                      psrc_rects, num_rects);
    }
    else
    {
        LLOGLN(0, ("rdpCapture0: unimplemented color conversion"));
    }
    rdpRegionUninit(&reg);
    return rv;
}

/******************************************************************************/
/* make out_rects always multiple of 16 width and height */
static Bool
rdpCapture1(rdpClientCon *clientCon,
            RegionPtr in_reg, BoxPtr *out_rects, int *num_out_rects,
            const char *src, int src_left, int src_top,
            int src_width, int src_height,
            int src_stride, int src_format,
            char *dst, int dst_width, int dst_height,
            int dst_stride, int dst_format, int max_rects)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    RegionRec reg;
    const char *src_rect;
    char *dst_rect;
    int num_regions;
    int src_bytespp;
    int dst_bytespp;
    int width;
    int height;
    int min_width;
    int min_height;
    int src_offset;
    int dst_offset;
    int index;
    int jndex;
    int kndex;
    int red;
    int green;
    int blue;
    int ex;
    int ey;
    Bool rv;
    const unsigned int *s32;
    unsigned int *d32;

    LLOGLN(10, ("rdpCapture1:"));

    rv = TRUE;

    min_width = RDPMIN(dst_width, src_width);
    min_height = RDPMIN(dst_height, src_height);

    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = min_width;
    rect.y2 = min_height;
    rdpRegionInit(&reg, &rect, 0);
    rdpRegionIntersect(&reg, in_reg, &reg);

    num_regions = REGION_NUM_RECTS(&reg);

    if (num_regions > max_rects)
    {
        num_regions = 1;
        psrc_rects = rdpRegionExtents(&reg);
    }
    else
    {
        psrc_rects = REGION_RECTS(&reg);
    }

    if (num_regions < 1)
    {
        return FALSE;
    }

    *num_out_rects = num_regions;

    *out_rects = g_new(BoxRec, num_regions * 4);
    index = 0;
    while (index < num_regions)
    {
        rect = psrc_rects[index];
        width = rect.x2 - rect.x1;
        height = rect.y2 - rect.y1;
        ex = ((width + 15) & ~15) - width;
        if (ex != 0)
        {
            rect.x2 += ex;
            if (rect.x2 > min_width)
            {
                rect.x1 -= rect.x2 - min_width;
                rect.x2 = min_width;
            }
            if (rect.x1 < 0)
            {
                rect.x1 += 16;
            }
        }
        ey = ((height + 15) & ~15) - height;
        if (ey != 0)
        {
            rect.y2 += ey;
            if (rect.y2 > min_height)
            {
                rect.y1 -= rect.y2 - min_height;
                rect.y2 = min_height;
            }
            if (rect.y1 < 0)
            {
                rect.y1 += 16;
            }
        }
#if 0
        if (rect.x1 < 0)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
        if (rect.y1 < 0)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
        if (rect.x2 > min_width)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
        if (rect.y2 > min_height)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
        if ((rect.x2 - rect.x1) % 16 != 0)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
        if ((rect.y2 - rect.y1) % 16 != 0)
        {
            LLOGLN(0, ("rdpCapture1: error"));
        }
#endif
        (*out_rects)[index] = rect;
        index++;
    }

    if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_a8b8g8r8))
    {
        src_bytespp = 4;
        dst_bytespp = 4;

        for (index = 0; index < num_regions; index++)
        {
            /* get rect to copy */
            rect = (*out_rects)[index];

            /* get rect dimensions */
            width = rect.x2 - rect.x1;
            height = rect.y2 - rect.y1;

            /* point to start of each rect in respective memory */
            src_offset = rect.y1 * src_stride + rect.x1 * src_bytespp;
            dst_offset = rect.y1 * dst_stride + rect.x1 * dst_bytespp;
            src_rect = src + src_offset;
            dst_rect = dst + dst_offset;

            /* copy one line at a time */
            for (jndex = 0; jndex < height; jndex++)
            {
                s32 = (const unsigned int *) src_rect;
                d32 = (unsigned int *) dst_rect;
                for (kndex = 0; kndex < width; kndex++)
                {
                    SPLITCOLOR32(red, green, blue, *s32);
                    *d32 = COLOR24(red, green, blue);
                    s32++;
                    d32++;
                }
                src_rect += src_stride;
                dst_rect += dst_stride;
            }
        }
    }
    else
    {
        LLOGLN(0, ("rdpCapture1: unimplemented color conversion"));
    }
    rdpRegionUninit(&reg);
    return rv;
}

/******************************************************************************/
static Bool
rdpCapture2(rdpClientCon *clientCon,
            RegionPtr in_reg, BoxPtr *out_rects, int *num_out_rects,
            const char *src, int src_left, int src_top,
            int src_width, int src_height,
            int src_stride, int src_format,
            char *dst, int dst_width, int dst_height,
            int dst_stride, int dst_format, int max_rects)
{
    int x;
    int y;
    int out_rect_index;
    int num_rects;
    int rcode;
    BoxRec rect;
    BoxRec extents_rect;
    BoxPtr rects;
    RegionRec tile_reg;
    RegionRec lin_reg;
    RegionRec temp_reg;
    RegionPtr pin_reg;

    LLOGLN(10, ("rdpCapture2:"));

    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }
    out_rect_index = 0;

    /* clip for smaller of 2 */
    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = min(dst_width, src_width);
    rect.y2 = min(dst_height, src_height);
    rdpRegionInit(&temp_reg, &rect, 0);
    rdpRegionIntersect(&temp_reg, in_reg, &temp_reg);

    /* limit the number of rects */
    num_rects = REGION_NUM_RECTS(&temp_reg);
    if (num_rects > max_rects)
    {
        LLOGLN(10, ("rdpCapture2: too many rects"));
        rdpRegionInit(&lin_reg, rdpRegionExtents(&temp_reg), 0);
        pin_reg = &lin_reg;
    }
    else
    {
        LLOGLN(10, ("rdpCapture2: not too many rects"));
        rdpRegionInit(&lin_reg, NullBox, 0);
        pin_reg = &temp_reg;
    }
    extents_rect = *rdpRegionExtents(pin_reg);
    y = extents_rect.y1 & ~63;
    while (y < extents_rect.y2)
    {
        x = extents_rect.x1 & ~63;
        while (x < extents_rect.x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            rcode = rdpRegionContainsRect(pin_reg, &rect);
            LLOGLN(10, ("rdpCapture2: rcode %d", rcode));

            if (rcode != rgnOUT)
            {
                if (rcode == rgnPART)
                {
                    LLOGLN(10, ("rdpCapture2: rgnPART"));
                    rdpFillBox_yuvalp(x, y, dst, dst_stride);
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionIntersect(&tile_reg, pin_reg, &tile_reg);
                    rects = REGION_RECTS(&tile_reg);
                    num_rects = REGION_NUM_RECTS(&tile_reg);
                    rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  rects, num_rects);
                    rdpRegionUninit(&tile_reg);
                }
                else /* rgnIN */
                {
                    LLOGLN(10, ("rdpCapture2: rgnIN"));
                    rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  &rect, 1);
                }
                (*out_rects)[out_rect_index] = rect;
                out_rect_index++;
                if (out_rect_index >= RDP_MAX_TILES)
                {
                    free(*out_rects);
                    *out_rects = NULL;
                    rdpRegionUninit(&temp_reg);
                    rdpRegionUninit(&lin_reg);
                    return FALSE;
                }
            }
            x += 64;
        }
        y += 64;
    }
    *num_out_rects = out_rect_index;
    rdpRegionUninit(&temp_reg);
    rdpRegionUninit(&lin_reg);
    return TRUE;
}

/******************************************************************************/
/* make out_rects always multiple of 2 width and height */
static Bool
rdpCapture3(rdpClientCon *clientCon,
            RegionPtr in_reg, BoxPtr *out_rects, int *num_out_rects,
            const char *src, int src_left, int src_top,
            int src_width, int src_height,
            int src_stride, int src_format,
            char *dst, int dst_width, int dst_height,
            int dst_stride, int dst_format, int max_rects)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    RegionRec reg;
    int num_rects;
    int min_width;
    int min_height;
    int index;
    char *dst_uv;
    Bool rv;

    LLOGLN(10, ("rdpCapture3:"));
    LLOGLN(10, ("rdpCapture3: src_left %d src_top %d src_stride %d "
           "dst_stride %d", src_left, src_top, src_stride, dst_stride));

    rv = TRUE;

    min_width = RDPMIN(dst_width, src_width);
    min_height = RDPMIN(dst_height, src_height);

    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = min_width;
    rect.y2 = min_height;
    rdpRegionInit(&reg, &rect, 0);
    rdpRegionIntersect(&reg, in_reg, &reg);

    num_rects = REGION_NUM_RECTS(&reg);

    if (num_rects > max_rects)
    {
        num_rects = 1;
        psrc_rects = rdpRegionExtents(&reg);
    }
    else
    {
        psrc_rects = REGION_RECTS(&reg);
    }

    if (num_rects < 1)
    {
        return FALSE;
    }

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects * 4);
    index = 0;
    while (index < num_rects)
    {
        rect = psrc_rects[index];
        LLOGLN(10, ("old x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.x2,
               rect.x2, rect.y2));
        rect.x1 -= rect.x1 & 1;
        rect.y1 -= rect.y1 & 1;
        rect.x2 += rect.x2 & 1;
        rect.y2 += rect.y2 & 1;
        LLOGLN(10, ("new x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.x2,
               rect.x2, rect.y2));
        (*out_rects)[index] = rect;
        index++;
    }
    if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_a8r8g8b8))
    {
        rdpCopyBox_a8r8g8b8_to_a8r8g8b8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride,
                                        src_left, src_top,
                                        *out_rects, num_rects);
    }
    else if ((src_format == XRDP_a8r8g8b8) && (dst_format == XRDP_nv12))
    {
        dst_uv = dst;
        dst_uv += dst_width * dst_height;
        rdpCopyBox_a8r8g8b8_to_nv12(clientCon,
                                    src, src_stride, 0, 0,
                                    dst, dst_stride,
                                    dst_uv, dst_stride,
                                    src_left, src_top,
                                    *out_rects, num_rects);
    }
    else
    {
        LLOGLN(0, ("rdpCapture3: unimplemented color conversion"));
    }

    rdpRegionUninit(&reg);
    return rv;
}

/**
 * Copy an array of rectangles from one memory area to another
 *****************************************************************************/
Bool
rdpCapture(rdpClientCon *clientCon,
           RegionPtr in_reg, BoxPtr *out_rects, int *num_out_rects,
           const char *src, int src_left, int src_top,
           int src_width, int src_height,
           int src_stride, int src_format,
           char *dst, int dst_width, int dst_height,
           int dst_stride, int dst_format, int mode)
{
    LLOGLN(10, ("rdpCapture:"));
    LLOGLN(10, ("rdpCapture: src %p dst %p mode %d", src, dst, mode));
    switch (mode)
    {
        case 0:
            return rdpCapture0(clientCon, in_reg, out_rects, num_out_rects,
                               src, src_left, src_top, src_width, src_height,
                               src_stride, src_format,
                               dst, dst_width, dst_height,
                               dst_stride, dst_format, 15);
        case 1:
            return rdpCapture1(clientCon, in_reg, out_rects, num_out_rects,
                               src, src_left, src_top, src_width, src_height,
                               src_stride, src_format,
                               dst, dst_width, dst_height,
                               dst_stride, dst_format, 15);
        case 2:
            /* used for remotefx capture */
            return rdpCapture2(clientCon, in_reg, out_rects, num_out_rects,
                               src, src_left, src_top, src_width, src_height,
                               src_stride, src_format,
                               dst, dst_width, dst_height,
                               dst_stride, dst_format, 15);
        case 3:
            /* used for even align capture */
            return rdpCapture3(clientCon, in_reg, out_rects, num_out_rects,
                               src, src_left, src_top, src_width, src_height,
                               src_stride, src_format,
                               dst, dst_width, dst_height,
                               dst_stride, dst_format, 15);
        default:
            LLOGLN(0, ("rdpCapture: mode %d not implemented", mode));
            break;
    }
    return FALSE;
}
