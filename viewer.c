#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>

#include "ida.h"
#include "x11.h"
#include "dither.h"
#include "readers.h"
#include "viewer.h"
#include "hex.h"
#include "idaconfig.h"

/* ----------------------------------------------------------------------- */

#define POINTER_NORMAL    0
#define POINTER_BUSY      1
#define POINTER_PICK      2
#define RUBBER_NEW        3
#define RUBBER_MOVE       4
#define RUBBER_X1         5
#define RUBBER_Y1         6
#define RUBBER_X2         7
#define RUBBER_Y2         8

#define RUBBER_RANGE      6
#define RUBBER_INTERVAL 100

#define PROCESS_LINES    16

int debug;
Cursor ptrs[POINTER_COUNT];

/* ----------------------------------------------------------------------- */

Pixmap image_to_pixmap(struct ida_image *img)
{
    unsigned char line[256],*src;
    XImage *ximage;
    void *shm;
    Pixmap pix;
    GC gc;
    unsigned int x,y;

    ximage = x11_create_ximage(app_shell, img->i.width, img->i.height, &shm);
    for (y = 0; y < img->i.height; y++) {
	src = img->data + 3*y*img->i.width;
	if (display_type == PSEUDOCOLOR) {
	    dither_line(src, line, y, img->i.width);
	    for (x = 0; x < img->i.width; x++)
		XPutPixel(ximage, x, y, x11_map[line[x]]);
	} else {
	    for (x = 0; x < img->i.width; x++, src += 3) {
		pix = x11_lut_red[src[0]] |
		    x11_lut_green[src[1]] |
		    x11_lut_blue[src[2]];
		XPutPixel(ximage, x, y, pix);
	    }
	}
    }
    pix = XCreatePixmap(dpy,XtWindow(app_shell),img->i.width, img->i.height,
			DefaultDepthOfScreen(XtScreen(app_shell)));
    gc = XCreateGC(dpy, pix, 0, NULL);
    XPUTIMAGE(dpy, pix, gc, ximage, 0, 0, 0, 0, img->i.width, img->i.height);
    XFreeGC(dpy, gc);
    x11_destroy_ximage(app_shell, ximage, shm);
    return pix;
}

/* ----------------------------------------------------------------------- */

int viewer_i2s(int zoom, int val)
{
    if (0 > zoom)
	return val/(-zoom+1);
    if (0 < zoom)
	return val*(zoom+1);
    return val;
}

/* ----------------------------------------------------------------------- */

static void
viewer_renderline(struct ida_viewer *ida, char *scanline)
{
    unsigned char *src,*dst,*rgb;
    unsigned long pix;
    unsigned int x,s,scrline;

    src = scanline;

    if (0 == ida->zoom) {
	/* as-is */
	if (display_type == PSEUDOCOLOR) {
	    dst = ida->dither_line;
	    dither_line(src, dst, ida->line, ida->scrwidth);
	    for (x = 0; x < ida->scrwidth; x++, dst++)
		XPutPixel(ida->ximage, x, ida->line, x11_map[*dst]);
	} else {
	    for (x = 0; x < ida->scrwidth; x++, src += 3) {
		pix = x11_lut_red[src[0]] |
		    x11_lut_green[src[1]] |
		    x11_lut_blue[src[2]];
		XPutPixel(ida->ximage, x, ida->line, pix);
	    }
	}

    } else if (ida->zoom < 0) {
	/* zoom out */
	s = -ida->zoom+1;
	if (s-1 != (ida->line % s))
	    return;
	scrline = ida->line/s;
	if (display_type == PSEUDOCOLOR) {
	    rgb = ida->rgb_line;
	    for (x = 0; x < ida->scrwidth; x++, rgb += 3, src += 3*s) {
		rgb[0] = src[0];
		rgb[1] = src[1];
		rgb[2] = src[2];
	    }
	    rgb = ida->rgb_line;
	    dst = ida->dither_line;
	    dither_line(rgb, dst, scrline, ida->scrwidth);
	    for (x = 0; x < ida->scrwidth; x++, dst++)
		XPutPixel(ida->ximage, x, scrline, x11_map[*dst]);
	} else {
#if 0
	    /* just drop pixels */
	    for (x = 0; x < ida->scrwidth; x++, src += 3*s) {
		pix = x11_lut_red[src[0]] |
		    x11_lut_green[src[1]] |
		    x11_lut_blue[src[2]];
		XPutPixel(ida->ximage, x, scrline, pix);
	    }
#else
	    /* horizontal interpolation (vertical is much harder ...) */
	    for (x = 0; x < ida->scrwidth; x++, src += 3*s) {
		int red,green,blue,count,ix;
		red   = 0;
		green = 0;
		blue  = 0;
		count = 0;
		for (ix = 0; ix < 3*s; ix += 3) {
		    red   += src[ix+0];
		    green += src[ix+1]; 
		    blue  += src[ix+2]; 
		    count += 1;
		}
		pix = x11_lut_red[red/count] |
		    x11_lut_green[green/count] |
		    x11_lut_blue[blue/count];
		XPutPixel(ida->ximage, x, scrline, pix);
	    }
#endif
	}
	
    } else {
	/* zoom in */
	s = ida->zoom+1;
	if (display_type == PSEUDOCOLOR) {
	    rgb = ida->rgb_line;
	    for (x = 0; x < ida->scrwidth; rgb += 3) {
		rgb[0] = src[0];
		rgb[1] = src[1];
		rgb[2] = src[2];
		x++;
		if (0 == (x%s))
		    src += 3;
	    }
	    for (scrline = ida->line*s; scrline < ida->line*s+s; scrline++) {
		rgb = ida->rgb_line;
		dst = ida->dither_line;
		dither_line(rgb, dst, scrline, ida->scrwidth);
		for (x = 0; x < ida->scrwidth; x++, dst++)
		    XPutPixel(ida->ximage, x, scrline, x11_map[*dst]);
	    }
	} else {
	    for (scrline = ida->line*s; scrline < ida->line*s+s; scrline++) {
		src = scanline;
		for (x = 0; x < ida->scrwidth; src += 3) {
		    unsigned int i;
		    pix = x11_lut_red[src[0]] |
			x11_lut_green[src[1]] |
			x11_lut_blue[src[2]];
		    for (i = 0; i < s; i++, x++)
			XPutPixel(ida->ximage, x, scrline, pix);
		}
	    }
	}
    }
}

/* ----------------------------------------------------------------------- */

static void
viewer_cleanup(struct ida_viewer *ida)
{
    if (ida->load_read) {
	ida->load_done(ida->load_data);
	ida->load_line = 0;
	ida->load_read = NULL;
	ida->load_done = NULL;
	ida->load_data = NULL;
    }
    if (ida->op_work) {
	ida->op_done(ida->op_data);
	ida->op_line = 0;
	ida->op_work = NULL;
	ida->op_done = NULL;
	ida->op_data = NULL;
	if (ida->op_src.data) {
	    if (ida->undo.data) {
		fprintf(stderr,"have undo buffer /* shouldn't happen */");
		free(ida->undo.data);
	    }
	    ida->undo = ida->op_src;
	    memset(&ida->op_src,0,sizeof(ida->op_src));
	}
    }
}

static Boolean
viewer_workproc(XtPointer client_data)
{
    struct ida_viewer *ida = client_data;
    unsigned int start,end;
    char *scanline;

    start = ida->line;
    end   = ida->line + ida->steps;
    if (end > ida->img.i.height)
	end = ida->img.i.height;

    /* image loading */
    if (ida->load_read) {
	for (ida->line = start; ida->line < end; ida->line++) {
	    if (ida->load_line > ida->line)
		continue;
	    scanline = ida->img.data + ida->img.i.width * ida->load_line * 3;
	    ida->load_read(scanline,ida->load_line,ida->load_data);
	    ida->load_line++;
	}
    }

    /* image processing */
    if (ida->op_work  &&  0 == ida->op_preview) {
	for (ida->line = start; ida->line < end; ida->line++) {
	    if (ida->op_line > ida->line)
		continue;
	    scanline = ida->img.data + ida->img.i.width * ida->op_line * 3;
	    ida->op_work(&ida->op_src,&ida->op_rect,
			scanline,ida->op_line,ida->op_data);
	    ida->op_line++;
	}
    }

    /* image rendering */
    if (ida->op_work  && ida->op_preview) {
	for (ida->line = start; ida->line < end; ida->line++) {
	    ida->op_line = ida->line;
	    ida->op_work(&ida->img,&ida->op_rect,
			 ida->preview_line,ida->line,ida->op_data);
	    viewer_renderline(ida,ida->preview_line);
	}
    } else {
	for (ida->line = start; ida->line < end; ida->line++) {
	    scanline = ida->img.data + ida->img.i.width * ida->line * 3;
	    viewer_renderline(ida,scanline);
	}
    }

    /* trigger redraw */
    XClearArea(XtDisplay(ida->widget), XtWindow(ida->widget),
	       0, viewer_i2s(ida->zoom,start),
	       ida->scrwidth, viewer_i2s(ida->zoom,ida->steps), True);

    /* all done ? */
    if (ida->line == ida->img.i.height) {
	viewer_cleanup(ida);
	ida->wproc = 0;
#if 1
	if (args.testload)
	    XtCallActionProc(ida->widget,"Next",NULL,NULL,0);
#endif
	return TRUE;
    }
    return FALSE;
}

static void viewer_workstart(struct ida_viewer *ida)
{
    /* (re-) start */
    ida->line  = 0;
    if (!ida->wproc)
	ida->wproc = XtAppAddWorkProc(app_context,viewer_workproc,ida);
}

static void viewer_workstop(struct ida_viewer *ida)
{
    if (!ida->wproc)
	return;

    viewer_cleanup(ida);
    XtRemoveWorkProc(ida->wproc);
    ida->wproc = 0;
}

static void viewer_workfinish(struct ida_viewer *ida)
{
    char *scanline;

    if (ida->load_read) {
	for (ida->line = ida->load_line; ida->line < ida->img.i.height;) {
	    scanline = ida->img.data + ida->img.i.width * ida->line * 3;
	    ida->load_read(scanline,ida->load_line,ida->load_data);
	    ida->line++;
	    ida->load_line++;
	}
    }
    if (ida->op_work && 0 == ida->op_preview) {
	for (ida->line = ida->op_line; ida->line < ida->img.i.height;) {
	    scanline = ida->img.data + ida->img.i.width * ida->line * 3;
	    ida->op_work(&ida->op_src,&ida->op_rect,
			scanline,ida->op_line,ida->op_data);
	    ida->line++;
	    ida->op_line++;
	}
    }
    viewer_workstop(ida);
}

/* ----------------------------------------------------------------------- */

static void
viewer_new_view(struct ida_viewer *ida)
{
    if (NULL != ida->ximage)
	x11_destroy_ximage(ida->widget,ida->ximage,ida->ximage_shm);
    if (NULL != ida->rgb_line)
	free(ida->rgb_line);
    if (NULL != ida->dither_line)
	free(ida->dither_line);
    if (NULL != ida->preview_line)
	free(ida->preview_line);

    ida->scrwidth  = viewer_i2s(ida->zoom,ida->img.i.width);
    ida->scrheight = viewer_i2s(ida->zoom,ida->img.i.height);
    ida->steps = PROCESS_LINES;
    if (ida->zoom < 0)
	while ((ida->steps % (-ida->zoom+1)) != 0)
	    ida->steps++;

    ida->rgb_line = malloc(ida->scrwidth*3);
    ida->dither_line = malloc(ida->scrwidth);
    ida->preview_line = malloc(ida->img.i.width*3);
    ida->ximage = x11_create_ximage(ida->widget, ida->scrwidth, ida->scrheight,
				   &ida->ximage_shm);
    if (NULL == ida->ximage) {
	ida->zoom--;
	return viewer_new_view(ida);
    }
    XtVaSetValues(ida->widget,
		  XtNwidth,  ida->scrwidth,
		  XtNheight, ida->scrheight,
		  NULL);
    viewer_workstart(ida);
}

static void
viewer_timeout(XtPointer client_data, XtIntervalId *id);

static int
viewer_rubber_draw(struct ida_viewer *ida)
{
    XGCValues values;
    struct ida_rect r = ida->current;
    int x,y,w,h;

    values.function   = GXxor;
    values.foreground = ida->mask;
    XChangeGC(dpy,ida->wgc,GCFunction|GCForeground,&values);
    if (r.x1 < r.x2) {
	x = viewer_i2s(ida->zoom,r.x1);
	w = viewer_i2s(ida->zoom,r.x2 - r.x1);
    } else {
	x = viewer_i2s(ida->zoom,r.x2);
	w = viewer_i2s(ida->zoom,r.x1 - r.x2);
    }
    if (r.y1 < r.y2) {
	y = viewer_i2s(ida->zoom,r.y1);
	h = viewer_i2s(ida->zoom,r.y2 - r.y1);
    } else {
	y = viewer_i2s(ida->zoom,r.y2);
	h = viewer_i2s(ida->zoom,r.y1 - r.y2);
    }
    if (0 == h && 0 == w)
	return 0;
    if (w)
	w--;
    if (h)
	h--;
    XDrawRectangle(dpy,XtWindow(ida->widget),ida->wgc,x,y,w,h);
    return 1;
}

static void
viewer_rubber_off(struct ida_viewer *ida)
{
    if (ida->marked)
	viewer_rubber_draw(ida);
    ida->marked = 0;
    if (ida->timer)
	XtRemoveTimeOut(ida->timer);
    ida->timer = 0;
}

static void
viewer_rubber_on(struct ida_viewer *ida)
{
    ida->marked = viewer_rubber_draw(ida);
    if (ida->marked)
	ida->timer = XtAppAddTimeOut(app_context,RUBBER_INTERVAL,
				    viewer_timeout,ida);
}

static void
viewer_timeout(XtPointer client_data, XtIntervalId *id)
{
    struct ida_viewer *ida = client_data;

    ida->timer = 0;
    viewer_rubber_off(ida);
    ida->mask <<= 1;
    if ((ida->mask & 0x10) == 0x10)
	ida->mask |= 0x01;
    viewer_rubber_on(ida);
}

static void
viewer_redraw(Widget widget, XtPointer client_data,
	      XEvent *ev, Boolean *cont)
{
    struct ida_viewer *ida = client_data;
    XExposeEvent *event;
    XGCValues values;

    if (ev->type != Expose)
	return;
    event = (XExposeEvent*)ev;
    
    if (NULL == ida->ximage)
	return;
    if (event->x + event->width > (int)ida->scrwidth)
	return;
    if (event->y + event->height > (int)ida->scrheight)
	return;
    if (NULL == ida->wgc)
	ida->wgc = XCreateGC(XtDisplay(widget), XtWindow(widget), 0, NULL);

    viewer_rubber_off(ida);
    values.function   = GXcopy;
    XChangeGC(dpy,ida->wgc,GCFunction,&values);
    XPUTIMAGE(XtDisplay(ida->widget), XtWindow(widget),
	      ida->wgc, ida->ximage,
	      event->x, event->y, event->x, event->y,
	      event->width, event->height);
    viewer_rubber_on(ida);
}

static int
viewer_pos2state(struct ida_viewer *ida, int x, int y)
{
    int x1,x2,y1,y2;

    if (POINTER_PICK == ida->state)
	return ida->state;

    x1 = viewer_i2s(ida->zoom,ida->current.x1);
    x2 = viewer_i2s(ida->zoom,ida->current.x2);
    y1 = viewer_i2s(ida->zoom,ida->current.y1);
    y2 = viewer_i2s(ida->zoom,ida->current.y2);
    if ((x1 < x && x < x2) || (x2 < x && x < x1)) {
	if (y1-RUBBER_RANGE < y && y < y1+RUBBER_RANGE)
	    return RUBBER_Y1;
	if (y2-RUBBER_RANGE < y && y < y2+RUBBER_RANGE)
	    return RUBBER_Y2;
    }
    if ((y1 < y && y < y2) || (y2 < y && y < y1)) {
	if (x1-RUBBER_RANGE < x && x < x1+RUBBER_RANGE)
	    return RUBBER_X1;
	if (x2-RUBBER_RANGE < x && x < x2+RUBBER_RANGE)
	    return RUBBER_X2;
    }
    if (((x1 < x && x < x2) || (x2 < x && x < x1)) &&
	((y1 < y && y < y2) || (y2 < y && y < y1)))
	return RUBBER_MOVE;
    return RUBBER_NEW;
}

static void
viewer_mouse(Widget widget, XtPointer client_data,
	     XEvent *ev, Boolean *cont)
{
    struct ida_viewer *ida = client_data;
    int state = POINTER_NORMAL;
    unsigned char *pix;
    int x,y;
    
    viewer_rubber_off(ida);

    switch (ev->type) {
    case ButtonPress:
    {
	XButtonEvent *eb = (XButtonEvent*)ev;

	if (eb->button != Button1)
	    goto out;
	ida->state = viewer_pos2state(ida,eb->x,eb->y);
	switch (ida->state) {
	case POINTER_PICK:
	    x = viewer_i2s(-ida->zoom,eb->x);
	    y = viewer_i2s(-ida->zoom,eb->y);
	    pix = ida->img.data + ida->img.i.width*y*3 + x*3;
	    ida->pick_cb(x,y,pix,ida->pick_data);
	    ida->pick_cb = NULL;
	    ida->pick_data = NULL;
	    ida->state = POINTER_NORMAL;
	    state = POINTER_NORMAL;
	    break;
	case RUBBER_NEW:
	    ida->mask = 0x33333333;
	    ida->current.x1 = ida->current.x2 = viewer_i2s(-ida->zoom,eb->x);
	    ida->current.y1 = ida->current.y2 = viewer_i2s(-ida->zoom,eb->y);
	    break;
	case RUBBER_MOVE:
	    ida->last_x = viewer_i2s(-ida->zoom,eb->x);
	    ida->last_y = viewer_i2s(-ida->zoom,eb->y);
	    break;
	case RUBBER_X1:
	    ida->current.x1 = viewer_i2s(-ida->zoom,eb->x);
	    break;
	case RUBBER_Y1:
	    ida->current.y1 = viewer_i2s(-ida->zoom,eb->y);
	    break;
	case RUBBER_X2:
	    ida->current.x2 = viewer_i2s(-ida->zoom,eb->x);
	    break;
	case RUBBER_Y2:
	    ida->current.y2 = viewer_i2s(-ida->zoom,eb->y);
	    break;
	}
	state = ida->state;
	break;
    }
    case MotionNotify:
    {
	XMotionEvent *em = (XMotionEvent*)ev;

	if (!(em->state & Button1Mask)) {
	    state = viewer_pos2state(ida,em->x,em->y);
	    goto out;
	}
	switch (ida->state) {
	case RUBBER_NEW:
	    ida->current.x2 = viewer_i2s(-ida->zoom,em->x);
	    ida->current.y2 = viewer_i2s(-ida->zoom,em->y);
	    if (em->state & ShiftMask) {
		/* square selection */
		int xlen,ylen;
		xlen = abs(ida->current.x1 - ida->current.x2);
		ylen = abs(ida->current.y1 - ida->current.y2);
		if (ylen > xlen) {
		    if (ida->current.x1 < ida->current.x2)
			ida->current.x2 -= (xlen - ylen);
		    else
			ida->current.x2 += (xlen - ylen);
		} else {
		    if (ida->current.y1 < ida->current.y2)
			ida->current.y2 -= (ylen - xlen);
		    else
			ida->current.y2 += (ylen - xlen);
		}
	    }
	    break;
	case RUBBER_MOVE:
	    x = viewer_i2s(-ida->zoom,em->x);
	    y = viewer_i2s(-ida->zoom,em->y);
	    ida->current.x1 += (x - ida->last_x);
	    ida->current.x2 += (x - ida->last_x);
	    ida->current.y1 += (y - ida->last_y);
	    ida->current.y2 += (y - ida->last_y);
	    ida->last_x = x;
	    ida->last_y = y;
	    break;
	case RUBBER_X1:
	    ida->current.x1 = viewer_i2s(-ida->zoom,em->x);
	    break;
	case RUBBER_Y1:
	    ida->current.y1 = viewer_i2s(-ida->zoom,em->y);
	    break;
	case RUBBER_X2:
	    ida->current.x2 = viewer_i2s(-ida->zoom,em->x);
	    break;
	case RUBBER_Y2:
	    ida->current.y2 = viewer_i2s(-ida->zoom,em->y);
	    break;
	}
	state = ida->state;
	break;
    }
    case ButtonRelease:
    {
	XButtonEvent *eb = (XButtonEvent*)ev;

	if (eb->button != Button1)
	    goto out;
	ida->state = POINTER_NORMAL;
	state = ida->state;
	break;
    }
    }

    if (ida->current.x1 < 0)
	ida->current.x1 = 0;
    if (ida->current.x1 > ida->img.i.width)
	ida->current.x1 = ida->img.i.width;
    if (ida->current.x2 < 0)
	ida->current.x2 = 0;
    if (ida->current.x2 > ida->img.i.width)
	ida->current.x2 = ida->img.i.width;
    if (ida->current.y1 < 0)
	ida->current.y1 = 0;
    if (ida->current.y1 > ida->img.i.height)
	ida->current.y1 = ida->img.i.height;
    if (ida->current.y2 < 0)
	ida->current.y2 = 0;
    if (ida->current.y2 > ida->img.i.height)
	ida->current.y2 = ida->img.i.height;

 out:
    XDefineCursor(dpy, XtWindow(widget), ptrs[state]);
    viewer_rubber_on(ida);
}

/* ----------------------------------------------------------------------- */
/* public stuff                                                            */

void viewer_pick(struct ida_viewer *ida, viewer_pick_cb cb, XtPointer data)
{
    if (POINTER_NORMAL != ida->state)
	return;
    if (debug)
	fprintf(stderr,"viewer_pick\n");
    ida->state = POINTER_PICK;
    ida->pick_cb   = cb;
    ida->pick_data = data;
}

void viewer_unpick(struct ida_viewer *ida)
{
    if (POINTER_PICK != ida->state)
	return;
    if (debug)
	fprintf(stderr,"viewer_unpick\n");
    ida->state = POINTER_NORMAL;
    ida->pick_cb   = NULL;
    ida->pick_data = NULL;
}

void
viewer_autozoom(struct ida_viewer *ida)
{
    if (GET_AUTOZOOM()) {
	ida->zoom = 0;
	while (XtScreen(ida->widget)->width  < viewer_i2s(ida->zoom,ida->img.i.width) ||
	       XtScreen(ida->widget)->height < viewer_i2s(ida->zoom,ida->img.i.height))
	    ida->zoom--;
    }
    viewer_new_view(ida);
}

void
viewer_setzoom(struct ida_viewer *ida, int zoom)
{
    ida->zoom = zoom;
    viewer_new_view(ida);
}

static void
viewer_op_rect(struct ida_viewer *ida)
{
    if (ida->current.x1 == ida->current.x2 &&
	ida->current.y1 == ida->current.y2) {
	/* full image */
	ida->op_rect.x1 = 0;
	ida->op_rect.x2 = ida->img.i.width;
	ida->op_rect.y1 = 0;
	ida->op_rect.y2 = ida->img.i.height;
	return;
    } else {
	/* have selection */
	if (ida->current.x1 < ida->current.x2) {
	    ida->op_rect.x1 = ida->current.x1;
	    ida->op_rect.x2 = ida->current.x2;
	} else {
	    ida->op_rect.x1 = ida->current.x2;
	    ida->op_rect.x2 = ida->current.x1;
	}
	if (ida->current.y1 < ida->current.y2) {
	    ida->op_rect.y1 = ida->current.y1;
	    ida->op_rect.y2 = ida->current.y2;
	} else {
	    ida->op_rect.y1 = ida->current.y2;
	    ida->op_rect.y2 = ida->current.y1;
	}
    }
}

int
viewer_start_op(struct ida_viewer *ida, struct ida_op *op, void *parm)
{
    struct ida_image dst;

    ptr_busy();
    viewer_workfinish(ida);
    viewer_rubber_off(ida);

    /* try init */
    viewer_op_rect(ida);
    if (debug)
	fprintf(stderr,"viewer_start_op: init %s(%p)\n",op->name,parm);
    ida->op_data = op->init(&ida->img,&ida->op_rect,&dst.i,parm);
    ptr_idle();
    if (NULL == ida->op_data)
	return -1;
    dst.data = malloc(dst.i.width * dst.i.height * 3);

    /* prepare background processing */
    if (ida->undo.data) {
	free(ida->undo.data);
	memset(&ida->undo,0,sizeof(ida->undo));
    }
    if (ida->op_src.data) {
	fprintf(stderr,"have op_src buffer /* shouldn't happen */");
	free(ida->op_src.data);
    }
    ida->op_src = ida->img;
    ida->img = dst;
    ida->op_line = 0;
    ida->op_work = op->work;
    ida->op_done = op->done;
    ida->op_preview = 0;

    if (ida->op_src.i.width  != ida->img.i.width ||
	ida->op_src.i.height != ida->img.i.height) {
	memset(&ida->current,0,sizeof(ida->current));
	viewer_autozoom(ida);
    } else
	viewer_new_view(ida);
    return 0;
}

int
viewer_undo(struct ida_viewer *ida)
{
    int resize;

    viewer_workfinish(ida);
    if (NULL == ida->undo.data)
	return -1;
    viewer_rubber_off(ida);
    memset(&ida->current,0,sizeof(ida->current));
    
    resize = (ida->undo.i.width  != ida->img.i.width ||
	      ida->undo.i.height != ida->img.i.height);
    free(ida->img.data);
    ida->img = ida->undo;
    memset(&ida->undo,0,sizeof(ida->undo));

    if (resize)
	viewer_autozoom(ida);
    else
	viewer_new_view(ida);
    return 0;
}

int
viewer_start_preview(struct ida_viewer *ida, struct ida_op *op, void *parm)
{
    struct ida_image dst;

    viewer_workfinish(ida);

    /* try init */
    viewer_op_rect(ida);
    ida->op_data = op->init(&ida->img,&ida->op_rect,&dst.i,parm);
    if (NULL == ida->op_data)
	return -1;

    /* prepare background preview */
    ida->op_line = 0;
    ida->op_work = op->work;
    ida->op_done = op->done;
    ida->op_preview = 1;

    viewer_workstart(ida);
    return 0;
}

int
viewer_cancel_preview(struct ida_viewer *ida)
{
    viewer_workstop(ida);
    viewer_workstart(ida);
    return 0;
}

int
viewer_loader_start(struct ida_viewer *ida, struct ida_loader *loader,
		    FILE *fp, char *filename, unsigned int page)
{
    struct ida_image_info info;
    void *data;

    /* init loader */
    ptr_busy();
    memset(&info,0,sizeof(info));
    data = loader->init(fp,filename,page,&info,0);
    ptr_idle();
    if (NULL == data) {
	fprintf(stderr,"loading %s [%s] FAILED\n",filename,loader->name);
	if (fp)
	    hex_display(filename);
	return -1;
    }

    /* ok, going to load new image */
    viewer_workstop(ida);
    viewer_rubber_off(ida);
    memset(&ida->current,0,sizeof(ida->current));
    if (ida->undo.data) {
	free(ida->undo.data);
	memset(&ida->undo,0,sizeof(ida->undo));
    }
    if (NULL != ida->img.data)
	free(ida->img.data);
    ida->file       = filename;
    ida->img.i      = info;
    ida->img.data   = malloc(ida->img.i.width * ida->img.i.height * 3);
    
    /* prepare background loading */
    ida->load_line = 0;
    ida->load_read = loader->read;
    ida->load_done = loader->done;
    ida->load_data = data;
    
    viewer_autozoom(ida);
    return info.npages;
}

int
viewer_loadimage(struct ida_viewer *ida, char *filename, unsigned int page)
{
    struct list_head  *item;
    struct ida_loader *loader;
    char blk[512];
    FILE *fp;

    if (NULL == (fp = fopen(filename, "r"))) {
	fprintf(stderr,"fopen %s: %s\n",filename,strerror(errno));
        return -1;
    }
    if (debug)
	fprintf(stderr,"load: %s\n",filename);
    memset(blk,0,sizeof(blk));
    fread(blk,1,sizeof(blk),fp);
    rewind(fp);

    /* pick loader */
    list_for_each(item,&loaders) {
        loader = list_entry(item, struct ida_loader, list);
#if 0
	if (NULL == loader->magic)
	    break;
#else
	if (NULL == loader->magic)
	    continue;
#endif
	if (0 == memcmp(blk+loader->moff,loader->magic,loader->mlen))
	    return viewer_loader_start(ida,loader,fp,filename,page);
    }
    fprintf(stderr,"%s: unknown format\n",filename);
    hex_display(filename);
    fclose(fp);
    return -1;
}

int
viewer_setimage(struct ida_viewer *ida, struct ida_image *img, char *name)
{
    /* ok, going to load new image */
    viewer_workstop(ida);
    viewer_rubber_off(ida);
    memset(&ida->current,0,sizeof(ida->current));
    if (ida->undo.data) {
	free(ida->undo.data);
	memset(&ida->undo,0,sizeof(ida->undo));
    }

    if (NULL != ida->img.data)
	free(ida->img.data);
    ida->file       = name;
    ida->img        = *img;

    viewer_autozoom(ida);
    return 0;
}

struct ida_viewer*
viewer_init(Widget widget)
{
    Colormap cmap = DefaultColormapOfScreen(XtScreen(widget));
    struct ida_viewer *ida;
    XColor white,red,dummy;
    unsigned int i;

    ida = malloc(sizeof(*ida));
    memset(ida,0,sizeof(*ida));
    ida->widget = widget;
    XtAddEventHandler(widget,ExposureMask,False,viewer_redraw,ida);
    XtAddEventHandler(widget,
		      ButtonPressMask   |
		      ButtonReleaseMask |
		      PointerMotionMask,
		      False,viewer_mouse,ida);

    ptrs[POINTER_NORMAL] = XCreateFontCursor(dpy,XC_left_ptr);
    ptrs[POINTER_BUSY]   = XCreateFontCursor(dpy,XC_watch);
    ptrs[POINTER_PICK]   = XCreateFontCursor(dpy,XC_tcross);
    ptrs[RUBBER_NEW]     = XCreateFontCursor(dpy,XC_left_ptr);
    ptrs[RUBBER_MOVE]    = XCreateFontCursor(dpy,XC_fleur);
    ptrs[RUBBER_X1]      = XCreateFontCursor(dpy,XC_sb_h_double_arrow);
    ptrs[RUBBER_X2]      = XCreateFontCursor(dpy,XC_sb_h_double_arrow);
    ptrs[RUBBER_Y1]      = XCreateFontCursor(dpy,XC_sb_v_double_arrow);
    ptrs[RUBBER_Y2]      = XCreateFontCursor(dpy,XC_sb_v_double_arrow);
    if (XAllocNamedColor(dpy,cmap,"white",&white,&dummy) &&
	XAllocNamedColor(dpy,cmap,"red",&red,&dummy))
	for (i = 0; i < sizeof(ptrs)/sizeof(Cursor); i++)
	    XRecolorCursor(dpy,ptrs[i],&red,&white);

    return ida;
}
