#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/xpm.h>
#include <Xm/Xm.h>

#include "icons.h"
#include "misc.h"

#include "xpm/cw.xpm"
#include "xpm/ccw.xpm"
#include "xpm/fliph.xpm"
#include "xpm/flipv.xpm"
#include "xpm/prev.xpm"
#include "xpm/next.xpm"
#include "xpm/zoomin.xpm"
#include "xpm/zoomout.xpm"
#include "xpm/exit.xpm"

#include "xpm/question.xpm"
#include "xpm/dir.xpm"
#include "xpm/file.xpm"
#include "xpm/unknown.xpm"

static void patch_bg(XImage *image, XImage *shape,
		     int width, int height, Pixel bg)
{
    unsigned int x,y;

    for (y = 0; y < height; y++)
	for (x = 0; x < width; x++)
	    if (!XGetPixel(shape, x, y))
		XPutPixel(image, x, y, bg);
}

static void
add_pixmap(Display *dpy, Pixel bg, char *name, char **data)
{
    XImage *image,*shape;
    XpmAttributes attr;
    char sname[32];

    memset(&attr,0,sizeof(attr));
    XpmCreateImageFromData(dpy,data,&image,&shape,&attr);

    if (shape) {
	patch_bg(image,shape,attr.width,attr.height,bg);
	snprintf(sname,sizeof(sname),"%s_shape",name);
	XmInstallImage(shape,sname);
    }
    XmInstallImage(image,name);
}

Pixmap x11_icon_fit(Display *dpy, Pixmap icon, unsigned long bg,
		    int width, int height)
{
    Pixmap pix;
    GC gc;
    XGCValues values;
    Window root;
    unsigned int w,h,b,d;
    int x,y;
    
    XGetGeometry(dpy,icon,&root,&x,&y,&w,&h,&b,&d);
    pix = XCreatePixmap(dpy,icon,width,height,d);

    /* fill background */
    values.foreground = bg;
    values.background = bg;
    values.fill_style = FillSolid;
    gc = XCreateGC(dpy, icon, GCForeground | GCBackground | GCFillStyle,
		   &values);
    XFillRectangle(dpy,pix,gc,0,0,width,height);

    /* blit */
    if (w <= width && h <= height) {
	/* just center ... */
	x = (width  - w) / 2;
	y = (height - h) / 2;
	XCopyArea(dpy,icon,pix,gc,0,0,w,h,x,y);
    } else {
	/* must scale down */
#if 0
	float xs,ys,scale;
	
	xs = (float)width  / w;
	ys = (float)height / h;
	scale = (xs < ys) ? xs : ys;
	w = w * scale;
	h = h * scale;
	if (0 == w) w = 1;
	if (0 == h) h = 1;

	x = (width  - w) / 2;
	y = (height - h) / 2;
	XCopyArea(dpy,icon,pix,gc,0,0,w,h,x,y);
#endif
	x = (width  - w) / 2;
	y = (height - h) / 2;
	if (x < 0)
	    x = 0;
	if (y < 0)
	    y = 0;
	XCopyArea(dpy,icon,pix,gc,0,0,w,h,0,0);
    }
    
    XFreeGC(dpy, gc);
    return pix;
}

void
x11_icons_init(Display *dpy, unsigned long bg)
{
    add_pixmap(dpy, bg, "rotcw",    cw_xpm);
    add_pixmap(dpy, bg, "rotccw",   ccw_xpm);
    add_pixmap(dpy, bg, "fliph",    fliph_xpm);
    add_pixmap(dpy, bg, "flipv",    flipv_xpm);
    add_pixmap(dpy, bg, "prev",     prev_xpm);
    add_pixmap(dpy, bg, "next",     next_xpm);
    add_pixmap(dpy, bg, "zoomin",   zoomin_xpm);
    add_pixmap(dpy, bg, "zoomout",  zoomout_xpm);
    add_pixmap(dpy, bg, "exit",     exit_xpm);

    add_pixmap(dpy, bg, "question", question_xpm);
    add_pixmap(dpy, bg, "dir",      dir_xpm);
    add_pixmap(dpy, bg, "file",     file_xpm);
    add_pixmap(dpy, bg, "unknown",  unknown_xpm);
}
