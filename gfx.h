#include <stdbool.h>
#include <inttypes.h>

#include <epoxy/egl.h>

typedef struct gfxstate gfxstate;

struct gfxstate {
    /* info */
    uint32_t hdisplay;
    uint32_t vdisplay;
    uint32_t stride;
    uint8_t  *mem;
    uint8_t  *mem2;

    uint32_t bits_per_pixel;
    uint32_t rlen, glen, blen, tlen;
    uint32_t roff, goff, boff, toff;

    /* egl */
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surface;

    /* calls */
    void (*restore_display)(void);
    void (*cleanup_display)(void);
    void (*flush_display)(bool second);
};
