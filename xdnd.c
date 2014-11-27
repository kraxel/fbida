/*
 * basic Xdnd support for Motif 2.x
 * 
 * Receive drops only.  Works fine in parallel with Motif DnD.
 *
 * Initiate drags is probably hard do do without breaking Motif DnD or
 * heavily mucking with the Motif internals.  Highlighting seems to be
 * non-trivial too.
 *
 * Usage:
 *    (1) register XdndAction as "Xdnd"
 *    (2) register Widgets using XdndDropSink (acts like XmeDropSink
 *        for Motif DnD)
 *    (3) the transfer callback functions have to call
 *        XdndDropFinished() when they are done (i.e. after calling
 *        XmTransferDone)
 *
 * Data transfer is done using the usual Motif 2.x way, using UTM.
 * Read: XmNdestinationCallback will be called, with
 * XmDestinationCallbackStruct->selection set to XdndSelection.
 *
 * It is up to the application to handle the Xdnd MIME targets
 * correctly, i.e. accept both "TEXT" and "text/plain" targets for
 * example.  Otherwise Xdnd support shouldn't need much work if Motif
 * DnD support is present already.
 *
 * Known problems:
 *   - Not working with KDE 2.x as they don't provide a TARGETS
 *     target (which IMO is illegal, see ICCCM specs).
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <Xm/Xm.h>
#include <Xm/Transfer.h>
#include <Xm/TransferP.h>

#include "xdnd.h"

/* ---------------------------------------------------------------------- */

int xdnd_debug = 0;

static Atom XdndAware;
static Atom XdndTypeList;
static Atom XdndSelection;

static Atom XdndEnter;
static Atom XdndPosition;
static Atom XdndStatus;
static Atom XdndLeave;
static Atom XdndDrop;
static Atom XdndFinished;

static Atom XdndActionCopy;
static Atom XdndActionMove;
static Atom XdndActionLink;
static Atom XdndActionAsk;
static Atom XdndActionPrivate;

/* ---------------------------------------------------------------------- */

static void XdndInit(Display *dpy)
{
    if (XdndAware)
	return;

    XdndAware         = XInternAtom(dpy, "XdndAware",         False);
    XdndTypeList      = XInternAtom(dpy, "XdndTypeList",      False);
    XdndSelection     = XInternAtom(dpy, "XdndSelection",     False);

    /* client messages */
    XdndEnter         = XInternAtom(dpy, "XdndEnter",         False);
    XdndPosition      = XInternAtom(dpy, "XdndPosition",      False);
    XdndStatus        = XInternAtom(dpy, "XdndStatus",        False);
    XdndLeave         = XInternAtom(dpy, "XdndLeave",         False);
    XdndDrop          = XInternAtom(dpy, "XdndDrop",          False);
    XdndFinished      = XInternAtom(dpy, "XdndFinished",      False);

    /* actions */
    XdndActionCopy    = XInternAtom(dpy, "XdndActionCopy",    False);
    XdndActionMove    = XInternAtom(dpy, "XdndActionMove",    False);
    XdndActionLink    = XInternAtom(dpy, "XdndActionLink",    False);
    XdndActionAsk     = XInternAtom(dpy, "XdndActionAsk",     False);
    XdndActionPrivate = XInternAtom(dpy, "XdndActionPrivate", False);
}

static void
init_window(Widget widget)
{
    int version = 4;

    /* window */
    XChangeProperty(XtDisplay(widget),XtWindow(widget),
		    XdndAware, XA_ATOM, 32, PropModeReplace,
		    (XtPointer)&version, 1);

    /* shell */
    while (!XtIsShell(widget))
	widget = XtParent(widget);

    XChangeProperty(XtDisplay(widget),XtWindow(widget),
		    XdndAware, XA_ATOM, 32, PropModeReplace,
		    (XtPointer)&version, 1);
    XtOverrideTranslations(widget, XtParseTranslationTable
                           ("<Message>XdndEnter:    Xdnd()\n"
			    "<Message>XdndPosition: Xdnd()\n"
			    "<Message>XdndLeave:    Xdnd()\n"
			    "<Message>XdndDrop:     Xdnd()"));
}

static Widget
find_window(Widget widget, int wx, int wy, int rx, int ry)
{
    WidgetList children;
    Cardinal nchildren;
    Dimension x,y,w,h;
    Widget found = NULL;
    int i;

    nchildren = 0;
    XtVaGetValues(widget,XtNchildren,&children,
                  XtNnumChildren,&nchildren,NULL);
    if (xdnd_debug)
        fprintf(stderr,"findwindow %s\n",XtName(widget));
    for (i = nchildren-1; i >= 0; i--) {
	XtVaGetValues(children[i],XtNx,&x,XtNy,&y,
		      XtNwidth,&w,XtNheight,&h,NULL);
	if (!XtIsManaged(children[i]))
	    continue;
	if (XtIsSubclass(children[i],xmGadgetClass))
	    continue;
	if (rx < wx+x || rx > wx+x+w)
	    continue;
	if (ry < wy+y || ry > wy+y+h)
	    continue;
	found = children[i];
	break;
    }
    if (found) {
        if (xdnd_debug)
            fprintf(stderr,"  more: %s\n",XtName(found));
	return find_window(found,wx+x,wy+y,rx,ry);
    }
    if (xdnd_debug)
        fprintf(stderr,"  done: %s\n",XtName(widget));
    return widget;
}

static int
check_window(Widget widget)
{
    Atom type;
    int format,rc;
    unsigned long nitems,rest;
    unsigned long *ldata;

    rc = XGetWindowProperty(XtDisplay(widget),XtWindow(widget),
			    XdndAware,0,64,False,AnyPropertyType,
			    &type,&format,&nitems,&rest,
			    (XtPointer)&ldata);
    XFree(ldata);
    return rc == Success && nitems > 0;
}

static XtEnum get_operation(Atom action)
{
    if (XdndActionCopy == action)
	return XmCOPY;
    if (XdndActionLink == action)
	return XmLINK;
    if (XdndActionMove == action)
	return XmMOVE;
    return 0;
}

static Atom get_action(XtEnum operation)
{
    if (XmCOPY == operation)
	return XdndActionCopy;
    if (XmLINK == operation)
	return XdndActionLink;
    if (XmMOVE == operation)
	return XdndActionMove;
    return None;
}

static void
XdndEvent(Widget widget, XtPointer clientdata, XEvent *event, Boolean *cont)
{
    switch(event->type) {
    case MapNotify:
	init_window(widget);
	break;
    }
}

/* ---------------------------------------------------------------------- */

/*
 * not very nice this way, but as you can hardly have two drags at the
 * same time with one pointer only it should be fine ...
 */
static Widget target;
static int target_ok,drop_ok;
static Window source;
static XtEnum operation;

void
XdndAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    char *name;
    XEvent reply;
        
    if (NULL == event)
	return;
    if (ClientMessage != event->type)
	return;

    if (XdndEnter == event->xclient.message_type) {
	if (xdnd_debug)
	    fprintf(stderr,"Xdnd: Enter: win=0x%lx ver=%ld more=%s\n",
		    event->xclient.data.l[0],
		    event->xclient.data.l[1] >> 24,
		    (event->xclient.data.l[1] & 1) ? "yes" : "no");
    }

    if (XdndPosition == event->xclient.message_type) {
	source = event->xclient.data.l[0];
	target = find_window(widget,0,0,
			     event->xclient.data.l[2] >> 16,
			     event->xclient.data.l[2] & 0xffff);
	target_ok = check_window(target);
	if (target_ok) {
	    operation = get_operation(event->xclient.data.l[4]);
	    operation = XmCOPY;  /* FIXME */
	    drop_ok   = 1;
	} else {
	    operation = 0;
	    drop_ok   = 0;
	}
	if (xdnd_debug) {
	    name = NULL;
	    if (event->xclient.data.l[4])
		name=XGetAtomName(XtDisplay(widget),event->xclient.data.l[4]);
	    fprintf(stderr,"Xdnd: Position: win=0x%lx pos=+%ld+%ld ts=%ld "
		    "ac=%s op=%d widget=%s drop=%s\n",
		    event->xclient.data.l[0],
		    event->xclient.data.l[2] >> 16,
		    event->xclient.data.l[2] & 0xffff,
		    event->xclient.data.l[3],
		    name,operation,
		    XtName(target),target_ok ? "yes" : "no");
	    if (name)
		XFree(name);
	}
	memset(&reply,0,sizeof(reply));
	reply.xany.type = ClientMessage;
	reply.xany.display = XtDisplay(widget);
	reply.xclient.window = event->xclient.data.l[0];
	reply.xclient.message_type = XdndStatus;
	reply.xclient.format = 32;
	reply.xclient.data.l[0] = XtWindow(widget);
	reply.xclient.data.l[1] = drop_ok ? 1 : 0;
	reply.xclient.data.l[4] = get_action(operation);
	XSendEvent(XtDisplay(widget),reply.xclient.window,0,0,&reply);
    }

    if (XdndDrop == event->xclient.message_type) {
	source = event->xclient.data.l[0];
	if (xdnd_debug)
	    fprintf(stderr,"Xdnd: Drop: win=0x%lx ts=%ld\n",
		    event->xclient.data.l[0],
		    event->xclient.data.l[2]);
	XmeNamedSink(target,XdndSelection,XmCOPY,NULL,
		     XtLastTimestampProcessed(XtDisplay(widget)));
    }

    if (XdndLeave == event->xclient.message_type) {
	source = 0;
	if (xdnd_debug)
	    fprintf(stderr,"Xdnd: Leave: win=0x%lx\n",
		    event->xclient.data.l[0]);
    }
}

void XdndDropFinished(Widget widget, XmSelectionCallbackStruct *scs)
{
    XEvent reply;

    if (XdndSelection != scs->selection)
	return;
    if (0 == source)
	return;

    if (xdnd_debug)
	fprintf(stderr,"Xdnd: sending Finished (0x%lx)\n",source);
    memset(&reply,0,sizeof(reply));
    reply.xany.type = ClientMessage;
    reply.xany.display = XtDisplay(widget);
    reply.xclient.window = source;
    reply.xclient.message_type = XdndFinished;
    reply.xclient.format = 32;
    while (!XtIsShell(widget))
	widget = XtParent(widget);
    reply.xclient.data.l[0] = XtWindow(widget);
    XSendEvent(XtDisplay(widget),reply.xclient.window,0,0,&reply);
    source = 0;
}

void XdndDropSink(Widget widget)
{
    XdndInit(XtDisplay(widget));

    if (XtWindow(widget))
	init_window(widget);
    XtAddEventHandler(widget,StructureNotifyMask,True,XdndEvent,NULL);
}
