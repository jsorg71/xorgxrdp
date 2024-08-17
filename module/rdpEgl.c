/*
Copyright 2018-2019 Jay Sorg

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

GLSL
EGL

*/

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include <glamor.h>

#include <gbm.h>
#include <drm_fourcc.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpMisc.h"
#include "rdpEgl.h"
#include "rdpReg.h"

#define XRDP_CRC_CHECK 0

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

#define SHADER_INDEX_COPY       0
#define SHADER_INDEX_RGB2YUV    1
#define SHADER_INDEX_YUV2YUVLP  2
#define SHADER_INDEX_CRC        3

struct rdp_egl
{
    GLuint quad_vao[1];
    GLuint quad_vbo[1];
    GLuint vertex_shader[4];
    GLuint fragment_shader[4];
    GLuint program[4];
    GLuint fb[1];
    GLint tex_loc[4];
    GLint tex_size_loc[4];
};

static const GLfloat g_vertices[] =
{
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};

static const GLchar g_vs[] =
"\
attribute vec4 position;\n\
void main(void)\n\
{\n\
    gl_Position = vec4(position.xy, 0.0, 1.0);\n\
}\n";
static const GLchar g_fs_copy[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main(void)\n\
{\n\
    gl_FragColor = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
}\n";
static const GLchar g_fs_rfx_rgb_to_yuv[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main()\n\
{\n\
    vec4 ymath;\n\
    vec4 umath;\n\
    vec4 vmath;\n\
    vec4 pixel;\n\
    vec4 pixel1;\n\
    vec4 pixel2;\n\
    ymath = vec4( 0.299000,  0.587000,  0.114000, 1.0);\n\
    umath = vec4(-0.168935, -0.331665,  0.500590, 1.0);\n\
    vmath = vec4( 0.499813, -0.418531, -0.081282, 1.0);\n\
    pixel = texture2D(tex, gl_FragCoord.xy / tex_size);\n\
    ymath = ymath * pixel;\n\
    umath = umath * pixel;\n\
    vmath = vmath * pixel;\n\
    pixel1 = vec4(ymath.r + ymath.g + ymath.b,\n\
                  umath.r + umath.g + umath.b + 0.5,\n\
                  vmath.r + vmath.g + vmath.b + 0.5,\n\
                  pixel.a);\n\
    pixel2 = clamp(pixel1, 0.0, 1.0);\n\
    gl_FragColor = pixel2;\n\
}\n";
static const GLchar g_fs_rfx_yuv_to_yuvlp[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
vec4 getpixel(float x1, float y1, float offset)\n\
{\n\
    float x;\n\
    float y;\n\
    vec2 xy;\n\
    x = x1 + floor(mod(offset, 64.0));\n\
    y = y1 + floor(offset / 64.0);\n\
    xy.x = x + 0.5;\n\
    xy.y = y + 0.5;\n\
    return texture2D(tex, xy / tex_size);\n\
}\n\
void main()\n\
{\n\
    float x;\n\
    float y;\n\
    float x1;\n\
    float y1;\n\
    float x2;\n\
    float y2;\n\
    float offset;\n\
    vec4 pixel1;\n\
    x = floor(gl_FragCoord.x);\n\
    y = floor(gl_FragCoord.y);\n\
    x1 = floor((x + 0.5) / 64.0) * 64.0;\n\
    y1 = floor((y + 0.5) / 64.0) * 64.0;\n\
    x2 = x - x1;\n\
    y2 = y - y1;\n\
    offset = y2 * 64.0 + x2;\n\
    if (offset + 0.5 < 1024.0)\n\
    {\n\
        pixel1.b = getpixel(x1, y1, offset * 4.0 + 0.0).r;\n\
        pixel1.g = getpixel(x1, y1, offset * 4.0 + 1.0).r;\n\
        pixel1.r = getpixel(x1, y1, offset * 4.0 + 2.0).r;\n\
        pixel1.a = getpixel(x1, y1, offset * 4.0 + 3.0).r;\n\
    }\n\
    else if (offset + 0.5 < 2048.0)\n\
    {\n\
        offset -= 1024.0;\n\
        pixel1.b = getpixel(x1, y1, offset * 4.0 + 0.0).g;\n\
        pixel1.g = getpixel(x1, y1, offset * 4.0 + 1.0).g;\n\
        pixel1.r = getpixel(x1, y1, offset * 4.0 + 2.0).g;\n\
        pixel1.a = getpixel(x1, y1, offset * 4.0 + 3.0).g;\n\
    }\n\
    else if (offset + 0.5 < 3072.0)\n\
    {\n\
        offset -= 2048.0;\n\
        pixel1.b = getpixel(x1, y1, offset * 4.0 + 0.0).b;\n\
        pixel1.g = getpixel(x1, y1, offset * 4.0 + 1.0).b;\n\
        pixel1.r = getpixel(x1, y1, offset * 4.0 + 2.0).b;\n\
        pixel1.a = getpixel(x1, y1, offset * 4.0 + 3.0).b;\n\
    }\n\
    else\n\
    {\n\
        offset -= 3072.0;\n\
        pixel1.b = getpixel(x1, y1, offset * 4.0 + 0.0).a;\n\
        pixel1.g = getpixel(x1, y1, offset * 4.0 + 1.0).a;\n\
        pixel1.r = getpixel(x1, y1, offset * 4.0 + 2.0).a;\n\
        pixel1.a = getpixel(x1, y1, offset * 4.0 + 3.0).a;\n\
    }\n\
    gl_FragColor = pixel1;\n\
}\n";
static const GLchar g_fs_rfx_crc[] =
"\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main()\n\
{\n\
    float x;\n\
    float y;\n\
    float index;\n\
    float jndex;\n\
    float a = 1.0;\n\
    float b = 0.0;\n\
    vec2 xy;\n\
    vec4 tmp_pixel;\n\
    vec4 sum_pixel;\n\
    x = floor(gl_FragCoord.x) * 64.0;\n\
    y = floor(gl_FragCoord.y) * 64.0;\n\
    for (index = 0.5; index < 64.0; index += 1.0)\n\
    {\n\
        xy.y = y + index;\n\
        for (jndex = 0.5; jndex < 64.0; jndex += 1.0)\n\
        {\n\
            xy.x = x + jndex;\n\
            tmp_pixel = texture2D(tex, xy / tex_size);\n\
            tmp_pixel = tmp_pixel * 255.0;\n\
            tmp_pixel = floor(clamp(tmp_pixel, 0.0, 255.0));\n\
            a = mod(a + tmp_pixel.b, 65521.0);\n\
            b = mod(b + a, 65521.0);\n\
            a = mod(a + tmp_pixel.g, 65521.0);\n\
            b = mod(b + a, 65521.0);\n\
            a = mod(a + tmp_pixel.r, 65521.0);\n\
            b = mod(b + a, 65521.0);\n\
            a = mod(a + tmp_pixel.a, 65521.0);\n\
            b = mod(b + a, 65521.0);\n\
        }\n\
    }\n\
    sum_pixel.a = b / 256.0;\n\
    sum_pixel.r = mod(b, 256.0);\n\
    sum_pixel.g = a / 256.0;\n\
    sum_pixel.b = mod(a, 256.0);\n\
    gl_FragColor = floor(clamp(sum_pixel, 0.0, 255.0)) / 255.0;\n\
}\n";

#if XRDP_CRC_CHECK
/******************************************************************************/
static int
alder32(const void *data, int data_bytes)
{
    int a = 1;
    int b = 0;
    int index;
    const uint8_t *data8 = (const uint8_t *)data;

    for (index = 0; index < data_bytes; index++)
    {
        a = (a + data8[index]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
#endif

/******************************************************************************/
static int
rdpEglSetupShader(struct rdp_egl *egl,
                  const GLchar *vsource,
                  const GLchar *fsource,
                  int index)
{
    GLint vlength;
    GLint flength;
    GLint linked;
    GLint compiled;
    GLuint vs;
    GLuint fs;
    GLuint pro;
    GLint tex_loc;
    GLint tex_size_loc;

    vs = glCreateShader(GL_VERTEX_SHADER);
    fs = glCreateShader(GL_FRAGMENT_SHADER);
    egl->vertex_shader[index] = vs;
    egl->fragment_shader[index] = fs;
    vlength = strlen(vsource);
    flength = strlen(fsource);
    glShaderSource(vs, 1, &vsource, &vlength);
    glShaderSource(fs, 1, &fsource, &flength);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglSetupShader: vertex_shader compiled %d", compiled));
    if (!compiled)
    {
        return 1;
    }
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglSetupShader: fragment_shader compiled %d", compiled));
    if (!compiled)
    {
        return 1;
    }
    pro = glCreateProgram();
    egl->program[index] = pro;
    glAttachShader(pro, vs);
    glAttachShader(pro, fs);
    glLinkProgram(pro);
    glGetProgramiv(pro, GL_LINK_STATUS, &linked);
    LLOGLN(0, ("rdpEglSetupShader: linked %d", linked));
    if (!linked)
    {
        return 1;
    }
    tex_loc = glGetUniformLocation(pro, "tex");
    egl->tex_loc[index] = tex_loc;
    tex_size_loc = glGetUniformLocation(pro, "tex_size");
    egl->tex_size_loc[index] = tex_size_loc;
    LLOGLN(0, ("rdpEglSetupShader: copy_tex_loc %d copy_tex_size_loc %d",
           tex_loc, tex_size_loc));
    return 0;
}

/******************************************************************************/
void *
rdpEglCreate(ScreenPtr screen)
{
    struct rdp_egl *egl;
    GLint old_vertex_array;

    egl = g_new0(struct rdp_egl, 1);
    /* create vertex array */
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glGenVertexArrays(1, egl->quad_vao);
    glBindVertexArray(egl->quad_vao[0]);
    glGenBuffers(1, egl->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, egl->quad_vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(old_vertex_array);
    glGenFramebuffers(1, egl->fb);
    /* create copy shader */
    rdpEglSetupShader(egl, g_vs, g_fs_copy,
                      SHADER_INDEX_COPY);
    /* create yuv shader */
    rdpEglSetupShader(egl, g_vs, g_fs_rfx_rgb_to_yuv,
                      SHADER_INDEX_RGB2YUV);
    /* create yuvlp shader */
    rdpEglSetupShader(egl, g_vs, g_fs_rfx_yuv_to_yuvlp,
                      SHADER_INDEX_YUV2YUVLP);
    /* create crc shader */
    rdpEglSetupShader(egl, g_vs, g_fs_rfx_crc,
                      SHADER_INDEX_CRC);
    return egl;
}

/******************************************************************************/
int
rdpEglDestroy(void *eglptr)
{
    struct rdp_egl *egl;

    egl = (struct rdp_egl *) eglptr;
    if (egl == NULL)
    {
        return 0;
    }
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxRgbToYuv(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
                  GLint width, GLint height)
{
    GLint old_vertex_array;
    int status;

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxRgbToYuv: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, width, height);
    glUseProgram(egl->program[SHADER_INDEX_RGB2YUV]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[SHADER_INDEX_RGB2YUV], 0);
    glUniform2f(egl->tex_size_loc[SHADER_INDEX_RGB2YUV], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxYuvToYuvlp(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
                    GLint width, GLint height)
{
    GLint old_vertex_array;
    int status;

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxYuvToYuvlp: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, width, height);
    glUseProgram(egl->program[SHADER_INDEX_YUV2YUVLP]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[SHADER_INDEX_YUV2YUVLP], 0);
    glUniform2f(egl->tex_size_loc[SHADER_INDEX_YUV2YUVLP], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxCrc(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
             GLint width, GLint height, int *crcs)
{
    GLint old_vertex_array;
    int status;
    int w_div_64;
    int h_div_64;

    w_div_64 = width / 64;
    h_div_64 = height / 64;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxCrc: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, w_div_64, h_div_64);
    glUseProgram(egl->program[SHADER_INDEX_CRC]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[SHADER_INDEX_CRC], 0);
    glUniform2f(egl->tex_size_loc[SHADER_INDEX_CRC], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, w_div_64, h_div_64, GL_BGRA,
                 GL_UNSIGNED_INT_8_8_8_8_REV, crcs);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglOut(rdpClientCon *clientCon, struct rdp_egl *egl, RegionPtr in_reg,
          BoxPtr out_rects, int *num_out_rects, struct image_data *id,
          uint32_t tex, BoxPtr tile_extents_rect, int *crcs)
{
    int x;
    int y;
    int lx;
    int ly;
    int dst_stride;
    int rcode;
    int out_rect_index;
    int status;
    BoxRec rect;
    RegionRec tile_reg;
    uint8_t *dst;
    uint8_t *tile_dst;
    int crc_offset;
    int crc_stride;
    uint32_t crc;
    int num_crcs;
    int tile_extents_stride;
    int mon_index;

    mon_index = (id->flags >> 28) & 0xF;
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglOut: glCheckFramebufferStatus error"));
    }
    dst = id->shmem_pixels;
    dst_stride = ((id->width + 63) & ~63) * 4;
    /* check crc list size */
    crc_stride = (id->width + 63) / 64;
    num_crcs = crc_stride * ((id->height + 63) / 64);
    if (num_crcs != clientCon->num_rfx_crcs_alloc[mon_index])
    {
        LLOGLN(0, ("rdpEglOut: resize the crc list was %d now %d",
               clientCon->num_rfx_crcs_alloc[mon_index], num_crcs));
        /* resize the crc list */
        clientCon->num_rfx_crcs_alloc[mon_index] = num_crcs;
        free(clientCon->rfx_crcs[mon_index]);
        clientCon->rfx_crcs[mon_index] = g_new0(uint64_t, num_crcs);
    }
    tile_extents_stride = (tile_extents_rect->x2 - tile_extents_rect->x1) / 64;
    out_rect_index = 0;
    y = tile_extents_rect->y1;
    while (y < tile_extents_rect->y2)
    {
        x = tile_extents_rect->x1;
        while (x < tile_extents_rect->x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            LLOGLN(10, ("rdpEglOut: x1 %d y1 %d x2 %d y2 %d",
                   rect.x1, rect.y1, rect.x2, rect.y2));
            rcode = rdpRegionContainsRect(in_reg, &rect);
            if (rcode == rgnOUT)
            {
                LLOGLN(10, ("rdpEglOut: rgnOUT"));
                rdpRegionInit(&tile_reg, &rect, 0);
                rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                rdpRegionUninit(&tile_reg);
            }
            else
            {
                lx = x - tile_extents_rect->x1;
                ly = y - tile_extents_rect->y1;
                tile_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
#if XRDP_CRC_CHECK
                /* check if the gpu calculated the crcs right */
                glReadPixels(lx, ly, 64, 64, GL_BGRA,
                             GL_UNSIGNED_INT_8_8_8_8_REV, tile_dst);
                crc = alder32(tile_dst, 64 * 64 * 4);
                if (crc != crcs[(ly / 64) * tile_extents_stride + (lx / 64)])
                {
                    LLOGLN(0, ("rdpEglOut: error crc no match "
                           "0x%8.8x 0x%8.8x", crc,
                           crcs[(ly / 64) * tile_extents_stride + (lx / 64)]));
                }
#endif
                crc = crcs[(ly / 64) * tile_extents_stride + (lx / 64)];
                crc_offset = (y / 64) * crc_stride + (x / 64);
                if (crc == clientCon->rfx_crcs[mon_index][crc_offset])
                {
                    LLOGLN(10, ("rdpEglOut: crc skip at x %d y %d", x, y));
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                    rdpRegionUninit(&tile_reg);
                }
                else
                {
                    glReadPixels(lx, ly, 64, 64, GL_BGRA,
                                 GL_UNSIGNED_INT_8_8_8_8_REV, tile_dst);
                    clientCon->rfx_crcs[mon_index][crc_offset] = crc;
                    out_rects[out_rect_index] = rect;
                    if (out_rect_index < RDP_MAX_TILES)
                    {
                        out_rect_index++;
                    }
                    else
                    {
                        LLOGLN(0, ("rdpEglOut: too many out rects %d",
                               out_rect_index));
                    }
                }

            }
            x += XRDP_RFX_ALIGN;
        }
        y += XRDP_RFX_ALIGN;
    }
    *num_out_rects = out_rect_index;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxClear(GCPtr rfxGC, PixmapPtr yuv_pixmap, BoxPtr tile_extents_rect,
               RegionPtr in_reg)
{
    RegionPtr reg;
    xRectangle rect;

    reg = rdpRegionCreate(tile_extents_rect, 0);
    rdpRegionSubtract(reg, reg, in_reg);
    rdpRegionTranslate(reg, -tile_extents_rect->x1, -tile_extents_rect->y1);
    /* rfxGC takes ownership of reg */
    rfxGC->funcs->ChangeClip(rfxGC, CT_REGION, reg, 0);
    rect.x = 0;
    rect.y = 0;
    rect.width = tile_extents_rect->x2 - tile_extents_rect->x1;
    rect.height = tile_extents_rect->y2 - tile_extents_rect->y1;
    rfxGC->ops->PolyFillRect(&(yuv_pixmap->drawable), rfxGC, 1, &rect);
    return 0;
}

/******************************************************************************/
Bool
rdpEglCaptureRfx(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    int width;
    int height;
    uint32_t tex;
    uint32_t yuv_tex;
    uint32_t crc_tex;
    BoxRec extents_rect;
    BoxRec tile_extents_rect;
    ScreenPtr pScreen;
    PixmapPtr screen_pixmap;
    PixmapPtr pixmap;
    PixmapPtr yuv_pixmap;
    PixmapPtr crc_pixmap;
    GCPtr rfxGC;
    ChangeGCVal tmpval[2];
    rdpPtr dev;
    struct rdp_egl *egl;
    int *crcs;

    dev = clientCon->dev;
    pScreen = dev->pScreen;
    egl = (struct rdp_egl *) (dev->egl);
    screen_pixmap = pScreen->GetScreenPixmap(pScreen);
    if (screen_pixmap == NULL)
    {
        return FALSE;
    }
    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }

    rdpRegionTranslate(in_reg, -id->left, -id->top);

    extents_rect = *rdpRegionExtents(in_reg);
    tile_extents_rect.x1 = extents_rect.x1 & ~63;
    tile_extents_rect.y1 = extents_rect.y1 & ~63;
    tile_extents_rect.x2 = (extents_rect.x2 + 63) & ~63;
    tile_extents_rect.y2 = (extents_rect.y2 + 63) & ~63;
    width = tile_extents_rect.x2 - tile_extents_rect.x1;
    height = tile_extents_rect.y2 - tile_extents_rect.y1;
    LLOGLN(10, ("rdpEglCaptureRfx: width %d height %d", width, height));
    crcs = g_new(int, (width / 64) * (height / 64));
    if (crcs == NULL)
    {
        free(out_rects);
        return FALSE;
    }
    rfxGC = GetScratchGC(dev->depth, pScreen);
    if (rfxGC != NULL)
    {
        tmpval[0].val = GXcopy;
        tmpval[1].val = 0;
        ChangeGC(NullClient, rfxGC, GCFunction | GCForeground, tmpval);
        ValidateGC(&(screen_pixmap->drawable), rfxGC);
        pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                       pScreen->rootDepth,
                                       GLAMOR_CREATE_NO_LARGE);
        if (pixmap != NULL)
        {
            tex = glamor_get_pixmap_texture(pixmap);
            crc_pixmap = pScreen->CreatePixmap(pScreen, width / 64,
                                               height / 64,
                                               pScreen->rootDepth,
                                               GLAMOR_CREATE_NO_LARGE);
            if (crc_pixmap != NULL)
            {
                crc_tex = glamor_get_pixmap_texture(crc_pixmap);
                yuv_pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                                   pScreen->rootDepth,
                                                   GLAMOR_CREATE_NO_LARGE);
                if (yuv_pixmap != NULL)
                {
                    yuv_tex = glamor_get_pixmap_texture(yuv_pixmap);
                    rfxGC->ops->CopyArea(&(screen_pixmap->drawable),
                                         &(pixmap->drawable), rfxGC,
                                         tile_extents_rect.x1 + id->left,
                                         tile_extents_rect.y1 + id->top,
                                         width, height, 0, 0);
                    rdpEglRfxRgbToYuv(egl, tex, yuv_tex, width, height);
                    rdpEglRfxClear(rfxGC, yuv_pixmap, &tile_extents_rect,
                                   in_reg);
                    rdpEglRfxYuvToYuvlp(egl, yuv_tex, tex, width, height);
                    rdpEglRfxCrc(egl, tex, crc_tex, width, height, crcs);
                    rdpEglOut(clientCon, egl, in_reg, *out_rects,
                              num_out_rects, id, tex, &tile_extents_rect,
                              crcs);
                    pScreen->DestroyPixmap(yuv_pixmap);
                }
                else
                {
                    LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
                }
                pScreen->DestroyPixmap(crc_pixmap);
            }
            else
            {
                LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
            }
            pScreen->DestroyPixmap(pixmap);
        }
        else
        {
            LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
        }
        FreeScratchGC(rfxGC);
    }
    else
    {
        LLOGLN(0, ("rdpEglCaptureRfx: GetScratchGC failed"));
    }
    free(crcs);
    return TRUE;
}
