#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include <pixman.h>

#include "vt.h"
#include "fbtools.h"
#include "readers.h"
#include "fb-gui.h"

static int ys =  3;
static int xs = 10;

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- internals                                        */

static int32_t s_lut_transp[256], s_lut_red[256], s_lut_green[256], s_lut_blue[256];

static unsigned char **shadow;
static unsigned int  swidth,sheight;

static cairo_t *context;
static cairo_surface_t *surface;
static pixman_image_t *pixman;
static unsigned char *framebuffer;
static cairo_font_extents_t extents;

static void shadow_lut_init_one(int32_t *lut, int bits, int shift)
{
    int i;
    
    if (bits > 8)
	for (i = 0; i < 256; i++)
	    lut[i] = (i << (bits + shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut[i] = (i >> (8 - bits)) << shift;
}

static void shadow_lut_init(gfxstate *gfx)
{
    shadow_lut_init_one(s_lut_transp, gfx->tlen, gfx->toff);
    shadow_lut_init_one(s_lut_red,    gfx->rlen, gfx->roff);
    shadow_lut_init_one(s_lut_green,  gfx->glen, gfx->goff);
    shadow_lut_init_one(s_lut_blue,   gfx->blen, gfx->boff);
}

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- management interface                             */

void shadow_render(gfxstate *gfx)
{
    static pixman_image_t *gfxfb;

    if (!console_visible)
	return;
    gfxfb = pixman_image_create_bits(gfx->fmt->pixman,
                                     gfx->hdisplay,
                                     gfx->vdisplay,
                                     (void*)gfx->mem,
                                     gfx->stride);
    pixman_image_composite(PIXMAN_OP_SRC, pixman, NULL, gfxfb,
                           0, 0,
                           0, 0,
                           0, 0,
                           gfx->hdisplay, gfx->vdisplay);
    pixman_image_unref(gfxfb);
    if (gfx->flush_display)
        gfx->flush_display(false);
}

void shadow_clear_lines(int first, int last)
{
    cairo_rectangle(context, 0, first, swidth, last - first + 1);
    cairo_set_source_rgb(context, 0, 0, 0);
    cairo_fill(context);
}

void shadow_clear(void)
{
    shadow_clear_lines(0, sheight-1);
}

void shadow_init(gfxstate *gfx)
{
    int i;

    /* init shadow fb */
    swidth  = gfx->hdisplay;
    sheight = gfx->vdisplay;
    shadow  = malloc(sizeof(unsigned char*) * sheight);
    framebuffer = malloc(swidth*sheight*4);
    for (i = 0; i < sheight; i++)
	shadow[i] = framebuffer + i*swidth*4;
    surface = cairo_image_surface_create_for_data(framebuffer,
                                                  CAIRO_FORMAT_RGB24,
                                                  swidth, sheight,
                                                  swidth * 4);
    context = cairo_create(surface);
    pixman = pixman_image_create_bits(PIXMAN_x8r8g8b8, swidth, sheight,
                                      (void*)framebuffer, swidth * 4);
    shadow_clear();

    /* init rendering */
    switch (gfx->bits_per_pixel) {
    case 15:
    case 16:
    case 24:
    case 32:
        shadow_lut_init(gfx);
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		gfx->bits_per_pixel);
	exit(1);
    }
}

void shadow_fini(void)
{
    if (!shadow)
	return;
    free(shadow);
    free(framebuffer);
}

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- drawing interface                                */

void shadow_draw_line(int x1, int x2, int y1,int y2)
{
    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_set_line_width(context, 1);

    cairo_move_to(context, x1 + 0.5, y1 + 0.5);
    cairo_line_to(context, x2 + 0.5, y2 + 0.5);
    cairo_stroke(context);
}

void shadow_draw_rect(int x1, int x2, int y1, int y2)
{
    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_set_line_width(context, 1);

    cairo_move_to(context, x1 + 0.5, y1 + 0.5);
    cairo_line_to(context, x2 + 0.5, y1 + 0.5);
    cairo_line_to(context, x2 + 0.5, y2 + 0.5);
    cairo_line_to(context, x1 + 0.5, y2 + 0.5);
    cairo_line_to(context, x1 + 0.5, y1 + 0.5);
    cairo_stroke(context);
}

void shadow_composite_image(struct ida_image *img,
                            int xoff, int yoff, int weight)
{
    if (weight == 100) {
        pixman_image_composite(PIXMAN_OP_SRC, img->p, NULL, pixman,
                               0, 0, 0, 0,
                               xoff, yoff,
                               img->i.width, img->i.height);
    } else {
        pixman_color_t color = {
            .alpha = weight * 0xffff / 100,
        };
        pixman_image_t *mask = pixman_image_create_solid_fill(&color);

        pixman_image_composite(PIXMAN_OP_OVER, img->p, mask, pixman,
                               0, 0, 0, 0,
                               xoff, yoff,
                               img->i.width, img->i.height);
        pixman_image_unref(mask);
    }
}

void shadow_darkify(int x1, int x2, int y1,int y2, int percent)
{
    cairo_rectangle(context, x1, y1,
                    x2 - x1 + 1,
                    y2 - y1 + 1);
    cairo_set_source_rgba(context, 0, 0, 0, 1 - (percent * 0.01));
    cairo_fill(context);
}

int shadow_draw_string(int x, int y, char *str, int align)
{
    cairo_text_extents_t te;

    cairo_text_extents(context, str, &te);
    switch(align) {
    case -1: /* left */
	break;
    case 0: /* center */
	x -= te.x_advance / 2;
	break;
    case 1: /* right */
	x -= te.x_advance;
	break;
    }

    cairo_move_to(context, x, y + extents.ascent);
    cairo_show_text(context, str);
    return 0;
}

void shadow_draw_text_box(int x, int y, int percent, char *lines[], unsigned int count)
{
    unsigned int i, len, max, x1, x2, y1, y2;

    if (!console_visible)
	return;

    max = 0;
    for (i = 0; i < count; i++) {
	len = strlen(lines[i]);
	if (max < len)
	    max = len;
    }

    x1 = x;
    x2 = x + max * extents.max_x_advance;
    y1 = y;
    y2 = y + count * extents.height;

    x += xs; x2 += 2*xs;
    y += ys; y2 += 2*ys;

    shadow_darkify(x1, x2, y1, y2, percent);
    shadow_draw_rect(x1, x2, y1, y2);
    for (i = 0; i < count; i++) {
	shadow_draw_string(x, y, lines[i], -1);
	y += extents.height;
    }
}

cairo_font_extents_t *shadow_font_init(char *font)
{
    char fontname[64];
    int fontsize, rc;

    rc = sscanf(font, "%63[^-]-%d", fontname, &fontsize);
    if (rc != 2) {
        rc = sscanf(font, "%63[^:]:size=%d", fontname, &fontsize);
        if (rc != 2) {
            strncpy(fontname, font, sizeof(fontname));
            fontsize = 16;
        }
    }

    cairo_select_font_face(context, fontname,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context, fontsize);
    cairo_font_extents(context, &extents);
    return &extents;
}
