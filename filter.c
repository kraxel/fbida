#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "readers.h"
#include "filter.h"

int debug = 0;

/* ----------------------------------------------------------------------- */

static void
op_grayscale(struct ida_image *src, struct ida_rect *rect,
	     unsigned char *dst, int line, void *data)
{
    unsigned char *scanline;
    int i,g;

    scanline = src->data + line * src->i.width * 3;
    memcpy(dst,scanline,src->i.width * 3);
    if (line < rect->y1 || line >= rect->y2)
	return;
    dst      += 3*rect->x1;
    scanline += 3*rect->x1;
    for (i = rect->x1; i < rect->x2; i++) {
	g = (scanline[0]*30 + scanline[1]*59+scanline[2]*11)/100;
	dst[0] = g;
	dst[1] = g;
	dst[2] = g;
	scanline += 3;
	dst += 3;
    }
}

/* ----------------------------------------------------------------------- */

struct op_3x3_handle {
    struct op_3x3_parm filter;
    int *linebuf;
};

static void*
op_3x3_init(struct ida_image *src, struct ida_rect *rect,
	    struct ida_image_info *i, void *parm)
{
    struct op_3x3_parm *args = parm;
    struct op_3x3_handle *h;

    h = malloc(sizeof(*h));
    memcpy(&h->filter,args,sizeof(*args));
    h->linebuf = malloc(sizeof(int)*3*(src->i.width));

    *i = src->i;
    return h;
}

static int inline
op_3x3_calc_pixel(struct op_3x3_parm *p, unsigned char *s1,
		  unsigned char *s2, unsigned char *s3)
{
    int val = 0;

    val += p->f1[0] * s1[0];
    val += p->f1[1] * s1[3];
    val += p->f1[2] * s1[6];
    val += p->f2[0] * s2[0];
    val += p->f2[1] * s2[3];
    val += p->f2[2] * s2[6];
    val += p->f3[0] * s3[0];
    val += p->f3[1] * s3[3];
    val += p->f3[2] * s3[6];
    if (p->mul && p->div)
	val = val * p->mul / p->div;
    val += p->add;
    return val;
}

static void
op_3x3_calc_line(struct ida_image *src, struct ida_rect *rect,
		 int *dst, unsigned int line, struct op_3x3_parm *p)
{
    unsigned char b1[9],b2[9],b3[9];
    unsigned char *s1,*s2,*s3;
    unsigned int i,left,right;

    s1 = src->data + (line-1) * src->i.width * 3;
    s2 = src->data +  line    * src->i.width * 3;
    s3 = src->data + (line+1) * src->i.width * 3;
    if (0 == line)
	s1 = src->data + line * src->i.width * 3;
    if (src->i.height-1 == line)
	s3 = src->data + line * src->i.width * 3;

    left  = rect->x1;
    right = rect->x2;
    if (0 == left) {
	/* left border special case: dup first col */
	memcpy(b1,s1,3);
	memcpy(b2,s2,3);
	memcpy(b3,s3,3);
	memcpy(b1+3,s1,6);
	memcpy(b2+3,s2,6);
	memcpy(b3+3,s3,6);
	dst[0] = op_3x3_calc_pixel(p,b1,b2,b3);
	dst[1] = op_3x3_calc_pixel(p,b1+1,b2+1,b3+1);
	dst[2] = op_3x3_calc_pixel(p,b1+2,b2+2,b3+2);
	left++;
    }
    if (src->i.width == right) {
	/* right border */
	memcpy(b1,s1+src->i.width*3-6,6);
	memcpy(b2,s2+src->i.width*3-6,6);
	memcpy(b3,s3+src->i.width*3-6,6);
	memcpy(b1+3,s1+src->i.width*3-3,3);
	memcpy(b2+3,s2+src->i.width*3-3,3);
	memcpy(b3+3,s3+src->i.width*3-3,3);
	dst[src->i.width*3-3] = op_3x3_calc_pixel(p,b1,b2,b3);
	dst[src->i.width*3-2] = op_3x3_calc_pixel(p,b1+1,b2+1,b3+1);
	dst[src->i.width*3-1] = op_3x3_calc_pixel(p,b1+2,b2+2,b3+2);
	right--;
    }
    
    dst += 3*left;
    s1  += 3*(left-1);
    s2  += 3*(left-1);
    s3  += 3*(left-1);
    for (i = left; i < right; i++) {
	dst[0] = op_3x3_calc_pixel(p,s1++,s2++,s3++);
	dst[1] = op_3x3_calc_pixel(p,s1++,s2++,s3++);
	dst[2] = op_3x3_calc_pixel(p,s1++,s2++,s3++);
	dst += 3;
    }
}

static void
op_3x3_clip_line(unsigned char *dst, int *src, int left, int right)
{
    int i,val;

    src += left*3;
    dst += left*3;
    for (i = left*3; i < right*3; i++) {
	val = *(src++);
	if (val < 0)
	    val = 0;
	if (val > 255)
	    val = 255;
	*(dst++) = val;
    }
}

static void
op_3x3_work(struct ida_image *src, struct ida_rect *rect,
	    unsigned char *dst, int line, void *data)
{
    struct op_3x3_handle *h = data;
    unsigned char *scanline;

    scanline = src->data + line * src->i.width * 3;
    memcpy(dst,scanline,src->i.width * 3);
    if (line < rect->y1 || line >= rect->y2)
	return;

    op_3x3_calc_line(src,rect,h->linebuf,line,&h->filter);
    op_3x3_clip_line(dst,h->linebuf,rect->x1,rect->x2);
}

static void
op_3x3_free(void *data)
{
    struct op_3x3_handle *h = data;

    free(h->linebuf);
    free(h);
}
	    
/* ----------------------------------------------------------------------- */

struct op_sharpe_handle {
    int  factor;
    int  *linebuf;
};

static void*
op_sharpe_init(struct ida_image *src, struct ida_rect *rect,
	       struct ida_image_info *i, void *parm)
{
    struct op_sharpe_parm *args = parm;
    struct op_sharpe_handle *h;

    h = malloc(sizeof(*h));
    h->factor  = args->factor;
    h->linebuf = malloc(sizeof(int)*3*(src->i.width));

    *i = src->i;
    return h;
}

static void
op_sharpe_work(struct ida_image *src, struct ida_rect *rect,
	       unsigned char *dst, int line, void *data)
{
    static struct op_3x3_parm laplace = {
	f1: {  1,  1,  1 },
	f2: {  1, -8,  1 },
	f3: {  1,  1,  1 },
    };
    struct op_sharpe_handle *h = data;
    unsigned char *scanline;
    int i;

    scanline = src->data + line * src->i.width * 3;
    memcpy(dst,scanline,src->i.width * 3);
    if (line < rect->y1 || line >= rect->y2)
	return;

    op_3x3_calc_line(src,rect,h->linebuf,line,&laplace);
    for (i = rect->x1*3; i < rect->x2*3; i++)
	h->linebuf[i] = scanline[i] - h->linebuf[i] * h->factor / 256;
    op_3x3_clip_line(dst,h->linebuf,rect->x1,rect->x2);
}

static void
op_sharpe_free(void *data)
{
    struct op_sharpe_handle *h = data;

    free(h->linebuf);
    free(h);
}

/* ----------------------------------------------------------------------- */

struct op_resize_state {
    float xscale,yscale,inleft;
    float *rowbuf;
    unsigned int width,height,srcrow;
};

static void*
op_resize_init(struct ida_image *src, struct ida_rect *rect,
	       struct ida_image_info *i, void *parm)
{
    struct op_resize_parm *args = parm;
    struct op_resize_state *h;

    h = malloc(sizeof(*h));
    h->width  = args->width;
    h->height = args->height;
    h->xscale = (float)args->width/src->i.width;
    h->yscale = (float)args->height/src->i.height;
    h->rowbuf = malloc(src->i.width * 3 * sizeof(float));
    h->srcrow = 0;
    h->inleft = 1;

    *i = src->i;
    i->width  = args->width;
    i->height = args->height;
    i->dpi    = args->dpi;
    return h;
}

static void
op_resize_work(struct ida_image *src, struct ida_rect *rect,
	       unsigned char *dst, int line, void *data)
{
    struct op_resize_state *h = data;
    float outleft,left,weight,d0,d1,d2;
    unsigned char *csrcline;
    float *fsrcline;
    unsigned int i,sx,dx;

    /* scale y */
    memset(h->rowbuf, 0, src->i.width * 3 * sizeof(float));
    outleft = 1/h->yscale;
    while (outleft > 0  &&  h->srcrow < src->i.height) {
	if (outleft < h->inleft) {
	    weight     = outleft * h->yscale;
	    h->inleft -= outleft;
	    outleft    = 0;
	} else {
	    weight     = h->inleft * h->yscale;
	    outleft   -= h->inleft;
	    h->inleft  = 0;
	}
#if 0
	if (debug)
	    fprintf(stderr,"y:  %6.2f%%: %d/%d => %d/%d\n",
		    weight*100,h->srcrow,src->height,line,h->height);
#endif
	csrcline = src->data + h->srcrow * src->i.width * 3;
	for (i = 0; i < src->i.width * 3; i++)
	    h->rowbuf[i] += (float)csrcline[i] * weight;
	if (0 == h->inleft) {
	    h->inleft = 1;
	    h->srcrow++;
	}
    }

    /* scale x */
    left = 1;
    fsrcline = h->rowbuf;
    for (sx = 0, dx = 0; dx < h->width; dx++) {
	d0 = d1 = d2 = 0;
	outleft = 1/h->xscale;
	while (outleft > 0  &&  dx < h->width  &&  sx < src->i.width) {
	    if (outleft < left) {
		weight   = outleft * h->xscale;
		left    -= outleft;
		outleft  = 0;
	    } else {
		weight   = left * h->xscale;
		outleft -= left;
		left     = 0;
	    }
#if 0
	    if (debug)
		fprintf(stderr," x: %6.2f%%: %d/%d => %d/%d\n",
			weight*100,sx,src->width,dx,h->width);
#endif
	    d0 += fsrcline[3*sx+0] * weight;
	    d1 += fsrcline[3*sx+1] * weight;
	    d2 += fsrcline[3*sx+2] * weight;
	    if (0 == left) {
		left = 1;
		sx++;
	    }
	}
	dst[0] = d0;
	dst[1] = d1;
	dst[2] = d2;
	dst += 3;
    }
}

static void
op_resize_done(void *data)
{
    struct op_resize_state *h = data;

    free(h->rowbuf);
    free(h);
}
    
/* ----------------------------------------------------------------------- */

struct op_rotate_state {
    float angle,sina,cosa;
    struct ida_rect calc;
    int cx,cy;
};

static void*
op_rotate_init(struct ida_image *src, struct ida_rect *rect,
	       struct ida_image_info *i, void *parm)
{
    struct op_rotate_parm *args = parm;
    struct op_rotate_state *h;
    float  diag;

    h = malloc(sizeof(*h));
    h->angle = args->angle * 2 * M_PI / 360;
    h->sina  = sin(h->angle);
    h->cosa  = cos(h->angle);
    h->cx    = (rect->x2 - rect->x1) / 2 + rect->x1;
    h->cy    = (rect->y2 - rect->y1) / 2 + rect->y1;

    /* the area we have to process (worst case: 45°) */
    diag     = sqrt((rect->x2 - rect->x1)*(rect->x2 - rect->x1) +
		    (rect->y2 - rect->y1)*(rect->y2 - rect->y1))/2;
    h->calc.x1 = h->cx - diag;
    h->calc.x2 = h->cx + diag;
    h->calc.y1 = h->cy - diag;
    h->calc.y2 = h->cy + diag;
    if (h->calc.x1 < 0)
	h->calc.x1 = 0;
    if (h->calc.x2 > src->i.width)
	h->calc.x2 = src->i.width;
    if (h->calc.y1 < 0)
	h->calc.y1 = 0;
    if (h->calc.y2 > src->i.height)
	h->calc.y2 = src->i.height;

    *i = src->i;
    return h;
}

static inline
unsigned char* op_rotate_getpixel(struct ida_image *src, struct ida_rect *rect,
				  int sx, int sy, int dx, int dy)
{
    static unsigned char black[] = { 0, 0, 0};

    if (sx < rect->x1 || sx >= rect->x2 ||
	sy < rect->y1 || sy >= rect->y2) {
	if (dx < rect->x1 || dx >= rect->x2 ||
	    dy < rect->y1 || dy >= rect->y2)
	    return src->data + dy * src->i.width * 3 + dx * 3;
	return black;
    }
    return src->data + sy * src->i.width * 3 + sx * 3;
}

static void
op_rotate_work(struct ida_image *src, struct ida_rect *rect,
	       unsigned char *dst, int y, void *data)
{
    struct op_rotate_state *h = data;
    unsigned char *pix;
    float fx,fy,w;
    int x,sx,sy;

    pix = src->data + y * src->i.width * 3;
    memcpy(dst,pix,src->i.width * 3);
    if (y < h->calc.y1 || y >= h->calc.y2)
	return;

    dst += 3*h->calc.x1;
    memset(dst, 0, (h->calc.x2-h->calc.x1) * 3);
    for (x = h->calc.x1; x < h->calc.x2; x++, dst+=3) {
	fx = h->cosa * (x - h->cx) - h->sina * (y - h->cy) + h->cx;
	fy = h->sina * (x - h->cx) + h->cosa * (y - h->cy) + h->cy;
	sx = (int)fx;
	sy = (int)fy;
	if (fx < 0)
	    sx--;
	if (fy < 0)
	    sy--;
	fx -= sx;
	fy -= sy;

	pix = op_rotate_getpixel(src,rect,sx,sy,x,y);
	w = (1-fx) * (1-fy);
	dst[0] += pix[0] * w;
	dst[1] += pix[1] * w;
	dst[2] += pix[2] * w;
	pix = op_rotate_getpixel(src,rect,sx+1,sy,x,y);
	w = fx * (1-fy);
	dst[0] += pix[0] * w;
	dst[1] += pix[1] * w;
	dst[2] += pix[2] * w;
	pix = op_rotate_getpixel(src,rect,sx,sy+1,x,y);
	w = (1-fx) * fy;
	dst[0] += pix[0] * w;
	dst[1] += pix[1] * w;
	dst[2] += pix[2] * w;
	pix = op_rotate_getpixel(src,rect,sx+1,sy+1,x,y);
	w = fx * fy;
	dst[0] += pix[0] * w;
	dst[1] += pix[1] * w;
	dst[2] += pix[2] * w;
    }
}

static void
op_rotate_done(void *data)
{
    struct op_rotate_state *h = data;

    free(h);
}

/* ----------------------------------------------------------------------- */

struct ida_op desc_grayscale = {
    name:  "grayscale",
    init:  op_none_init,
    work:  op_grayscale,
    done:  op_none_done,
};
struct ida_op desc_3x3 = {
    name:  "3x3",
    init:  op_3x3_init,
    work:  op_3x3_work,
    done:  op_3x3_free,
};
struct ida_op desc_sharpe = {
    name:  "sharpe",
    init:  op_sharpe_init,
    work:  op_sharpe_work,
    done:  op_sharpe_free,
};
struct ida_op desc_resize = {
    name:  "resize",
    init:  op_resize_init,
    work:  op_resize_work,
    done:  op_resize_done,
};
struct ida_op desc_rotate = {
    name:  "rotate",
    init:  op_rotate_init,
    work:  op_rotate_work,
    done:  op_rotate_done,
};
