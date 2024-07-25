/*
Copyright 2024 Jay Sorg

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

NEON SIMD a8r8g8b8_to_yuvalp

*/

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arm_neon.h>

const uint32x4_t g_cd255 = { 255, 255, 255, 255 };
const int16x8_t g_cw128 = { 128, 128, 128, 128, 128, 128, 128, 128 };
const int16x8_t g_cw77  = {  77,  77,  77,  77,  77,  77,  77,  77 };
const int16x8_t g_cw150 = { 150, 150, 150, 150, 150, 150, 150, 150 };
const int16x8_t g_cw29  = {  29,  29,  29,  29,  29,  29,  29,  29 };
const int16x8_t g_cw43  = {  43,  43,  43,  43,  43,  43,  43,  43 };
const int16x8_t g_cw85  = {  85,  85,  85,  85,  85,  85,  85,  85 };
const int16x8_t g_cw107 = { 107, 107, 107, 107, 107, 107, 107, 107 };
const int16x8_t g_cw21  = {  21,  21,  21,  21,  21,  21,  21,  21 };

/******************************************************************************/
/* notes
   address s8 should be aligned on 16 bytes, will be slower if not
   width must be multiple of 8 and > 0
   height must be > 0 */
void
a8r8g8b8_to_yuvalp_box_neon(const uint8_t *s8, int src_stride,
                            uint8_t *d8, int dst_stride,
                            int width, int height)
{
    int jndex;
    int kndex;
    uint32x4_t v1;
    uint32x4_t blue1;
    uint32x4_t blue2;
    uint32x4_t green1;
    uint32x4_t green2;
    uint32x4_t red1;
    uint32x4_t red2;
    uint32x4_t alpha1;
    uint32x4_t alpha2;
    int16x8_t blue;
    int16x8_t green;
    int16x8_t red;
    int16x8_t v2;
    uint8x8_t out;

    for (jndex = 0; jndex < height; jndex++)
    {
        for (kndex = 0; kndex < width; kndex += 8)
        {
            /* 4 pixels, 16 bytes */
            v1 = vld1q_u32((const uint32_t *)(s8 + kndex * 4));
            blue1 = vandq_u32(v1, g_cd255);
            green1 = vandq_u32(vshrq_n_u32(v1, 8), g_cd255);
            red1 = vandq_u32(vshrq_n_u32(v1, 16), g_cd255);
            alpha1 = vandq_u32(vshrq_n_u32(v1, 24), g_cd255);
            /* 4 pixels, 16 bytes */
            v1 = vld1q_u32((const uint32_t *)(s8 + kndex * 4 + 16));
            /* take care of alpha first */
            alpha2 = vandq_u32(vshrq_n_u32(v1, 24), g_cd255);
            out = vqmovn_u16(vcombine_u16(vqmovn_u32(alpha1),
                                          vqmovn_u32(alpha2)));
            vst1_u8(d8 + kndex * 8 + 3 * 64 * 64, out);
            /* blue */
            blue2 = vandq_u32(v1, g_cd255);
            blue = (int16x8_t)vcombine_u16(vqmovn_u32(blue1),
                                           vqmovn_u32(blue2));
            /* green */
            green2 = vandq_u32(vshrq_n_u32(v1, 8), g_cd255);
            green = (int16x8_t)vcombine_u16(vqmovn_u32(green1),
                                            vqmovn_u32(green2));
            /* red */
            red2 = vandq_u32(vshrq_n_u32(v1, 16), g_cd255);
            red = (int16x8_t)vcombine_u16(vqmovn_u32(red1),
                                          vqmovn_u32(red2));
            /* _Y = (77 * _R + 150 * _G + 29 * _B) >> 8 */
            v2 = vaddq_s16(vmulq_s16(blue, g_cw29),
                           vmulq_s16(green, g_cw150));
            v2 = vaddq_s16(v2, vmulq_s16(red, g_cw77));
            out = vqmovn_u16((uint16x8_t)vshrq_n_s16(v2, 8));
            vst1_u8(d8 + kndex * 8 + 0 * 64 * 64, out);
            /* _U = ((-43 * _R - 85 * _G + 128 * _B) >> 8) + 128 */
            v2 = vsubq_s16(vmulq_s16(blue, g_cw128),
                           vmulq_s16(green, g_cw85));
            v2 = vsubq_s16(v2, vmulq_s16(red, g_cw43));
            v2 = vaddq_s16(vshrq_n_s16(v2, 8), g_cw128);
            out = vqmovn_u16((uint16x8_t)v2);
            vst1_u8(d8 + kndex * 8 + 1 * 64 * 64, out);
            /* _V = ((128 * _R - 107 * _G -  21 * _B) >> 8) + 128 */
            v2 = vsubq_s16(vmulq_s16(red, g_cw128),
                           vmulq_s16(green, g_cw107));
            v2 = vsubq_s16(v2, vmulq_s16(blue, g_cw21));
            v2 = vaddq_s16(vshrq_n_s16(v2, 8), g_cw128);
            out = vqmovn_u16((uint16x8_t)v2);
            vst1_u8(d8 + kndex * 8 + 2 * 64 * 64, out);
        }
        s8 += src_stride;
        d8 += dst_stride;
    }
}
