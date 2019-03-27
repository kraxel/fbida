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
    uint32_t              depth;
    uint32_t              bpp;
};

extern gfxfmt fmt_list[];
extern uint32_t fmt_count;

gfxfmt *gfx_fmt_find_pixman(pixman_format_code_t  pixman);

struct gfxstate {
    /* info */
    uint32_t hdisplay;
    uint32_t vdisplay;
    uint32_t stride;
    uint8_t  *mem;
    uint8_t  *mem2;
    gfxfmt   *fmt;

    char devpath[128];
    dev_t devnum;

    /* calls */
    void (*suspend_display)(void);
    void (*resume_display)(void);
    void (*cleanup_display)(void);
    void (*flush_display)(bool second);
};
