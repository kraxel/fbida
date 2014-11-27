#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "readers.h"
#include "op.h"
#include "filter.h"

/* ----------------------------------------------------------------------- */
/* functions                                                               */

static char op_none_data;

static void
op_flip_vert(struct ida_image *src, struct ida_rect *rect,
	     unsigned char *dst, int line, void *data)
{
    char *scanline;

    scanline = src->data + (src->i.height - line - 1) * src->i.width * 3;
    memcpy(dst,scanline,src->i.width*3);
}

static void
op_flip_horz(struct ida_image *src, struct ida_rect *rect,
	     unsigned char *dst, int line, void *data)
{
    char *scanline;
    unsigned int i;

    scanline = src->data + (line+1) * src->i.width * 3;
    for (i = 0; i < src->i.width; i++) {
	scanline -= 3;
	dst[0] = scanline[0];
	dst[1] = scanline[1];
	dst[2] = scanline[2];
	dst += 3;
    }
}

static void*
op_rotate_init(struct ida_image *src, struct ida_rect *rect,
	       struct ida_image_info *i, void *parm)
{
    *i = src->i;
    i->height = src->i.width;
    i->width  = src->i.height;
    i->dpi    = src->i.dpi;
    return &op_none_data;
}

static void
op_rotate_cw(struct ida_image *src, struct ida_rect *rect,
	     unsigned char *dst, int line, void *data)
{
    char *pix;
    unsigned int i;

    pix = src->data + src->i.width * src->i.height * 3 + line * 3;
    for (i = 0; i < src->i.height; i++) {
	pix -= src->i.width * 3;
	dst[0] = pix[0];
	dst[1] = pix[1];
	dst[2] = pix[2];
	dst += 3;
    }
}

static void
op_rotate_ccw(struct ida_image *src, struct ida_rect *rect,
	      unsigned char *dst, int line, void *data)
{
    char *pix;
    unsigned int i;

    pix = src->data + (src->i.width-line-1) * 3;
    for (i = 0; i < src->i.height; i++) {
	dst[0] = pix[0];
	dst[1] = pix[1];
	dst[2] = pix[2];
	pix += src->i.width * 3;
	dst += 3;
    }
}

static void
op_invert(struct ida_image *src, struct ida_rect *rect,
	  unsigned char *dst, int line, void *data)
{
    unsigned char *scanline;
    int i;

    scanline = src->data + line * src->i.width * 3;
    memcpy(dst,scanline,src->i.width * 3);
    if (line < rect->y1 || line >= rect->y2)
	return;
    dst      += 3*rect->x1;
    scanline += 3*rect->x1;
    for (i = rect->x1; i < rect->x2; i++) {
	dst[0] = 255-scanline[0];
	dst[1] = 255-scanline[1];
	dst[2] = 255-scanline[2];
	scanline += 3;
	dst += 3;
    }
}

static void*
op_crop_init(struct ida_image *src, struct ida_rect *rect,
	     struct ida_image_info *i, void *parm)
{
    if (rect->x2 - rect->x1 == src->i.width &&
	rect->y2 - rect->y1 == src->i.height)
	return NULL;
    *i = src->i;
    i->width  = rect->x2 - rect->x1;
    i->height = rect->y2 - rect->y1;
    return &op_none_data;
}

static void
op_crop_work(struct ida_image *src, struct ida_rect *rect,
	     unsigned char *dst, int line, void *data)
{
    unsigned char *scanline;
    int i;

    scanline = src->data + (line+rect->y1) * src->i.width * 3 + rect->x1 * 3;
    for (i = rect->x1; i < rect->x2; i++) {
	dst[0] = scanline[0];
	dst[1] = scanline[1];
	dst[2] = scanline[2];
	scanline += 3;
	dst += 3;
    }
}

static void*
op_autocrop_init(struct ida_image *src, struct ida_rect *unused,
		 struct ida_image_info *i, void *parm)
{
    static struct op_3x3_parm filter = {
	f1: { -1, -1, -1 },
	f2: { -1,  8, -1 },
	f3: { -1, -1, -1 },
    };
    struct ida_rect rect;
    struct ida_image img;
    int x,y,limit;
    unsigned char *line;
    void *data;
    
    /* detect edges */
    rect.x1 = 0;
    rect.x2 = src->i.width;
    rect.y1 = 0;
    rect.y2 = src->i.height;
    data = desc_3x3.init(src, &rect, &img.i, &filter);

    img.data   = malloc(img.i.width * img.i.height * 3);
    for (y = 0; y < (int)img.i.height; y++)
	desc_3x3.work(src, &rect, img.data+3*img.i.width*y, y, data);
    desc_3x3.done(data);
    limit = 64;

    /* y border */
    for (y = 0; y < (int)img.i.height; y++) {
	line = img.data + img.i.width*y*3;
	for (x = 0; x < (int)img.i.width; x++)
	    if (line[3*x+0] > limit ||
		line[3*x+1] > limit ||
		line[3*x+2] > limit)
		break;
	if (x != (int)img.i.width)
	    break;
    }
    rect.y1 = y;
    for (y = (int)img.i.height-1; y > rect.y1; y--) {
	line = img.data + img.i.width*y*3;
	for (x = 0; x < (int)img.i.width; x++)
	    if (line[3*x+0] > limit ||
		line[3*x+1] > limit ||
		line[3*x+2] > limit)
		break;
	if (x != (int)img.i.width)
	    break;
    }
    rect.y2 = y+1;

    /* x border */
    for (x = 0; x < (int)img.i.width; x++) {
	for (y = 0; y < (int)img.i.height; y++) {
	    line = img.data + (img.i.width*y+x) * 3;
	    if (line[0] > limit ||
		line[1] > limit ||
		line[2] > limit)
		break;
	}
	if (y != (int)img.i.height)
	    break;
    }
    rect.x1 = x;
    for (x = (int)img.i.width-1; x > rect.x1; x--) {
	for (y = 0; y < (int)img.i.height; y++) {
	    line = img.data + (img.i.width*y+x) * 3;
	    if (line[0] > limit ||
		line[1] > limit ||
		line[2] > limit)
		break;
	}
	if (y != (int)img.i.height)
	    break;
    }
    rect.x2 = x+1;

    free(img.data);
    if (debug)
	fprintf(stderr,"y: %d-%d/%u  --  x: %d-%d/%u\n",
		rect.y1, rect.y2, img.i.height,
		rect.x1, rect.x2, img.i.width);

    if (0 == rect.x2 - rect.x1  ||  0 == rect.y2 - rect.y1)
	return NULL;
    
    *unused = rect;
    *i = src->i;
    i->width  = rect.x2 - rect.x1;
    i->height = rect.y2 - rect.y1;
    return &op_none_data;
}

/* ----------------------------------------------------------------------- */

static char op_none_data;

void* op_none_init(struct ida_image *src,  struct ida_rect *sel,
		   struct ida_image_info *i, void *parm)
{
    *i = src->i;
    return &op_none_data;
}

void  op_none_done(void *data) {}
void  op_free_done(void *data) { free(data); }

/* ----------------------------------------------------------------------- */

struct ida_op desc_flip_vert = {
    name:  "flip-vert",
    init:  op_none_init,
    work:  op_flip_vert,
    done:  op_none_done,
};
struct ida_op desc_flip_horz = {
    name:  "flip-horz",
    init:  op_none_init,
    work:  op_flip_horz,
    done:  op_none_done,
};
struct ida_op desc_rotate_cw = {
    name:  "rotate-cw",
    init:  op_rotate_init,
    work:  op_rotate_cw,
    done:  op_none_done,
};
struct ida_op desc_rotate_ccw = {
    name:  "rotate-ccw",
    init:  op_rotate_init,
    work:  op_rotate_ccw,
    done:  op_none_done,
};
struct ida_op desc_invert = {
    name:  "invert",
    init:  op_none_init,
    work:  op_invert,
    done:  op_none_done,
};
struct ida_op desc_crop = {
    name:  "crop",
    init:  op_crop_init,
    work:  op_crop_work,
    done:  op_none_done,
};
struct ida_op desc_autocrop = {
    name:  "autocrop",
    init:  op_autocrop_init,
    work:  op_crop_work,
    done:  op_none_done,
};
