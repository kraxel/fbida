#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "readers.h"
#include "viewer.h"
#include "lut.h"

/* ----------------------------------------------------------------------- */

struct op_map_parm_ch op_map_nothing = {
    gamma:  1,
    bottom: 0,
    top:    255,
    left:   0,
    right:  255
};

struct op_map_lut {
    unsigned char red[256];
    unsigned char green[256];
    unsigned char blue[256];
};

/* ----------------------------------------------------------------------- */
/* functions                                                               */

static void build_lut(struct op_map_parm_ch *arg, unsigned char *lut)
{
    int i,val;
    int inrange,outrange;
    float p;

    inrange  = arg->right - arg->left +1;
    outrange = arg->top - arg->bottom +1;
    p = 1/arg->gamma;

    for (i = 0; i < arg->left; i++)
	lut[i] = 0;
    for (; i <= arg->right; i++) {
	val  = pow((float)(i-arg->left)/inrange,p) * outrange + 0.5;
	val += arg->bottom;
	if (val < 0)   val = 0;
	if (val > 255) val = 255;
	lut[i] = val;
    }
    for (; i < 256; i++)
	lut[i] = 255;
}

static void*
op_map_init(struct ida_image *src, struct ida_rect *rect,
	    struct ida_image_info *i, void *parm)
{
    struct op_map_parm *args = parm;
    struct op_map_lut *lut;

    lut = malloc(sizeof(*lut));
    build_lut(&args->red,lut->red);
    build_lut(&args->green,lut->green);
    build_lut(&args->blue,lut->blue);

    *i = src->i;
    return lut;
}

static void
op_map_work(struct ida_image *src, struct ida_rect *rect,
	    unsigned char *dst, int line, void *data)
{
    struct op_map_lut *lut = data;
    unsigned char *scanline;
    int i;

    scanline = src->data + line * src->i.width * 3;
    memcpy(dst,scanline,src->i.width * 3);
    if (line < rect->y1 || line >= rect->y2)
	return;
    dst      += 3*rect->x1;
    scanline += 3*rect->x1;
    for (i = rect->x1; i < rect->x2; i++) {
	dst[0] = lut->red[scanline[0]];
	dst[1] = lut->green[scanline[1]];
	dst[2] = lut->blue[scanline[2]];
	scanline += 3;
	dst += 3;
    }
}

/* ----------------------------------------------------------------------- */

struct ida_op desc_map = {
    name:  "map",
    init:  op_map_init,
    work:  op_map_work,
    done:  op_free_done,
};
