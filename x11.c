/*
 * some X11 ximage / pixmaps rotines
 *
 *   (c) 1996 Gerd Hoffmann <kraxel@goldbach.in-berlin.de>
 *
 * basic usage:
 *  1) call x11_color_init()
 *     this does all the visual checking/colormap handling stuff and returns
 *     TRUECOLOR or PSEUDOCOLOR
 *  2) create/load the image
 *  3) call x11_create_pixmaps()
 *     For TRUECOLOR:   It expects the data in one long (4 byte) per pixel.
 *                      To create the long, run the rgb-values throuth the
 *                      x11_lut_[red|green|blue] tables and or the results
 *     For PSEUDOCOLOR: The data is expected to be one byte per pixel,
 *                      containing the results from dither_line (see dither.c)
 *                      Not required to call init_dither, this is done by
 *                      x11_color_init
 *     returns a pixmap.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/XShm.h>

#include "x11.h"

extern Display *dpy;

#define PERROR(str)      fprintf(stderr,"%s:%d: %s: %s\n",__FILE__,__LINE__,str,strerror(errno))

/* ------------------------------------------------------------------------ */

int             display_type = 0;
int             display_depth = 0;
XVisualInfo     *info;

unsigned long   x11_red;
unsigned long   x11_green;
unsigned long   x11_blue;
unsigned long   x11_gray;

int             have_shmem = 0;

/* TrueColor: r,g,b => X11-color */
unsigned long   x11_lut_red[256];
unsigned long   x11_lut_green[256];
unsigned long   x11_lut_blue[256];

static unsigned long
x11_alloc_color(Display * dpy, Colormap cmap, int red, int green, int blue)
{
    XColor          akt_color;

    akt_color.red = red;
    akt_color.green = green;
    akt_color.blue = blue;

    XAllocColor(dpy, cmap, &akt_color);
    return akt_color.pixel;
}

static void
x11_create_lut(unsigned long red_mask,
	       unsigned long green_mask,
	       unsigned long blue_mask)
{
    int             rgb_red_bits = 0;
    int             rgb_red_shift = 0;
    int             rgb_green_bits = 0;
    int             rgb_green_shift = 0;
    int             rgb_blue_bits = 0;
    int             rgb_blue_shift = 0;
    int             i;
    unsigned long   mask;

    for (i = 0; i < 24; i++) {
	mask = (1 << i);
	if (red_mask & mask)
	    rgb_red_bits++;
	else if (!rgb_red_bits)
	    rgb_red_shift++;
	if (green_mask & mask)
	    rgb_green_bits++;
	else if (!rgb_green_bits)
	    rgb_green_shift++;
	if (blue_mask & mask)
	    rgb_blue_bits++;
	else if (!rgb_blue_bits)
	    rgb_blue_shift++;
    }
#if 0
    printf("color: bits shift\n");
    printf("red  : %04i %05i\n", rgb_red_bits, rgb_red_shift);
    printf("green: %04i %05i\n", rgb_green_bits, rgb_green_shift);
    printf("blue : %04i %05i\n", rgb_blue_bits, rgb_blue_shift);
#endif

    for (i = 0; i < 256; i++) {
	x11_lut_red[i] = (i >> (8 - rgb_red_bits)) << rgb_red_shift;
	x11_lut_green[i] = (i >> (8 - rgb_green_bits)) << rgb_green_shift;
	x11_lut_blue[i] = (i >> (8 - rgb_blue_bits)) << rgb_blue_shift;
    }
}

int
x11_color_init(Widget shell)
{
    Screen          *scr;
    Colormap        cmap;
    XVisualInfo     template;
    unsigned int    found;

    scr = XtScreen(shell);
    cmap = DefaultColormapOfScreen(scr);

    /* Ask for visual type */
    template.screen = XDefaultScreen(dpy);
    template.visualid =
	XVisualIDFromVisual(DefaultVisualOfScreen(scr));
    info = XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask, &template,
			  &found);
    if (XShmQueryExtension(dpy)) {
	have_shmem = 1;
    }

    /* display_depth = (info->depth+7)/8; */
    if (info->class == TrueColor) {
	/* TrueColor */
	display_depth = 4;
	display_type = TRUECOLOR;
	x11_create_lut(info->red_mask, info->green_mask, info->blue_mask);
    } else {
	fprintf(stderr, "sorry, can't handle visual\n");
	exit(1);
    }

    /* some common colors */
    x11_red = x11_alloc_color(dpy, cmap, 65535, 0, 0);
    x11_green = x11_alloc_color(dpy, cmap, 0, 65535, 0);
    x11_blue = x11_alloc_color(dpy, cmap, 0, 0, 65535);
    x11_gray = x11_alloc_color(dpy, cmap, 0xc400, 0xc400, 0xc400);

    return display_type;
}

/* ------------------------------------------------------------------------ */

static int      mitshm_bang = 0;

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    mitshm_bang = 1;
    return 0;
}

XImage*
x11_create_ximage(Widget shell, int width, int height, void **shm)
{
    XImage         *ximage = NULL;
    unsigned char  *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    XtPointer       old_handler;
    Screen         *scr = XtScreen(shell);

    if (have_shmem) {
	old_handler = XSetErrorHandler(x11_error_dev_null);
	(*shm) = shminfo = malloc(sizeof(XShmSegmentInfo));
	memset(shminfo, 0, sizeof(XShmSegmentInfo));
	ximage = XShmCreateImage(dpy,
				 DefaultVisualOfScreen(scr),
				 DefaultDepthOfScreen(scr),
				 ZPixmap, NULL,
				 shminfo, width, height);
	if (ximage) {
	    shminfo->shmid = shmget(IPC_PRIVATE,
				    ximage->bytes_per_line * ximage->height,
				    IPC_CREAT | 0777);
	    if (-1 == shminfo->shmid) {
		fprintf(stderr,"shmget(%dMB): %s\n",
			ximage->bytes_per_line * ximage->height / 1024 / 1024,
			strerror(errno));
		goto oom;
	    }
	    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
	    if ((void *) -1 == shminfo->shmaddr) {
		perror("shmat");
		goto oom;
	    }
	    ximage->data = shminfo->shmaddr;
	    shminfo->readOnly = False;

	    XShmAttach(dpy, shminfo);
	    XSync(dpy, False);
	    shmctl(shminfo->shmid, IPC_RMID, 0);
	    if (mitshm_bang) {
		have_shmem = 0;
		shmdt(shminfo->shmaddr);
		free(shminfo);
		shminfo = *shm = NULL;
		XDestroyImage(ximage);
		ximage = NULL;
	    }
	} else {
	    have_shmem = 0;
	    free(shminfo);
	    shminfo = *shm = NULL;
	}
	XSetErrorHandler(old_handler);
    }

    if (ximage == NULL) {
	(*shm) = NULL;
	if (NULL == (ximage_data = malloc(width * height * display_depth))) {
	    fprintf(stderr,"Oops: out of memory\n");
	    goto oom;
	}
	ximage = XCreateImage(dpy,
			      DefaultVisualOfScreen(scr),
			      DefaultDepthOfScreen(scr),
			      ZPixmap, 0, ximage_data,
			      width, height,
			      8, 0);
    }
    memset(ximage->data, 0, ximage->bytes_per_line * ximage->height);

    return ximage;

  oom:
    if (shminfo) {
	if (shminfo->shmid && shminfo->shmid != -1)
	    shmctl(shminfo->shmid, IPC_RMID, 0);
	free(shminfo);
    }
    if (ximage)
	XDestroyImage(ximage);
    return NULL;
}

void
x11_destroy_ximage(Widget shell, XImage * ximage, void *shm)
{
    XShmSegmentInfo *shminfo = shm;

    if (shminfo) {
	XShmDetach(dpy, shminfo);
	XDestroyImage(ximage);
	shmdt(shminfo->shmaddr);
	free(shminfo);
    } else
	XDestroyImage(ximage);
}

Pixmap
x11_create_pixmap(Widget shell, unsigned char *byte_data,
		  int width, int height, int gray)
{
    Pixmap          pixmap;
    XImage         *ximage;
    XGCValues       values;
    GC              gc;
    unsigned long  *long_data = (unsigned long *) byte_data;
    int             x, y;
    void           *shm;

    Screen         *scr = XtScreen(shell);

    pixmap = XCreatePixmap(dpy,
			   RootWindowOfScreen(scr),
			   width, height,
			   DefaultDepthOfScreen(scr));
    gc = XCreateGC(dpy, pixmap, 0, &values);

    if (NULL == (ximage = x11_create_ximage(shell, width, height, &shm))) {
	XFreePixmap(dpy, pixmap);
	XFreeGC(dpy, gc);
	return 0;
    }
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            XPutPixel(ximage, x, y, *(long_data++));

    XPUTIMAGE(dpy, pixmap, gc, ximage, 0, 0, 0, 0, width, height);

    x11_destroy_ximage(shell, ximage, shm);
    XFreeGC(dpy, gc);
    return pixmap;
}

void
x11_data_to_ximage(unsigned char *data, unsigned char *ximage,
		   int x, int y, int sy, int gray)
{
    unsigned long  *d;
    int             i, n;

    d = (unsigned long *) ximage;
    n = 3 * x * y;
    for (i = 0; i < n; i += 3)
        *(d++) = x11_lut_red[data[i]] |
            x11_lut_green[data[i + 1]] |
            x11_lut_blue[data[i + 2]];
}
