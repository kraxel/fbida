#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <Xm/SelectioB.h>
#include <Xm/DrawingA.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/Scale.h>
#include <Xm/Label.h>
#include "RegEdit.h"

#include "ida.h"
#include "readers.h"
#include "writers.h"
#include "viewer.h"

struct PAPER {
    char  *name;
    int   width,height;
};

static struct PAPER formats[] = {
    {
	name:   "A4",
	width:  595,
	height: 842,
    },{
	name:   "Letter",
	width:  612,
	height: 792,
    },{
	/* EOF */
    }
};

static const char *header =
"%%!PS-Adobe-2.0 EPSF-2.0\n"
"%%%%Creator: ida " VERSION " (https://www.kraxel.org/blog/linux/fbida/)\n"
"%%%%Pages: 1\n"
"%%%%BoundingBox: %d %d %d %d\n"
"%%%%DocumentFonts: \n"
"%%%%EndComments\n"
"%%%%EndProlog\n"
"\n"
"%%%%Page: 1 1"
"\n"
"/origstate save def\n"
"20 dict begin\n";

static const char *footer =
"\n"
"showpage\n"
"end\n"
"origstate restore\n"
"%%%%Trailer\n";

/* taken from xwd2ps, ftp://ftp.x.org/R5contrib/xwd2ps.tar.Z */
static const char *ColorImage =
"% define 'colorimage' if it isn't defined\n"
"%   ('colortogray' and 'mergeprocs' come from xwd2ps\n"
"%     via xgrab)\n"
"/colorimage where   % do we know about 'colorimage'?\n"
"  { pop }           % yes: pop off the 'dict' returned\n"
"  {                 % no:  define one\n"
"    /colortogray {  % define an RGB->I function\n"
"      /rgbdata exch store    % call input 'rgbdata'\n"
"      rgbdata length 3 idiv\n"
"      /npixls exch store\n"
"      /rgbindx 0 store\n"
"      0 1 npixls 1 sub {\n"
"        grays exch\n"
"        rgbdata rgbindx       get 20 mul    % Red\n"
"        rgbdata rgbindx 1 add get 32 mul    % Green\n"
"        rgbdata rgbindx 2 add get 12 mul    % Blue\n"
"        add add 64 idiv      % I = .5G + .31R + .18B\n"
"        put\n"
"        /rgbindx rgbindx 3 add store\n"
"      } for\n"
"      grays 0 npixls getinterval\n"
"    } bind def\n"
"\n"
"    % Utility procedure for colorimage operator.\n"
"    % This procedure takes two procedures off the\n"
"    % stack and merges them into a single procedure.\n"
"\n"
"    /mergeprocs { % def\n"
"      dup length\n"
"      3 -1 roll\n"
"      dup\n"
"      length\n"
"      dup\n"
"      5 1 roll\n"
"      3 -1 roll\n"
"      add\n"
"      array cvx\n"
"      dup\n"
"      3 -1 roll\n"
"      0 exch\n"
"      putinterval\n"
"      dup\n"
"      4 2 roll\n"
"      putinterval\n"
"    } bind def\n"
"\n"
"    /colorimage { % def\n"
"      pop pop     % remove 'false 3' operands\n"
"      {colortogray} mergeprocs\n"
"      image\n"
"    } bind def\n"
"  } ifelse          % end of 'false' case\n"
"\n";


/* ---------------------------------------------------------------------- */
/* save                                                                   */

#define PORTRAIT    0
#define LANDSCAPE   1

#define DRAW_SIZE   200
#define DRAW_SCALE  6
#define DSCALED(x)  (((x)+DRAW_SCALE/2)/DRAW_SCALE)

static struct ps_options {
    Widget shell,draw,scale,geo;
    int xscale,yscale;
    GC gc;
    int lastx,lasty;

    int format;
    int ori;
    int scaling;

    int iwidth,iheight,ires;
    int pwidth,pheight;
    int mwidth,mheight;
    int width,height,xcenter,ycenter;
} ps;

static void
ps_draw(Widget widget, XtPointer client_data, XtPointer calldata)
{
    XmDrawingAreaCallbackStruct *cb = calldata;
    XExposeEvent *e;
    XmString str;
    char buf[128];
    int x,y,w,h;
    
    if (!(cb->reason == XmCR_EXPOSE))
	return;
    e = (XExposeEvent*)cb->event;
    if (e->count)
	return;

    if (!ps.gc)
	ps.gc = XCreateGC(dpy,XtWindow(app_shell),0,NULL);

    w = DSCALED(ps.pwidth);
    h = DSCALED(ps.pheight);
    x = (DRAW_SIZE-w) / 2;
    y = (DRAW_SIZE-h) / 2;
    XDrawRectangle(dpy,XtWindow(widget),ps.gc, x,y,w,h);

    w = DSCALED(ps.width);
    h = DSCALED(ps.height);
    x += DSCALED(ps.xcenter - ps.width/2);
    y += DSCALED(ps.ycenter - ps.height/2);
    XFillRectangle(dpy,XtWindow(widget),ps.gc, x,y,w,h);

    sprintf(buf,"%d dpi",ps.iwidth * 72 / ps.width);
    str = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(ps.geo,XmNlabelString,str,NULL);
    XmStringFree(str);
}

static void
ps_defaults(void)
{
    /* max size, keep aspect ratio */
    if (ps.ori == PORTRAIT) {
	ps.pwidth  = formats[ps.format].width;
	ps.pheight = formats[ps.format].height;
    } else {
	ps.pheight = formats[ps.format].width;
	ps.pwidth  = formats[ps.format].height;
    }

    if (ps.iwidth  * ps.pheight > ps.iheight * ps.pwidth) {
	ps.mwidth  = ps.pwidth;
	ps.mheight = ps.iheight * ps.mwidth / ps.iwidth;
    } else {
	ps.mheight = ps.pheight;
	ps.mwidth  = ps.iwidth * ps.mheight / ps.iheight;
    }
    ps.scaling = 0;
    if (ps.ires) {
	/* Use image resolution to calculate default scaling factor.
	 * The image will be printed in original size if it fits into
	 * one page */
	ps.scaling = ps.iwidth * 72 * 1000 / ps.mwidth / ps.ires;
    }
    if (ps.scaling > 1000 || ps.scaling < 1) {
	/* default: maxpect with some border */
	ps.scaling = 1000;
	while (ps.mwidth  * ps.scaling / 1000 + 50 > ps.mwidth ||
	       ps.mheight * ps.scaling / 1000 + 50 > ps.mheight)
	    ps.scaling--;
    }
    XmScaleSetValue(ps.scale,ps.scaling);
    ps.width  = ps.mwidth * ps.scaling  / 1000;
    ps.height = ps.mheight * ps.scaling / 1000;
    ps.xcenter = ps.pwidth/2;
    ps.ycenter = ps.pheight/2;

    if (XtWindow(ps.draw))
	XClearArea(XtDisplay(ps.draw), XtWindow(ps.draw),
		   0,0,0,0, True);
}

static void
ps_ranges(void)
{
    if (ps.width == 0)
	ps.width = 1;
    if (ps.height == 0)
	ps.height = 1;
    if (ps.xcenter - ps.width/2 < 0)
	ps.xcenter = ps.width/2;
    if (ps.xcenter + ps.width/2 > ps.pwidth)
	ps.xcenter = ps.pwidth - ps.width/2;
    if (ps.ycenter - ps.height/2 < 0)
	ps.ycenter = ps.height/2;
    if (ps.ycenter + ps.height/2 > ps.pheight)
	ps.ycenter = ps.pheight - ps.height/2;
}

static void
ps_mouse(Widget widget, XtPointer client_data,
	 XEvent *ev, Boolean *cont)
{
    switch (ev->type) {
    case ButtonPress:
    {
	XButtonEvent *e = (XButtonEvent*)ev;

	ps.lastx = e->x;
	ps.lasty = e->y;
	break;
    }
    case MotionNotify:
    {
	XMotionEvent *e = (XMotionEvent*)ev;

	if (e->state & Button1Mask) {
	    ps.xcenter += (e->x - ps.lastx) * DRAW_SCALE;
	    ps.ycenter += (e->y - ps.lasty) * DRAW_SCALE;
	    ps.lastx = e->x;
	    ps.lasty = e->y;
	}
	break;
    default:
	return;
    }
    }
    ps_ranges();
    if (XtWindow(ps.draw))
	XClearArea(XtDisplay(ps.draw), XtWindow(ps.draw),
		   0,0,0,0, True);
}

static void
ps_paper_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    ps.format = (intptr_t)clientdata;
    ps_defaults();
}

static void
ps_ori_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    ps.ori = (intptr_t)clientdata;
    ps_defaults();
}

static void
ps_scaling_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmScaleCallbackStruct *cd = call_data;

    ps.scaling = cd->value;
    ps.width  = ps.mwidth  * ps.scaling / 1000;
    ps.height = ps.mheight * ps.scaling / 1000;
    ps_ranges();
    if (XtWindow(ps.draw))
	XClearArea(XtDisplay(ps.draw), XtWindow(ps.draw),
		   0,0,0,0, True);
}

static void
ps_button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = call_data;

    if (XmCR_OK == cb->reason) {
	do_save_print();
    }
    XtUnmanageChild(ps.shell);
}

static int
ps_conf(Widget parent, struct ida_image *img)
{
    Widget rc,menu,push,opt;
    Arg args[2];
    intptr_t i;
    
    if (!ps.shell) {
	/* build dialog */
	ps.shell = XmCreatePromptDialog(parent,"ps",NULL,0);
	XmdRegisterEditres(XtParent(ps.shell));
	XtUnmanageChild(XmSelectionBoxGetChild(ps.shell,XmDIALOG_HELP_BUTTON));
	XtUnmanageChild(XmSelectionBoxGetChild(ps.shell,XmDIALOG_SELECTION_LABEL));
	XtUnmanageChild(XmSelectionBoxGetChild(ps.shell,XmDIALOG_TEXT));
	XtAddCallback(ps.shell,XmNokCallback,ps_button_cb,NULL);
	XtAddCallback(ps.shell,XmNcancelCallback,ps_button_cb,NULL);

	rc = XtVaCreateManagedWidget("rc1",xmRowColumnWidgetClass,
				     ps.shell,NULL);
	ps.draw = XtVaCreateManagedWidget("draw",xmDrawingAreaWidgetClass,rc,
					  XtNwidth,DRAW_SIZE,
					  XtNheight,DRAW_SIZE,
					  NULL);
	XtAddCallback(ps.draw,XmNexposeCallback,ps_draw,NULL);
	XtAddEventHandler(ps.draw,
			  ButtonPressMask   |
			  ButtonReleaseMask |
			  ButtonMotionMask,
			  False,ps_mouse,NULL);
	rc = XtVaCreateManagedWidget("rc2",xmRowColumnWidgetClass,
				     rc,NULL);

	/* paper */
	menu = XmCreatePulldownMenu(rc,"paperM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,menu);
	opt = XmCreateOptionMenu(rc,"paper",args,1);
	XtManageChild(opt);
	for (i = 0; formats[i].name != NULL; i++) {
	    push = XtVaCreateManagedWidget(formats[i].name,xmPushButtonWidgetClass,menu,NULL);
	    XtAddCallback(push,XmNactivateCallback,ps_paper_cb,(XtPointer)i);
	}

	/* orientation */
	menu = XmCreatePulldownMenu(rc,"oriM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,menu);
	opt = XmCreateOptionMenu(rc,"ori",args,1);
	XtManageChild(opt);
	push = XtVaCreateManagedWidget("portrait",xmPushButtonWidgetClass,
				       menu,NULL);
	XtAddCallback(push,XmNactivateCallback,ps_ori_cb,(XtPointer)PORTRAIT);
	push = XtVaCreateManagedWidget("landscape",xmPushButtonWidgetClass,
				       menu,NULL);
	XtAddCallback(push,XmNactivateCallback,ps_ori_cb,(XtPointer)LANDSCAPE);

	ps.scale = XtVaCreateManagedWidget("scale",xmScaleWidgetClass,rc,NULL);
	XtAddCallback(ps.scale,XmNdragCallback,ps_scaling_cb,NULL);
	XtAddCallback(ps.scale,XmNvalueChangedCallback,ps_scaling_cb,NULL);
	
	/* output */
	ps.geo = XtVaCreateManagedWidget("geo",xmLabelWidgetClass,rc,NULL);
    }

    ps.iwidth  = img->i.width;
    ps.iheight = img->i.height;
    ps.ires    = 0;
    if (ida->img.i.dpi)
	ps.ires = ida->img.i.dpi;
    ps_defaults();
    
    XtManageChild(ps.shell);
    return 0;
}

static int
ps_write(FILE *fp, struct ida_image *img)
{
    unsigned int width,height,xoff,yoff;
    unsigned int iwidth,iheight;
    unsigned int x,y;
    unsigned char *p;

    if (ps.ori == PORTRAIT) {
	iwidth  = img->i.width;
	iheight = img->i.height;
	width   = ps.width;
	height  = ps.height;
	xoff    = ps.xcenter - ps.width/2;
	yoff    = (ps.pheight - ps.ycenter) - ps.height/2;
    } else{
	iwidth  = img->i.height;
	iheight = img->i.width;
	width   = ps.height;
	height  = ps.width;
	xoff    = ps.ycenter - ps.height/2;
	yoff    = ps.xcenter - ps.width/2;
    }

    /* PS header */
    fprintf(fp,header, /* includes bbox */
	    xoff,yoff,xoff+width,yoff+height);
    fprintf(fp,"\n"
	    "/pix %u string def\n"
	    "/grays %u string def\n"
	    "/npixls 0 def\n"
	    "/rgbindx 0 def\n"
	    "\n",
	    img->i.width*3,img->i.width);
    fwrite(ColorImage,strlen(ColorImage),1,fp);

    fprintf(fp,"%u %u translate\n",xoff,yoff);
    fprintf(fp,"%u %u scale\n",width,height);

    fprintf(fp,"\n"
	    "%u %u 8\n"
	    "[%u 0 0 -%u 0 %u]\n"
	    "{currentfile pix readhexstring pop}\n"
	    "false 3 colorimage\n",
	    iwidth,iheight,iwidth,iheight,iheight);

    /* image data + ps footer */
    if (ps.ori == PORTRAIT) {
	p = img->data;
	for (y = 0; y < img->i.height; y++) {
	    for (x = 0; x < img->i.width; x++) {
		if (0 == (x % 10))
		    fprintf(fp,"\n");
		fprintf(fp,"%02x%02x%02x ",p[0],p[1],p[2]);
		p += 3;
	    }
	    fprintf(fp,"\n");
	}
    } else {
	for (x = img->i.width-1; x != -1; x--) {
	    p = img->data + 3*x;
	    for (y = 0; y < img->i.height; y++) {
		if (0 == (y % 10))
		    fprintf(fp,"\n");
		fprintf(fp,"%02x%02x%02x ",p[0],p[1],p[2]);
		p += img->i.width*3;
	    }
	    fprintf(fp,"\n");
	}
    }
    fprintf(fp,footer);
    return 0;
}

struct ida_writer ps_writer = {
    label:  "PostScript",
    ext:    { "ps", "eps", NULL},
    write:  ps_write,
    conf:   ps_conf,
};

static void __init init_wr(void)
{
    write_register(&ps_writer);
}

