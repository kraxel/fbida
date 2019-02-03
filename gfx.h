#include <stdbool.h>
#include <inttypes.h>

#include <pixman.h>
#include <cairo.h>

/* ---------------------------------------------------------------------- */

#undef  MAX
#define MAX(x,y)        ((x)>(y)?(x):(y))
#undef  MIN
#define MIN(x,y)        ((x)<(y)?(x):(y))
#define ARRAY_SIZE(x)   (sizeof(x)/sizeof(x[0]))

/* ---------------------------------------------------------------------- */

typedef struct gfxfmt gfxfmt;
typedef struct gfxstate gfxstate;

struct gfxfmt {
    uint32_t              fourcc;  /* little endian (drm) */
    cairo_format_t        cairo;   /* native endian */
    pixman_format_code_t  pixman;  /* native endian */
    uint32_t              bits_pp;
    uint32_t              bytes_pp;
};

extern gfxfmt fmt_list[];
extern uint32_t fmt_count;

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

    dev_t devnum;

    /* calls */
    void (*restore_display)(void);
    void (*cleanup_display)(void);
    void (*flush_display)(bool second);
};
