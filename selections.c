#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <Xm/Xm.h>
#include <Xm/Transfer.h>
#include <Xm/TransferP.h>
#include <Xm/DragIcon.h>

#include "ida.h"
#include "readers.h"
#include "writers.h"
#include "viewer.h"
#include "xwd.h"
#include "xdnd.h"
#include "selections.h"
#include "list.h"

Atom XA_TARGETS, XA_DONE;
Atom XA_FILE_NAME, XA_FILE;
Atom XA_BACKGROUND, XA_FOREGROUND, XA_PIXEL;
Atom _MOTIF_EXPORT_TARGETS;
Atom _MOTIF_CLIPBOARD_TARGETS;
Atom _MOTIF_DEFERRED_CLIPBOARD_TARGETS;
Atom _MOTIF_SNAPSHOT;
Atom _MOTIF_DROP;
Atom _MOTIF_LOSE_SELECTION;
Atom _NETSCAPE_URL;
Atom MIME_TEXT_URI_LIST;
Atom MIME_IMAGE_PPM;
Atom MIME_IMAGE_PGM;
Atom MIME_IMAGE_XPM;
Atom MIME_IMAGE_BMP;
Atom MIME_IMAGE_JPEG;
Atom MIME_IMAGE_GIF;
Atom MIME_IMAGE_PNG;
Atom MIME_IMAGE_TIFF;

/* ---------------------------------------------------------------------- */
/* send data (drags, copy)                                                */

struct sel_data {
    struct list_head  list;
    Atom              atom;
    struct ida_image  img;
    Pixmap            pixmap;
    char              *filename;
    Pixmap            icon_pixmap;
    Widget            icon_widget;
};
static struct list_head selections;

static void
iconify(Widget widget, struct sel_data *data)
{
    struct ida_image small;
    unsigned int scale,x,y,depth;
    char *src,*dst;
    Arg args[4];
    Cardinal n=0;

    /* calc size */
    memset(&small,0,sizeof(small));
    for (scale = 1;; scale++) {
	small.i.width  = data->img.i.width  / scale;
	small.i.height = data->img.i.height / scale;
	if (small.i.width < 128 && small.i.height < 128)
	    break;
    }

    /* scale down & create pixmap */
    dst = small.data = malloc(small.i.width * small.i.height * 3);
    for (y = 0; y < small.i.height; y++) {
	src = data->img.data + 3 * y * scale * data->img.i.width;
	for (x = 0; x < small.i.width; x++) {
	    dst[0] = src[0];
	    dst[1] = src[1];
	    dst[2] = src[2];
	    dst += 3;
	    src += 3*scale;
	}
    }
    data->icon_pixmap = image_to_pixmap(&small);

    /* build DnD icon */
    n = 0;
    depth = DefaultDepthOfScreen(XtScreen(widget));
    XtSetArg(args[n], XmNpixmap, data->icon_pixmap); n++;
    XtSetArg(args[n], XmNwidth,  small.i.width); n++;
    XtSetArg(args[n], XmNheight, small.i.height); n++;
    XtSetArg(args[n], XmNdepth,  depth); n++;
    data->icon_widget = XmCreateDragIcon(widget,"dragicon",args,n);
    
    free(small.data);
}

static struct sel_data*
sel_find(Atom selection)
{
    struct list_head  *item;
    struct sel_data   *sel;
    
    list_for_each(item,&selections) {
	sel = list_entry(item, struct sel_data, list);
	if (sel->atom == selection)
	    return sel;
    }
    return NULL;
}

static void
sel_free(Atom selection)
{
    struct sel_data   *sel;

    sel = sel_find(selection);
    if (NULL == sel)
	return;
    if (sel->filename) {
	unlink(sel->filename);
	free(sel->filename);
    }
    if (sel->icon_widget)
	XtDestroyWidget(sel->icon_widget);
    if (sel->icon_pixmap)
	XFreePixmap(dpy,sel->icon_pixmap);
    if (sel->pixmap)
	XFreePixmap(dpy,sel->pixmap);
    if (sel->img.data)
	free(sel->img.data);

    list_del(&sel->list);
    free(sel);
}

static struct sel_data*
sel_init(Atom selection)
{
    struct sel_data   *sel;

    sel_free(selection);
    sel = malloc(sizeof(*sel));
    memset(sel,0,sizeof(*sel));

    sel->atom = selection;
    sel->img  = ida->img;
    sel->img.data = malloc(ida->img.i.width * ida->img.i.height * 3);
    memcpy(sel->img.data, ida->img.data,
	   ida->img.i.width * ida->img.i.height * 3);

    list_add_tail(&sel->list,&selections);
    return sel;
}

static void
sel_tmpfile(struct sel_data *sel, struct ida_writer *wr)
{
    static char *base = "ida";
    char *tmpdir;
    FILE *fp;
    int fd;

    tmpdir = getenv("TMPDIR");
    if (NULL == tmpdir)
	tmpdir="/tmp";
    sel->filename = malloc(strlen(tmpdir)+strlen(base)+16);
    sprintf(sel->filename,"%s/%s-XXXXXX",tmpdir,base);
    fd = mkstemp(sel->filename);
    fp = fdopen(fd,"w");
    wr->write(fp,&sel->img);
    fclose(fp);
}

Atom sel_unique_atom(Widget widget)
{
    char id_name[32];
    Atom id;
    int i;

    for (i = 0;; i++) {
	sprintf(id_name,"_IDA_DATA_%lX_%d",XtWindow(widget),i);
	id = XInternAtom(XtDisplay(widget),id_name,False);
	if (NULL == sel_find(id))
	    break;
    }
    return id;
}

void
selection_convert(Widget widget, XtPointer ignore, XtPointer call_data)
{
    XmConvertCallbackStruct *ccs = call_data;
    unsigned long *ldata;
    unsigned char *cdata;
    struct sel_data *sel;
    char *filename;
    int n;

    if (debug) {
	char *t = !ccs->target    ? NULL : XGetAtomName(dpy,ccs->target);
	char *s = !ccs->selection ? NULL : XGetAtomName(dpy,ccs->selection);
	fprintf(stderr,"conv: target=%s selection=%s\n",t,s);
	if (t) XFree(t);
	if (s) XFree(s);
    }

    /* tell which formats we can handle */
    if ((ccs->target == XA_TARGETS) ||
	(ccs->target == _MOTIF_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_DEFERRED_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_EXPORT_TARGETS)) {
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom)*12);
	if (ccs->target != _MOTIF_CLIPBOARD_TARGETS) {
	    ldata[n++] = XA_TARGETS;
	    ldata[n++] = MIME_IMAGE_PPM;
	    ldata[n++] = XA_PIXMAP;
	    ldata[n++] = XA_FOREGROUND;
	    ldata[n++] = XA_BACKGROUND;
	    ldata[n++] = XA_COLORMAP;
	    ldata[n++] = XA_FILE_NAME;
	    ldata[n++] = XA_FILE;
	    ldata[n++] = MIME_TEXT_URI_LIST;
	    ldata[n++] = _NETSCAPE_URL;
	}
	ccs->value  = ldata;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;

    } else if (ccs->target == _MOTIF_SNAPSHOT) {
	/* save away clipboard data */
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom));
	ldata[n++] = sel_unique_atom(widget);
	sel_init(ldata[0]);
	ccs->value  = ldata;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    sel = sel_find(ccs->selection);
    if (NULL == sel) {
	/* should not happen */
	fprintf(stderr,"Oops: selection not found\n");
	ccs->status = XmCONVERT_REFUSE;
	return;
    }

    if ((ccs->target == _MOTIF_LOSE_SELECTION) ||
	(ccs->target == XA_DONE)) {
	/* free stuff */
	sel_free(ccs->selection);
	ccs->value  = NULL;
	ccs->length = 0;
	ccs->type   = XA_INTEGER;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* convert data */
    if (ccs->target == XA_BACKGROUND ||
	ccs->target == XA_FOREGROUND ||
	ccs->target == XA_COLORMAP) {
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom)*8);
	if (ccs->target == XA_BACKGROUND) {
	    ldata[n++] = WhitePixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_FOREGROUND) {
	    ldata[n++] = BlackPixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_COLORMAP) {
	    ldata[n++] = DefaultColormapOfScreen(XtScreen(widget));
	    ccs->type  = XA_COLORMAP;
	}
	ccs->value  = ldata;
	ccs->length = n;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_PIXMAP) {
	/* xfer pixmap id */
	if (!sel->pixmap)
	    sel->pixmap = image_to_pixmap(&sel->img);
	ldata = (Pixmap*)XtMalloc(sizeof(Pixmap));
	ldata[0] = sel->pixmap;
	if (debug)
	    fprintf(stderr,"conv: pixmap id is 0x%lx\n",ldata[0]);
	ccs->value  = ldata;
	ccs->length = 1;
	ccs->type   = XA_DRAWABLE;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == MIME_IMAGE_PPM) {
	/* xfer image data directly */
	cdata = XtMalloc(sel->img.i.width * sel->img.i.height * 3 + 32);
	n = sprintf(cdata,"P6\n%d %d\n255\n",
		    sel->img.i.width, sel->img.i.height);
	memcpy(cdata+n, sel->img.data, sel->img.i.width*sel->img.i.height*3);
	ccs->value  = cdata;
	ccs->length = n + sel->img.i.width * sel->img.i.height * 3;
	ccs->type   = MIME_IMAGE_PPM;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_FILE_NAME       ||
	       ccs->target == XA_FILE            ||
	       ccs->target == XA_STRING          ||
	       ccs->target == MIME_TEXT_URI_LIST ||
	       ccs->target == _NETSCAPE_URL) {
	/* xfer image via tmp file */
	if (NULL == sel->filename)
	    sel_tmpfile(sel,&jpeg_writer);
	if (ccs->target == MIME_TEXT_URI_LIST ||
	    ccs->target == _NETSCAPE_URL) {
	    /* filename => url */
	    filename = XtMalloc(strlen(sel->filename)+8);
	    sprintf(filename,"file:%s\r\n",sel->filename);
	    ccs->type = ccs->target;
	    if (debug)
		fprintf(stderr,"conv: tmp url is %s\n",filename);
	} else {
	    filename = XtMalloc(strlen(sel->filename));
	    strcpy(filename,sel->filename);
	    ccs->type = XA_STRING;
	    if (debug)
		fprintf(stderr,"conv: tmp file is %s\n",filename);
	}
	ccs->value  = filename;
	ccs->length = strlen(filename);
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else {
	/* shouldn't happen */
	fprintf(stderr,"Huh? unknown target\n");
	ccs->status = XmCONVERT_REFUSE;
    }
}

static void
dnd_done(Widget widget, XtPointer ignore, XtPointer call_data)
{
    if (debug)
	fprintf(stderr,"conv: transfer finished\n");
    sel_free(_MOTIF_DROP);
}

/* ---------------------------------------------------------------------- */
/* receive data (drops, paste)                                            */

static Atom targets[16];
static Cardinal ntargets;

static void
selection_xfer(Widget widget, XtPointer ignore, XtPointer call_data)
{
    XmSelectionCallbackStruct *scs = call_data;
    unsigned char *cdata = scs->value;
    unsigned long *ldata = scs->value;
    Atom target = 0;
    unsigned int i,j,pending;
    char *file,*tmp;

    if (debug) {
	char *y = !scs->type      ? NULL : XGetAtomName(dpy,scs->type);
	char *t = !scs->target    ? NULL : XGetAtomName(dpy,scs->target);
	char *s = !scs->selection ? NULL : XGetAtomName(dpy,scs->selection);
	fprintf(stderr,"xfer: id=%p target=%s type=%s selection=%s\n",
		scs->transfer_id,t,y,s);
	if (y) XFree(y);
	if (t) XFree(t);
	if (s) XFree(s);
    }

    pending = scs->remaining;
    if (scs->target == XA_TARGETS) {
	/* look if we find a target we can deal with ... */
	for (i = 0; !target && i < scs->length; i++) {
	    for (j = 0; j < ntargets; j++) {
		if (ldata[i] == targets[j]) {
		    target = ldata[i];
		    break;
		}
	    }
	}
	if (target) {
	    XmTransferValue(scs->transfer_id, target, selection_xfer,
			    NULL, XtLastTimestampProcessed(dpy));
	    pending++;
	}
	if (debug) {
	    fprintf(stderr,"xfer: available targets: ");
	    for (i = 0; i < scs->length; i++) {
		char *name = !ldata[i] ? NULL : XGetAtomName(dpy,ldata[i]);
		fprintf(stderr,"%s%s", i != 0 ? ", " : "", name);
		XFree(name);
	    }
	    fprintf(stderr,"\n");
	    if (0 == scs->length)
		fprintf(stderr,"xfer: Huh? no TARGETS available?\n");
	}
    }

    if (scs->target == XA_FILE_NAME ||
	scs->target == XA_FILE) {
	/* load file */
	if (debug)
	    fprintf(stderr,"xfer: => \"%s\"\n",cdata);
	new_file(cdata,1);
    }

    if (scs->target == _NETSCAPE_URL) {
	/* load file */
	if (NULL != (tmp = strchr(cdata,'\n')))
	    *tmp = 0;
	if (NULL != (tmp = strchr(cdata,'\r')))
	    *tmp = 0;
	if (debug)
	    fprintf(stderr,"xfer: => \"%s\"\n",cdata);
	new_file(cdata,1);
    }

    if (scs->target == MIME_TEXT_URI_LIST) {
	/* load file(s) */
	for (file = strtok(cdata,"\r\n");
	     NULL != file;
	     file = strtok(NULL,"\r\n")) {
	    if (debug)
		fprintf(stderr,"xfer: => \"%s\"\n",file);
	    new_file(file,1);
	}
    }

    if (scs->target == XA_STRING) {
	/* might be a file name too, but don't complain if not */
	if (debug)
	    fprintf(stderr,"xfer: => \"%s\"\n",cdata);
	new_file(cdata,0);
    }

    if (scs->target == MIME_IMAGE_PPM   ||
	scs->target == MIME_IMAGE_PGM   ||
	scs->target == MIME_IMAGE_JPEG  ||
	scs->target == MIME_IMAGE_GIF   ||
	scs->target == MIME_IMAGE_PNG   ||
	scs->target == MIME_IMAGE_TIFF  ||
	scs->target == MIME_IMAGE_XPM   ||
	scs->target == MIME_IMAGE_BMP) {
	/* xfer image data directly */
	char *filename = load_tmpfile("ida");
	int fd;
	fd = mkstemp(filename);
	write(fd,scs->value,scs->length);
	close(fd);
	if (0 == viewer_loadimage(ida,filename,0)) {
	    ida->file = "selection";
	    resize_shell();
	}
	unlink(filename);
	free(filename);
    }

    if (scs->target == XA_PIXMAP) {
	/* beaming pixmaps between apps */
	Screen *scr;
	Window root;
	Pixmap pix;
	int x,y,w,h,bw,depth;
	XImage *ximage;
	struct ida_image img;
	
	pix = ldata[0];
	if (debug)
	    fprintf(stderr,"xfer: => id=0x%lx\n",pix);
	scr = XtScreen(widget);
	XGetGeometry(dpy,pix,&root,&x,&y,&w,&h,&bw,&depth);
	ximage = XGetImage(dpy,pix,0,0,w,h,-1,ZPixmap);
	parse_ximage(&img, ximage);
	XDestroyImage(ximage);
	viewer_setimage(ida,&img,"selection");
	resize_shell();
    }

    XFree(scs->value);
    if (1 == pending) {
	/* all done -- clean up */
	if (debug)
	    fprintf(stderr,"xfer: all done\n");
	XmTransferDone(scs->transfer_id, XmTRANSFER_DONE_SUCCEED);
	XdndDropFinished(widget,scs);
    }
}

void
selection_dest(Widget  w, XtPointer ignore, XtPointer call_data)
{
    XmDestinationCallbackStruct *dcs = call_data;

    if (NULL != sel_find(_MOTIF_DROP)) {
	if (debug)
	    fprintf(stderr,"dest: ignore self drop\n");
	XmTransferDone(dcs->transfer_id, XmTRANSFER_DONE_FAIL);
	return;
    }
    if (debug)
	fprintf(stderr,"dest: xfer id=%p\n",dcs->transfer_id);
    XmTransferValue(dcs->transfer_id, XA_TARGETS, selection_xfer,
		    NULL, XtLastTimestampProcessed(dpy));
}

/* ---------------------------------------------------------------------- */

void ipc_ac(Widget widget, XEvent *event, String *argv, Cardinal *argc)
{
    struct    sel_data *sel;
    Widget    drag;
    Arg       args[4];
    Cardinal  n=0;

    if (0 == *argc)
	return;

    if (debug)
	fprintf(stderr,"ipc: %s\n",argv[0]);
    
    if (0 == strcmp(argv[0],"paste")) {
	XmeClipboardSink(ida->widget,XmCOPY,NULL);

    } else if (0 == strcmp(argv[0],"copy")) {
	XmeClipboardSource(ida->widget,XmCOPY,XtLastTimestampProcessed(dpy));

    } else if (0 == strcmp(argv[0],"drag")) {
	sel = sel_init(_MOTIF_DROP);
	iconify(widget,sel);
	XtSetArg(args[n], XmNdragOperations, XmDROP_COPY); n++;
	XtSetArg(args[n], XmNsourcePixmapIcon, sel->icon_widget); n++;
	drag = XmeDragSource(ida->widget, NULL, event, args, n);
	XtAddCallback(drag, XmNdragDropFinishCallback, dnd_done, NULL);
    }
}

void dnd_add(Widget widget)
{
    Arg       args[4];
    Cardinal  n=0;

    XtSetArg(args[n], XmNimportTargets, targets); n++;
    XtSetArg(args[n], XmNnumImportTargets, ntargets); n++;
    XmeDropSink(widget,args,n);
    XdndDropSink(widget);
}

void ipc_init()
{
    _MOTIF_EXPORT_TARGETS =
	XInternAtom(dpy, "_MOTIF_EXPORT_TARGETS", False);
    _MOTIF_CLIPBOARD_TARGETS =
	XInternAtom(dpy, "_MOTIF_CLIPBOARD_TARGETS", False);
    _MOTIF_DEFERRED_CLIPBOARD_TARGETS =
	XInternAtom(dpy, "_MOTIF_DEFERRED_CLIPBOARD_TARGETS", False);
    _MOTIF_SNAPSHOT =
	XInternAtom(dpy, "_MOTIF_SNAPSHOT", False);
    _MOTIF_DROP =
	XInternAtom(dpy, "_MOTIF_DROP", False);
    _MOTIF_LOSE_SELECTION =
	XInternAtom(dpy, "_MOTIF_LOSE_SELECTION", False);

    XA_TARGETS         = XInternAtom(dpy, "TARGETS",       False);
    XA_DONE            = XInternAtom(dpy, "DONE",          False);
    XA_FILE_NAME       = XInternAtom(dpy, "FILE_NAME",     False);
    XA_FILE            = XInternAtom(dpy, "FILE",          False);
    XA_FOREGROUND      = XInternAtom(dpy, "FOREGROUND",    False);
    XA_BACKGROUND      = XInternAtom(dpy, "BACKGROUND",    False);
    XA_PIXEL           = XInternAtom(dpy, "PIXEL",         False);
    _NETSCAPE_URL      = XInternAtom(dpy, "_NETSCAPE_URL", False);

    MIME_TEXT_URI_LIST = XInternAtom(dpy, "text/uri-list", False);
    MIME_IMAGE_PPM     = XInternAtom(dpy, "image/ppm",     False);
    MIME_IMAGE_PGM     = XInternAtom(dpy, "image/pgm",     False);
    MIME_IMAGE_XPM     = XInternAtom(dpy, "image/xpm",     False);
    MIME_IMAGE_BMP     = XInternAtom(dpy, "image/bmp",     False);
    MIME_IMAGE_JPEG    = XInternAtom(dpy, "image/jpeg",    False);
    MIME_IMAGE_GIF     = XInternAtom(dpy, "image/gif",     False);
    MIME_IMAGE_PNG     = XInternAtom(dpy, "image/png",     False);
    MIME_IMAGE_TIFF    = XInternAtom(dpy, "image/tiff",    False);

    targets[ntargets++] = XA_FILE_NAME;
    targets[ntargets++] = XA_FILE;
    targets[ntargets++] = _NETSCAPE_URL;
    targets[ntargets++] = MIME_TEXT_URI_LIST;
    targets[ntargets++] = MIME_IMAGE_PPM;
    targets[ntargets++] = MIME_IMAGE_PGM;
    targets[ntargets++] = MIME_IMAGE_XPM;
    targets[ntargets++] = MIME_IMAGE_BMP;
    targets[ntargets++] = MIME_IMAGE_JPEG;
#ifdef HAVE_LIBUNGIF
    targets[ntargets++] = MIME_IMAGE_GIF;
#endif
#ifdef HAVE_LIBPNG
    targets[ntargets++] = MIME_IMAGE_PNG;
#endif
#ifdef HAVE_LIBTIFF
    targets[ntargets++] = MIME_IMAGE_TIFF;
#endif
    targets[ntargets++] = XA_PIXMAP;
    targets[ntargets++] = XA_STRING;

    INIT_LIST_HEAD(&selections);
}
