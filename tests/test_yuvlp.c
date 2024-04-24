#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define RDPCLAMP(_val, _lo, _hi) \
    ((_val) < (_lo) ? (_lo) : (_val) > (_hi) ? (_hi) : (_val))

#define RGB_SPLIT(A, R, G, B, pixel) \
    A = (pixel >> 24) & UCHAR_MAX; \
    R = (pixel >> 16) & UCHAR_MAX; \
    G = (pixel >>  8) & UCHAR_MAX; \
    B = (pixel >>  0) & UCHAR_MAX;

int
a8r8g8b8_to_yuvalp_boxf(const uint8_t *s8, int src_stride,
                        uint8_t *d8, int dst_stride,
                        int width, int height)
{
    uint8_t *yptr;
    uint8_t *uptr;
    uint8_t *vptr;
    uint8_t *aptr;
    const uint32_t *s32;
    int jndex;
    int kndex;
    uint32_t pixel;
    uint8_t a;
    short r;
    short g;
    short b;
    float y;
    float u;
    float v;

    for (jndex = 0; jndex < height; jndex++)
    {
        s32 = (const uint32_t *) s8;
        yptr = d8;
        uptr = yptr + 64 * 64;
        vptr = uptr + 64 * 64;
        aptr = vptr + 64 * 64;
        kndex = 0;
        while (kndex < width)
        {
            pixel = *(s32++);
            RGB_SPLIT(a, r, g, b, pixel);
            y = r *  0.299000 + g *  0.587000 + b *  0.114000;
            u = r * -0.168935 + g * -0.331665 + b *  0.500590;
            v = r *  0.499813 + g * -0.418531 + b * -0.081282;
            u = u + 128;
            v = v + 128;
            y = RDPCLAMP(y, 0, UCHAR_MAX);
            u = RDPCLAMP(u, 0, UCHAR_MAX);
            v = RDPCLAMP(v, 0, UCHAR_MAX);
            *(yptr++) = y;
            *(uptr++) = u;
            *(vptr++) = v;
            *(aptr++) = a;
            kndex++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

int
a8r8g8b8_to_yuvalp_box16(const uint8_t *s8, int src_stride,
                         uint8_t *d8, int dst_stride,
                         int width, int height)
{
    uint8_t *yptr;
    uint8_t *uptr;
    uint8_t *vptr;
    uint8_t *aptr;
    const uint32_t *s32;
    int jndex;
    int kndex;
    uint32_t pixel;
    uint8_t a;
    short r;
    short g;
    short b;
    short y;
    short u;
    short v;

    for (jndex = 0; jndex < height; jndex++)
    {
        s32 = (const uint32_t *) s8;
        yptr = d8;
        uptr = yptr + 64 * 64;
        vptr = uptr + 64 * 64;
        aptr = vptr + 64 * 64;
        kndex = 0;
        while (kndex < width)
        {
            pixel = *(s32++);
            RGB_SPLIT(a, r, g, b, pixel);
            y = ((r * 77 + g * 150 + b * 29) + 128) >> 8;
            u = ((r * -43 + g * -85 + b * 128) + 128) >> 8;
            v = ((r * 128 + g * -107 + b * -21) + 128) >> 8;
            u = u + 128;
            v = v + 128;
            y = RDPCLAMP(y, 0, UCHAR_MAX);
            u = RDPCLAMP(u, 0, UCHAR_MAX);
            v = RDPCLAMP(v, 0, UCHAR_MAX);
            *(yptr++) = y;
            *(uptr++) = u;
            *(vptr++) = v;
            *(aptr++) = a;
            kndex++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

int
a8r8g8b8_to_yuvalp_box32(const uint8_t *s8, int src_stride,
                         uint8_t *d8, int dst_stride,
                         int width, int height)
{
    uint8_t *yptr;
    uint8_t *uptr;
    uint8_t *vptr;
    uint8_t *aptr;
    const uint32_t *s32;
    int jndex;
    int kndex;
    uint32_t pixel;
    uint8_t a;
    int r;
    int g;
    int b;
    int y;
    int u;
    int v;

    for (jndex = 0; jndex < height; jndex++)
    {
        s32 = (const uint32_t *) s8;
        yptr = d8;
        uptr = yptr + 64 * 64;
        vptr = uptr + 64 * 64;
        aptr = vptr + 64 * 64;
        kndex = 0;
        while (kndex < width)
        {
            pixel = *(s32++);
            RGB_SPLIT(a, r, g, b, pixel);
            y = (r *  19595 + g *  38470 + b *   7471) >> 16;
            u = (r * -11071 + g * -21736 + b *  32807) >> 16;
            v = (r *  32756 + g * -27429 + b *  -5327) >> 16;
            u = u + 128;
            v = v + 128;
            y = RDPCLAMP(y, 0, UCHAR_MAX);
            u = RDPCLAMP(u, 0, UCHAR_MAX);
            v = RDPCLAMP(v, 0, UCHAR_MAX);
            *(yptr++) = y;
            *(uptr++) = u;
            *(vptr++) = v;
            *(aptr++) = a;
            kndex++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

int get_biggest_diff(unsigned char* dst1, unsigned char* dst2, int bytes)
{
    int index;
    int diff;
    int biggest_diff = 0;

    for (index = 0; index < bytes; index++)
    {
        if (dst1[index] != dst2[index])
        {
            printf("1 %d 2 %d\n", dst1[index], dst2[index]);
            diff = abs(dst1[index] - dst2[index]);
            if (diff > biggest_diff)
            {
                biggest_diff = diff;
            }
        }
    }
    return biggest_diff;
}

#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define TILE_STRIDE (TILE_WIDTH * 4)
#define TILE_SIZE (TILE_STRIDE * TILE_HEIGHT)

int
main(int argc, char** argv)
{
    int index;
    char* tile_data;
    char* src;
    char* dst1;
    char* dst2;
    int fd;

    tile_data = malloc(TILE_SIZE * 3);
    src = tile_data;
    fd = open("/dev/urandom", O_RDONLY);
    read(fd, src, TILE_SIZE);
    dst1 = src + TILE_SIZE;
    dst2 = dst1 + TILE_SIZE;

#if 1
    a8r8g8b8_to_yuvalp_boxf(src, TILE_STRIDE, dst1, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
    //a8r8g8b8_to_yuvalp_box16(src, TILE_STRIDE, dst1, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
    a8r8g8b8_to_yuvalp_box32(src, TILE_STRIDE, dst2, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
    //a8r8g8b8_to_yuvalp_box_x86_sse2(src, TILE_STRIDE, dst2, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
    if (memcmp(dst1, dst2, TILE_SIZE) == 0)
    {
        printf("match\n");
    }
    else
    {
        printf("no match %d\n", get_biggest_diff(dst1, dst2, TILE_SIZE));
    }
#endif

#if 0
    for (index = 0; index < 64 * 1024; index++)
    {
        a8r8g8b8_to_yuvalp_box32(src, TILE_STRIDE, dst1, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
        //a8r8g8b8_to_yuvalp_box_x86_sse2(src, TILE_STRIDE, dst1, TILE_WIDTH, TILE_WIDTH, TILE_HEIGHT);
    }
#endif
    free(tile_data);
    close(fd);
    return 0;
}
