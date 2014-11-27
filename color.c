#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <math.h>

#include <X11/X.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/DrawingA.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Scale.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/SelectioB.h>

#include "RegEdit.h"
#include "ida.h"
#include "x11.h"
#include "readers.h"
#include "viewer.h"
#include "color.h"
#include "lut.h"

/* ---------------------------------------------------------------------- */

#define HIST_SIZE 60

struct ida_coledit;

struct ida_hist {
    /* x11 */
    GC                     gc;
    unsigned long          color;

    /* histogram */
    Widget                 hist;
    unsigned int           max;
    unsigned int           data[256];

    /* mapping */
    Widget                 map;
    struct op_map_parm_ch  parm;

    struct ida_coledit      *up;
};

struct ida_coledit {
    /* misc */
    Widget dlg,form,vals,toggle;
    Widget l,r,t,b,g;
    int lock,apply;
    
    /* histogram data */
    struct ida_hist red;
    struct ida_hist green;
    struct ida_hist blue;
    struct ida_hist *cur;
};

/* ---------------------------------------------------------------------- */

static void
color_calchist(struct ida_image *img, struct ida_coledit *me)
{
    unsigned char *pix;
    unsigned int i,x,y,max;

    pix = img->data;
    for (y = 0; y < img->i.height; y++) {
	for (x = 0; x < img->i.width; x++) {
	    me->red.data[pix[0]]++;
	    me->green.data[pix[1]]++;
	    me->blue.data[pix[2]]++;
	    pix += 3;
	}
    }
    max = 0;
    for (i = 0; i < 256; i++) {
	if (max < me->red.data[i])
	    max = me->red.data[i];
	if (max < me->green.data[i])
	    max = me->green.data[i];
	if (max < me->blue.data[i])
	    max = me->blue.data[i];
    }
    me->red.max   = max;
    me->green.max = max;
    me->blue.max  = max;
}

static void
color_update(struct ida_coledit *me, struct ida_hist *h, int text)
{
    struct op_map_parm param;
    char tmp[32];

    if (me->lock) {
	if (&me->red != h)
	    me->red.parm = h->parm;
	if (&me->green != h)
	    me->green.parm = h->parm;
	if (&me->blue != h)
	    me->blue.parm = h->parm;
	XClearArea(XtDisplay(me->red.hist), XtWindow(me->red.hist),
		   0,0,0,0, True);
	XClearArea(XtDisplay(me->red.map), XtWindow(me->red.map),
		   0,0,0,0, True);
	XClearArea(XtDisplay(me->green.hist), XtWindow(me->green.hist),
		   0,0,0,0, True);
	XClearArea(XtDisplay(me->green.map), XtWindow(me->green.map),
		   0,0,0,0, True);
	XClearArea(XtDisplay(me->blue.hist), XtWindow(me->blue.hist),
		   0,0,0,0, True);
	XClearArea(XtDisplay(me->blue.map), XtWindow(me->blue.map),
		   0,0,0,0, True);
    } else {
	XClearArea(XtDisplay(h->hist), XtWindow(h->hist),
		   0,0,0,0, True);
	XClearArea(XtDisplay(h->map), XtWindow(h->map),
		   0,0,0,0, True);
    }
    if ((me->lock || h == me->cur) && text >= 1) {
	/* mouse-click updateable values */
	sprintf(tmp,"%d",h->parm.left);
	XmTextSetString(me->l,tmp);
	sprintf(tmp,"%d",h->parm.right);
	XmTextSetString(me->r,tmp);
    }
    if ((me->lock || h == me->cur) && text >= 2) {
	/* others */
	sprintf(tmp,"%d",h->parm.bottom);
	XmTextSetString(me->b,tmp);
	sprintf(tmp,"%d",h->parm.top);
	XmTextSetString(me->t,tmp);
	sprintf(tmp,"%.2f",h->parm.gamma);
	XmTextSetString(me->g,tmp);
    }
    
    param.red   = me->red.parm;
    param.green = me->green.parm;
    param.blue  = me->blue.parm;
    viewer_start_preview(ida,&desc_map,&param);
}

static void
color_drawmap(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_hist *me = client_data;
    XmDrawingAreaCallbackStruct *cb = calldata;
    XGCValues values;
    int left,right,top,bottom,i,val,x1,y1,x2,y2;
    float p;

    if (cb->reason == XmCR_EXPOSE) {
	/* window needs redraw */
	XExposeEvent *e = (XExposeEvent*)cb->event;
	if (e->count)
	    return;
	values.foreground = x11_gray;
	XChangeGC(dpy,me->gc,GCForeground,&values);
	left   = me->parm.left   * HIST_SIZE / 255;
	right  = me->parm.right  * HIST_SIZE / 255;
	bottom = me->parm.bottom * HIST_SIZE / 255;
	top    = me->parm.top    * HIST_SIZE / 255;
	if (me->parm.left > 0)
	    XFillRectangle(dpy,XtWindow(me->map),me->gc,
			   0,0,left,HIST_SIZE);
	if (me->parm.right < 255)
	    XFillRectangle(dpy,XtWindow(me->map),me->gc,
			   right,0,HIST_SIZE-right,HIST_SIZE);
	values.foreground = me->color;
	XChangeGC(dpy,me->gc,GCForeground,&values);
	if (me->parm.left > 0)
	    XDrawLine(dpy,XtWindow(me->map),me->gc,
		      0,HIST_SIZE-bottom,left,HIST_SIZE-bottom);
	if (me->parm.right < 255)
	    XDrawLine(dpy,XtWindow(me->map),me->gc,
		      right,HIST_SIZE-top,HIST_SIZE,HIST_SIZE-top);
	p = 1/me->parm.gamma;
	x2 = y2 = 0;
	for (i = left; i <= right; i++) {
	    val  = pow((float)(i-left)/(right-left),p) * (top-bottom) + 0.5;
	    val += bottom;
	    if (val < 0)         val = 0;
	    if (val > HIST_SIZE) val = HIST_SIZE;
	    x1 = x2;
	    y1 = y2;
	    x2 = i;
	    y2 = HIST_SIZE-val;
	    if (i > left)
		XDrawLine(dpy,XtWindow(me->map),me->gc,
			  x1,y1,x2,y2);
	}
    }
}

static void
color_drawhist(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_hist *me = client_data;
    XmDrawingAreaCallbackStruct *cb = calldata;
    XGCValues values;
    int i,val;

    if (cb->reason == XmCR_EXPOSE) {
	/* window needs redraw */
	XExposeEvent *e = (XExposeEvent*)cb->event;
	if (e->count)
	    return;
	values.foreground = x11_gray;
	XChangeGC(dpy,me->gc,GCForeground,&values);
	if (me->parm.left > 0)
	    XFillRectangle(dpy,XtWindow(me->hist),me->gc,
			   0,0,me->parm.left,HIST_SIZE);
	if (me->parm.right < 255)
	    XFillRectangle(dpy,XtWindow(me->hist),me->gc,
			   me->parm.right,0,256-me->parm.right,HIST_SIZE);
	values.foreground = me->color;
	XChangeGC(dpy,me->gc,GCForeground,&values);
	for (i = 0; i < 256; i++) {
	    val = log(me->data[i])*HIST_SIZE/log(me->max);
	    XDrawLine(dpy,XtWindow(me->hist),me->gc,
		      i,HIST_SIZE,i,HIST_SIZE-val);
	}
    }
}

static void
color_mouse(Widget widget, XtPointer client_data,
	    XEvent *ev, Boolean *cont)
{
    struct ida_hist *me = client_data;
    int x;

    switch (ev->type) {
    case ButtonPress:
    case ButtonRelease:
    {
	XButtonEvent *e = (XButtonEvent*)ev;

	x = e->x;
	break;
    }
    case MotionNotify:
    {
	XMotionEvent *e = (XMotionEvent*)ev;

	x = e->x;
	break;
    default:
	return;
    }
    }
    if (x > (me->parm.right + me->parm.left)/2) {
	me->parm.right = x;
	if (me->parm.right > 255)
	    me->parm.right = 255;
	if (me->parm.right < me->parm.left)
	    me->parm.right = me->parm.left;
    } else {
	me->parm.left = x;
	if (me->parm.left < 0)
	    me->parm.left = 0;
	if (me->parm.left > me->parm.right)
	    me->parm.left = me->parm.right;
    }
    color_update(me->up,me,1);
}

static void
color_lock(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_coledit *me = client_data;
    XmToggleButtonCallbackStruct *cb = calldata;
    Widget label,button;

    label  = XmOptionLabelGadget(me->vals);
    button = XmOptionButtonGadget(me->vals);
    me->lock = cb->set;
    XtVaSetValues(label,XtNsensitive,!me->lock,NULL);
    XtVaSetValues(button,XtNsensitive,!me->lock,NULL);
}

static void
color_vals(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_hist *cur = client_data;
    struct ida_coledit *me = cur->up;

    me->cur = cur;
    color_update(me,cur,2);
}

static void
color_text(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_coledit *me = client_data;
    int left,right,bottom,top;
    float gamma;

    if (widget == me->l &&
	1 == sscanf(XmTextGetString(me->l),"%d",&left) &&
	left >= 0 && left <= me->cur->parm.right) {
	me->cur->parm.left = left;
    }
    if (widget == me->r &&
	1 == sscanf(XmTextGetString(me->r),"%d",&right) &&
	me->cur->parm.left <= right && right <= 255) {
	me->cur->parm.right = right;
    }
    if (widget == me->b &&
	1 == sscanf(XmTextGetString(me->b),"%d",&bottom) &&
	bottom <= me->cur->parm.top) {
	me->cur->parm.bottom = bottom;
    }
    if (widget == me->t &&
	1 == sscanf(XmTextGetString(me->t),"%d",&top) &&
	me->cur->parm.bottom <= top) {
	me->cur->parm.top = top;
    }
    if (widget == me->g &&
	1 == sscanf(XmTextGetString(me->g),"%f",&gamma)) {
	me->cur->parm.gamma = gamma;
    }
    color_update(me,me->cur,0);
}

static void
color_pick_ok(int x, int y, unsigned char *pix, XtPointer data)
{
    struct ida_coledit *me = data;
    int max;

    if (debug)
	fprintf(stderr,"color_pick_ok: +%d+%d %d/%d/%d\n",
		x,y, pix[0],pix[1],pix[2]);

    max = 0;
    if (max < pix[0])
	max = pix[0];
    if (max < pix[1])
	max = pix[1];
    if (max < pix[2])
	max = pix[2];

    XmToggleButtonSetState(me->toggle,False,True);
    me->red.parm.right   = (int)255 * pix[0] / max;
    color_update(me,&me->red,1);
    me->green.parm.right = (int)255 * pix[1] / max;
    color_update(me,&me->green,1);
    me->blue.parm.right  = (int)255 * pix[2] / max;
    color_update(me,&me->blue,1);

    if (debug)
	fprintf(stderr,"color_pick_ok: %d/%d/%d max=%d\n",
		me->red.parm.right,
		me->green.parm.right,
		me->blue.parm.right,
		max);
}

static void
color_pick(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_coledit *me = client_data;
    viewer_pick(ida,color_pick_ok,me);
}

static void
color_createhist(Widget parent, char *name, unsigned long color,
		 struct ida_hist *me)
{
    char tmp[32];

    sprintf(tmp,"h%s",name);
    me->hist = XtVaCreateManagedWidget(tmp,xmDrawingAreaWidgetClass,parent,
				       XtNwidth,256,
				       XtNheight,HIST_SIZE,
				       NULL);
    sprintf(tmp,"m%s",name);
    me->map = XtVaCreateManagedWidget(tmp,xmDrawingAreaWidgetClass,parent,
				      XtNwidth,HIST_SIZE,
				      XtNheight,HIST_SIZE,
				      NULL);
    XtAddEventHandler(me->hist,
		      ButtonPressMask   |
		      ButtonReleaseMask |
		      ButtonMotionMask,
		      False,color_mouse,me);
    XtAddCallback(me->hist,XmNexposeCallback,color_drawhist,me);
    XtAddCallback(me->map,XmNexposeCallback,color_drawmap,me);
    me->gc = XCreateGC(dpy,XtWindow(app_shell),0,NULL);
    me->color = color;
    me->parm = op_map_nothing;
}

static void
color_button_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_coledit *me = client_data;
    XmSelectionBoxCallbackStruct *cb = calldata;

    if (cb->reason == XmCR_OK)
	me->apply = 1;
    XtDestroyWidget(XtParent(me->form));
}

static void
color_destroy(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_coledit *me = client_data;
    struct op_map_parm param;

    if (me->apply) {
	param.red   = me->red.parm;
	param.green = me->green.parm;
	param.blue  = me->blue.parm;
	viewer_start_op(ida,&desc_map,&param);
    } else
	viewer_cancel_preview(ida);
    viewer_unpick(ida);
    free(me);
}

void
color_init(struct ida_image *img)
{
    Widget menu,push,rc;
    struct ida_coledit *me;
    Arg args[2];

    me = malloc(sizeof(*me));
    memset(me,0,sizeof(*me));
    color_calchist(img,me);

    /* dialog shell */
    me->dlg = XmCreatePromptDialog(app_shell,"color",NULL,0);
    XmdRegisterEditres(XtParent(me->dlg));
    XtUnmanageChild(XmSelectionBoxGetChild(me->dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(me->dlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(me->dlg,XmDIALOG_TEXT));
    me->form = XtVaCreateManagedWidget("form",xmFormWidgetClass,
				       me->dlg,NULL);
    XtAddCallback(XtParent(me->dlg),XmNdestroyCallback,color_destroy,me);
    XtAddCallback(me->dlg,XmNokCallback,color_button_cb,me);
    XtAddCallback(me->dlg,XmNcancelCallback,color_button_cb,me);

    /* histograms */
    XtVaCreateManagedWidget("hist",xmLabelWidgetClass,
			    me->form,NULL);
    color_createhist(me->form,"red",  x11_red,  &me->red);
    color_createhist(me->form,"green",x11_green,&me->green);
    color_createhist(me->form,"blue", x11_blue, &me->blue);
    me->red.up = me;
    me->green.up = me;
    me->blue.up = me;
    XtVaCreateManagedWidget("map",xmLabelWidgetClass,
			    me->form,NULL);

    /* control */
    me->toggle = XtVaCreateManagedWidget("lock",xmToggleButtonWidgetClass,
					 me->form,NULL);
    XtAddCallback(me->toggle,XmNvalueChangedCallback,color_lock,me);
    menu = XmCreatePulldownMenu(me->form,"valsM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,menu);
    me->vals = XmCreateOptionMenu(me->form,"vals",args,1);
    XtManageChild(me->vals);
    push = XtVaCreateManagedWidget("red",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,color_vals,&me->red);
    push = XtVaCreateManagedWidget("green",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,color_vals,&me->green);
    push = XtVaCreateManagedWidget("blue",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,color_vals,&me->blue);

    /* in range */
    rc = XtVaCreateManagedWidget("in",xmRowColumnWidgetClass,me->form,NULL);
    XtVaCreateManagedWidget("label",xmLabelWidgetClass,rc,NULL);
    me->l = XtVaCreateManagedWidget("left",xmTextWidgetClass,rc,NULL);
    XtAddCallback(me->l,XmNvalueChangedCallback,color_text,me);
    me->r = XtVaCreateManagedWidget("right",xmTextWidgetClass,rc,NULL);
    XtAddCallback(me->r,XmNvalueChangedCallback,color_text,me);

    /* out range */
    rc = XtVaCreateManagedWidget("out",xmRowColumnWidgetClass,me->form,NULL);
    XtVaCreateManagedWidget("label",xmLabelWidgetClass,rc,NULL);
    me->b = XtVaCreateManagedWidget("bottom",xmTextWidgetClass,rc,NULL);
    XtAddCallback(me->b,XmNvalueChangedCallback,color_text,me);
    me->t = XtVaCreateManagedWidget("top",xmTextWidgetClass,rc,NULL);
    XtAddCallback(me->t,XmNvalueChangedCallback,color_text,me);

    /* gamma */
    rc = XtVaCreateManagedWidget("gamma",xmRowColumnWidgetClass,me->form,NULL);
    XtVaCreateManagedWidget("label",xmLabelWidgetClass,rc,NULL);
    me->g = XtVaCreateManagedWidget("gamma",xmTextWidgetClass,rc,NULL);
    XtAddCallback(me->g,XmNvalueChangedCallback,color_text,me);

    /* testing stuff */
    rc = XtVaCreateManagedWidget("pick",xmRowColumnWidgetClass,me->form,NULL);
    push = XtVaCreateManagedWidget("white",xmPushButtonWidgetClass,rc,NULL);
    XtAddCallback(push,XmNactivateCallback,color_pick,me);
    
    XtManageChild(me->dlg);

    me->cur = &me->red;
    color_update(me,me->cur,2);
    XmToggleButtonSetState(me->toggle,True,True);
}
