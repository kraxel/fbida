#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>

#include <libdrm/drm_fourcc.h>

#include "gfx.h"
#include "byteorder.h"

gfxfmt fmt_list[] = {
    {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        .fourcc   = DRM_FORMAT_XRGB8888,
#else
        .fourcc   = DRM_FORMAT_BGRX8888,
#endif
        .cairo    = CAIRO_FORMAT_RGB24,
        .pixman   = PIXMAN_x8r8g8b8,
        .depth    = 24,
        .bpp      = 32,
    },
    {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        .fourcc   = DRM_FORMAT_ARGB8888,
#else
        .fourcc   = DRM_FORMAT_BGRA8888,
#endif
        .cairo    = CAIRO_FORMAT_ARGB32,
        .pixman   = PIXMAN_a8r8g8b8,
        .depth    = 24,
        .bpp      = 32,
    },
#if __BYTE_ORDER == __LITTLE_ENDIAN
    {
        .fourcc   = DRM_FORMAT_RGB565,
        .cairo    = CAIRO_FORMAT_RGB16_565,
        .pixman   = PIXMAN_r5g6b5,
        .depth    = 16,
        .bpp      = 16,

    },
    {
        .fourcc   = DRM_FORMAT_XRGB2101010,
        .cairo    = CAIRO_FORMAT_RGB30,
        .pixman   = PIXMAN_x2r10g10b10,
        .depth    = 30,
        .bpp      = 32,
    },
#endif
};

uint32_t fmt_count = ARRAY_SIZE(fmt_list);

gfxfmt *gfx_fmt_find_pixman(pixman_format_code_t  pixman)
{
    int i;

    for (i = 0; i < fmt_count; i++) {
        if (pixman != fmt_list[i].pixman)
            continue;
        return fmt_list + i;
    }
    return NULL;
}
