#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/Transfer.h>
#include <Xm/TransferP.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>
#include <Xm/ScrolledW.h>
#include <Xm/SelectioB.h>
#include <Xm/Text.h>

#include "RegEdit.h"
#include "list.h"
#include "ida.h"
#include "x11.h"
#include "icons.h"
#include "readers.h"
#include "filter.h"
#include "viewer.h"
#include "selections.h"
#include "filebutton.h"
#include "fileops.h"
#include "idaconfig.h"

/*----------------------------------------------------------------------*/

struct fileinfo {
    struct list_head       list;
    char                   *path;
    struct ida_image_info  img;
    Pixmap                 small;
    Pixmap                 large;
};

static LIST_HEAD(pcache);
static LIST_HEAD(pqueue);
static LIST_HEAD(files);
static XtWorkProcId pproc;

/*----------------------------------------------------------------------*/

static struct fileinfo*
fileinfo_cache_add(char *path, struct ida_image_info *img,
		   Pixmap small, Pixmap large)
{
    struct fileinfo *item;

    item = malloc(sizeof(*item));
    memset(item,0,sizeof(*item));
    item->path  = strdup(path);
    item->img   = *img;
    item->small = small;
    item->large = large;
    list_add_tail(&item->list,&pcache);
    return item;
}

static void fileinfo_cache_del(char *path)
{
    struct list_head *item;
    struct fileinfo *b;

    list_for_each(item,&pcache) {
	b = list_entry(item,struct fileinfo,list);
	if (0 == strcmp(path,b->path)) {
	    list_del(&b->list);
	    free(b);
	    return;
	}
    }
}

static struct fileinfo* fileinfo_cache_get(char *path)
{
    struct list_head *item;
    struct fileinfo *b;

    list_for_each(item,&pcache) {
	b = list_entry(item,struct fileinfo,list);
	if (0 == strcmp(path,b->path))
	    return b;
    }
    return 0;
}

/*----------------------------------------------------------------------*/

static void
fileinfo_cleanup(struct file_button *file)
{
    switch (file->state) {
    case 1:
	file->loader->done(file->wdata);
	break;
    case 2:
	desc_resize.done(file->wdata);
	break;
    }
    file->state = 0;

    if (file->wimg.data) {
	free(file->wimg.data);
	file->wimg.data = NULL;
    }
    if (file->simg.data) {
	free(file->simg.data);
	file->simg.data = NULL;
    }
    if (!list_empty(&file->queue)) {
	list_del_init(&file->queue);
    }
}

static void fileinfo_details(struct file_button *file)
{
    struct ida_image_info *img;
    struct ida_extra *extra;
    char buf[80];

    img = &file->info->img;
    snprintf(buf, sizeof(buf), "%dx%d",
	     img->thumbnail ? img->real_width  : img->width,
	     img->thumbnail ? img->real_height : img->height);
    XmStringFree(file->details[DETAIL_SIZE]);
    file->details[DETAIL_SIZE] = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT,NULL);

    extra = load_find_extra(img, EXTRA_COMMENT);
    if (extra) {
	XmStringFree(file->details[DETAIL_COMMENT]);
	file->details[DETAIL_COMMENT] =
	    XmStringGenerate(extra->data, NULL, XmMULTIBYTE_TEXT,NULL);
    }

    XtVaSetValues(file->widget,
		  XmNdetail, file->details,
		  XmNdetailCount, DETAIL_COUNT,
		  NULL);
}

static Boolean
fileinfo_loader(XtPointer clientdata)
{
    struct op_resize_parm resize;
    struct ida_rect rect;
    struct list_head *item;
    struct file_button *file;
    struct fileinfo *info;
    Pixmap pix;
    char blk[512];
    FILE *fp;
    float xs,ys,scale;
    struct ida_image timg;
    void *data;

    if (list_empty(&pqueue)) {
	/* nothing to do */
	pproc = 0;
	return TRUE;
    }
    file = list_entry(pqueue.next, struct file_button, queue);

    switch (file->state) {
    case 0:
	/* ------------------- new file -------------------- */
	info = fileinfo_cache_get(file->filename);
	if (info) {
	    file_set_info(file,info);
	    goto next;
	}
	
	/* open file */
	if (NULL == (fp = fopen(file->filename, "r"))) {
	    if (debug)
		fprintf(stderr,"open %s: %s\n",file->filename,
			strerror(errno));
	    goto unknown;
	}
	if (debug)
	    fprintf(stderr,"OPENED: %s\n",file->filename);
	fstat(fileno(fp),&file->st);
	
	/* pick loader */
	memset(blk,0,sizeof(blk));
	fread(blk,1,sizeof(blk),fp);
	rewind(fp);
	list_for_each(item,&loaders) {
	    file->loader = list_entry(item, struct ida_loader, list);
	    if (NULL == file->loader->magic)
		continue;
	    if (0 == memcmp(blk+file->loader->moff,file->loader->magic,
			    file->loader->mlen))
		break;
	    file->loader = NULL;
	}
	if (NULL == file->loader) {
	    if (debug)
		fprintf(stderr,"%s: unknown format\n",file->filename);
	    fclose(fp);
	    goto unknown;
	}

	/* load image */
	file->wdata = file->loader->init(fp, file->filename,
					 0, &file->wimg.i, 1);
	if (NULL == file->wdata) {
	    if (debug)
		fprintf(stderr,"loading %s [%s] FAILED\n",
			file->filename, file->loader->name);
	    goto unknown;
	}

	file->wimg.data = malloc(file->wimg.i.width * file->wimg.i.height * 3);
	file->state = 1;
	file->y     = 0;
	return FALSE;

    case 1:
	/* ------------------- loading file -------------------- */
	if (file->y < file->wimg.i.height) {
	    file->loader->read(file->wimg.data
			       + 3 * file->y * file->wimg.i.width,
			       file->y, file->wdata);
	    file->y++;
	    return FALSE;
	}
	file->loader->done(file->wdata);
	if (debug)
	    fprintf(stderr,"LOADED: %s [%ux%u]\n",
		    file->filename, file->wimg.i.width, file->wimg.i.height);
	
	/* resize image */
	xs = (float)GET_ICON_LARGE() / file->wimg.i.width;
	ys = (float)GET_ICON_LARGE() / file->wimg.i.height;
	scale = (xs < ys) ? xs : ys;
	resize.width  = file->wimg.i.width  * scale;
	resize.height = file->wimg.i.height * scale;
	if (0 == resize.width)
	    resize.width = 1;
	if (0 == resize.height)
	    resize.height = 1;
	
	rect.x1 = 0;
	rect.x2 = file->wimg.i.width;
	rect.y1 = 0;
	rect.y2 = file->wimg.i.height;
	file->wdata = desc_resize.init(&file->wimg,&rect,&file->simg.i,&resize);
	file->simg.data = malloc(file->simg.i.width * file->simg.i.height * 3);

	file->state = 2;
	file->y     = 0;
	return FALSE;

    case 2:
	/* ------------------- scaling file -------------------- */
	if (file->y < file->simg.i.height) {
	    desc_resize.work(&file->wimg,&rect, file->simg.data
			     + 3 * file->simg.i.width * file->y,
			     file->y, file->wdata);
	    file->y++;
	    return FALSE;
	}
	desc_resize.done(file->wdata);
	if (debug)
	    fprintf(stderr,"SCALED: %s [%ux%u]\n",
		    file->filename,file->simg.i.width,file->simg.i.height);

	/* scale once more (small icon) */
	xs = (float)GET_ICON_SMALL() / file->simg.i.width;
	ys = (float)GET_ICON_SMALL() / file->simg.i.height;
	scale = (xs < ys) ? xs : ys;
	resize.width  = file->simg.i.width  * scale;
	resize.height = file->simg.i.height * scale;
	if (0 == resize.width)
	    resize.width = 1;
	if (0 == resize.height)
	    resize.height = 1;
	
	rect.x1 = 0;
	rect.x2 = file->simg.i.width;
	rect.y1 = 0;
	rect.y2 = file->simg.i.height;
	data = desc_resize.init(&file->simg,&rect,&timg.i,&resize);
	timg.data = malloc(timg.i.width * timg.i.height * 3);

	for (file->y = 0; file->y < timg.i.height; file->y++)
	    desc_resize.work(&file->simg,&rect,
			     timg.data + 3 * timg.i.width * file->y,
			     file->y, data);
	desc_resize.done(data);

	/* build, cache + install pixmap */
	info = fileinfo_cache_add(file->filename,&file->wimg.i,
				  image_to_pixmap(&timg),
				  image_to_pixmap(&file->simg));
	file_set_info(file,info);
	free(timg.data);
	file->state = 0;
	goto next;

    default:
	/* shouldn't happen */
	fprintf(stderr,"Oops: %s:%d\n",__FILE__,__LINE__);
	exit(1);
    }

 unknown:
    /* generic file icon */
    pix = XmGetPixmap(file->screen,"unknown",0,0);
    file_set_icon(file,pix,pix);
    
 next:
    fileinfo_cleanup(file);
    return FALSE;
}

/*----------------------------------------------------------------------*/

void fileinfo_queue(struct file_button *file)
{
    if (NULL == file->queue.next)
	INIT_LIST_HEAD(&file->queue);

    if (!list_empty(&file->queue)) {
	/* already queued */
	if (0 == file->state)
	    return;
	fileinfo_cleanup(file);
    }

    file->state = 0;
    memset(&file->wimg,0,sizeof(file->wimg));
    memset(&file->simg,0,sizeof(file->simg));
    list_add_tail(&file->queue,&pqueue);
    if (0 == pproc)
	pproc = XtAppAddWorkProc(app_context,fileinfo_loader,NULL);
}

void fileinfo_invalidate(char *filename)
{
    struct file_button  *file;
    struct list_head    *item;
    Pixmap              pix;
    
    if (debug)
	fprintf(stderr,"fileinfo invalidate: %s\n",filename);
    fileinfo_cache_del(filename);

    list_for_each(item,&files) {
	file = list_entry(item, struct file_button, global);
	if (0 != strcmp(file->filename,filename))
	    continue;
	if (debug)
	    fprintf(stderr,"  %p %s\n",file,filename);
	file->info = NULL;
	pix = XmGetPixmap(file->screen,"file",0,0);
	file_set_icon(file,pix,pix);
	fileinfo_queue(file);
    }
}

/*----------------------------------------------------------------------*/

static void
container_ops_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    WidgetList children;
    Cardinal nchildren,i;
    
    XtVaGetValues(container,
		  XmNselectedObjects,&children,
		  XmNselectedObjectCount,&nchildren,
		  NULL);
    for (i = 0; i < nchildren; i++) {
	struct stat st;
	if (-1 == stat(XtName(children[i]),&st))
	    continue;
	if (!S_ISREG(st.st_mode))
	    continue;
	job_submit(XtName(widget),XtName(children[i]), NULL);
    }
}

static void
comment_box_cb(Widget widget, XtPointer clientdata, XtPointer calldata)
{
    Widget container = clientdata;
    XmSelectionBoxCallbackStruct *cd = calldata;
    WidgetList children;
    Cardinal nchildren,i;
    Widget text;
    char *comment;

    if (XmCR_OK == cd->reason) {
	/* TODO */
	text = XmSelectionBoxGetChild(widget,XmDIALOG_TEXT);
	comment = XmTextGetString(text);
	XtVaGetValues(container,
		      XmNselectedObjects,&children,
		      XmNselectedObjectCount,&nchildren,
		      NULL);
	for (i = 0; i < nchildren; i++) {
	    struct stat st;
	    if (-1 == stat(XtName(children[i]),&st))
		continue;
	    if (!S_ISREG(st.st_mode))
		continue;
	    job_submit("comment",XtName(children[i]), comment);
	}
    }
    XtDestroyWidget(XtParent(widget));
}

static void
container_comment_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    Widget box,text;
    WidgetList children;
    Cardinal nchildren;
    static struct fileinfo *info;
    struct ida_extra *extra;
    char *comment = "";

    XtVaGetValues(container,
		  XmNselectedObjects,&children,
		  XmNselectedObjectCount,&nchildren,
		  NULL);
    switch (nchildren) {
    case 0:
	/* nothing to do */
	return;
    case 1:
	/* get old comment */
	info = fileinfo_cache_get(XtName(children[0]));
	if (!info)
	    /* not a image */
	    return;
	extra = load_find_extra(&info->img, EXTRA_COMMENT);
	if (extra)
	    comment = extra->data;
	break;
    default:
	/* start with a empty comment */
	break;
    }
    
    /* dialog box */
    box = XmCreatePromptDialog(container,"comment",NULL,0);
    XtUnmanageChild(XmSelectionBoxGetChild(box,XmDIALOG_HELP_BUTTON));
    XmdRegisterEditres(XtParent(box));
    XtAddCallback(box,XmNokCallback,comment_box_cb,clientdata);
    XtAddCallback(box,XmNcancelCallback,comment_box_cb,clientdata);
    XtAddCallback(XtParent(box),XmNdestroyCallback,destroy_cb,XtParent(box));

    text = XmSelectionBoxGetChild(box,XmDIALOG_TEXT);
    XmTextSetString(text,comment);
    XmTextSetInsertionPosition(text,strlen(comment));
    XtManageChild(box);
}

void container_menu_ops(Widget menu, Widget container)
{
    Widget push;
    
    push = XtVaCreateManagedWidget("rotexif",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,container_ops_cb,container);
    push = XtVaCreateManagedWidget("rotcw",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,container_ops_cb,container);
    push = XtVaCreateManagedWidget("rotccw",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,container_ops_cb,container);
    push = XtVaCreateManagedWidget("comment",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,container_comment_cb,container);
}

/*----------------------------------------------------------------------*/

void
container_resize_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *d)
{
    Widget clip,scroll,container;
    Dimension width, height, ch;

    clip = widget;
    scroll = XtParent(widget);
    XtVaGetValues(scroll,XmNworkWindow,&container,NULL);

    XtVaGetValues(clip,
		  XtNwidth,  &width,
		  XtNheight, &height,
		  NULL);
    XtVaGetValues(container,
		  XtNheight, &ch,
		  NULL);
    if (ch < height-5)
	ch = height-5;
    XtVaSetValues(container,
		  XtNwidth, width-5,
		  XtNheight,ch,
		  NULL);
    container_relayout(container);
}

void
container_spatial_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    WidgetList children;
    Cardinal nchildren;

    XtVaSetValues(container,
		  XmNlayoutType,    XmSPATIAL,
		  XmNspatialStyle,  XmNONE,
		  NULL);
    nchildren = XmContainerGetItemChildren(container,NULL,&children);
    if (nchildren) {
	XtFree((XtPointer)children);
	/* FIXME: Hmm, why ??? */
	XtVaSetValues(container,
		      XmNentryViewType, XmLARGE_ICON,
		      NULL);
    }
    container_relayout(container);
}

void container_detail_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;

    XtVaSetValues(container,
		  XmNlayoutType,    XmDETAIL,
		  XmNentryViewType, XmSMALL_ICON,
		  NULL);
    container_relayout(container);
}

void
container_traverse_cb(Widget scroll, XtPointer clientdata, XtPointer call_data)
{
    XmTraverseObscuredCallbackStruct *cd = call_data;

    if (cd->reason == XmCR_OBSCURED_TRAVERSAL)
	XmScrollVisible(scroll, cd->traversal_destination, 25, 25);
}

void
container_convert_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmConvertCallbackStruct *ccs = call_data;
    char *file = NULL;
    Atom *targs;
    int i,n,len;
    WidgetList children;
    Cardinal nchildren;

    if (ccs->location_data) {
	Widget item = ccs->location_data;
	children  = &item;
	nchildren = 1;
    } else {
	XtVaGetValues(widget,
		      XmNselectedObjects,&children,
		      XmNselectedObjectCount,&nchildren,
		      NULL);
    }

    if (debug) {
	char *t = !ccs->target    ? NULL : XGetAtomName(dpy,ccs->target);
	char *s = !ccs->selection ? NULL : XGetAtomName(dpy,ccs->selection);
	fprintf(stderr,"drag: target=%s selection=%s [%d files,%p]\n",
		t, s, nchildren, ccs->location_data);
	if (t) XFree(t);
	if (s) XFree(s);
    }

    if ((ccs->target == XA_TARGETS)                        ||
	(ccs->target == _MOTIF_CLIPBOARD_TARGETS)          ||
	(ccs->target == _MOTIF_EXPORT_TARGETS)) {
	targs = (Atom*)XtMalloc(sizeof(Atom)*8);
	n = 0;
	if (nchildren >= 1) {
	    targs[n++] = MIME_TEXT_URI_LIST;
	}
	if (1 == nchildren) {
	    targs[n++] = XA_FILE_NAME;
	    targs[n++] = XA_FILE;
	    targs[n++] = _NETSCAPE_URL;
	    targs[n++] = XA_STRING;
	}
    	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_MERGE;
	return;
    }

    if (ccs->target == _MOTIF_DEFERRED_CLIPBOARD_TARGETS) {
	targs = (Atom*)XtMalloc(sizeof(Atom)*8);
	n = 0;
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
    }

    if ((ccs->target == _MOTIF_LOSE_SELECTION) ||
	(ccs->target == XA_DONE)) {
	/* free stuff */
	ccs->value  = NULL;
	ccs->length = 0;
	ccs->type   = XA_INTEGER;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    if (ccs->target == XA_FILE_NAME ||
	ccs->target == XA_FILE      ||
	ccs->target == XA_STRING) {
	file = XtMalloc(strlen(XtName(children[0])+1));
	strcpy(file,XtName(children[0]));
	ccs->value  = file;
	ccs->length = strlen(file);
	ccs->type   = XA_STRING;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    if (ccs->target == _NETSCAPE_URL ||
	ccs->target == MIME_TEXT_URI_LIST) {
	for (i = 0, len = 0; i < nchildren; i++)
	    len += strlen(XtName(children[i]));
	file = XtMalloc(len + 8 * nchildren);
	for (i = 0, len = 0; i < nchildren; i++)
	    len += sprintf(file+len,"file:%s\n",XtName(children[i]));
	ccs->value  = file;
	ccs->length = len;
	ccs->type   = XA_STRING;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    ccs->status = XmCONVERT_DEFAULT;
}

static void
container_copy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    XmeClipboardSource(container,XmCOPY,XtLastTimestampProcessed(dpy));
}

static void
container_paste_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    XmeClipboardSink(container,XmCOPY,NULL);
}

static void
container_del_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;
    WidgetList children,list;
    Cardinal nchildren,i;

    XtVaGetValues(container,
		  XmNselectedObjects,&children,
		  XmNselectedObjectCount,&nchildren,
		  NULL);
    list = malloc(sizeof(Widget*)*nchildren);
    memcpy(list,children,sizeof(Widget*)*nchildren);

    XtVaSetValues(container,
		  XmNselectedObjectCount,0,
		  NULL);
    XtUnmanageChildren(list,nchildren);
    for (i = 0; i < nchildren; i++)
        XtDestroyWidget(list[i]);
    free(list);
}

void container_menu_edit(Widget menu, Widget container,
			 int cut, int copy, int paste, int del)
{
    Widget push;

#if 0
    if (cut) {
	push = XtVaCreateManagedWidget("cut",xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,
		      container_cut_cb,container);
    }
#endif
    if (copy) {
	push = XtVaCreateManagedWidget("copy",xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,
		      container_copy_cb,container);
    }
    if (paste) {
	push = XtVaCreateManagedWidget("paste",xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,
		      container_paste_cb,container);
    }
    if (del) {
	push = XtVaCreateManagedWidget("del",xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,
		      container_del_cb,container);
    }
}

void container_menu_view(Widget menu, Widget container)
{
    Widget push;
    
    push = XtVaCreateManagedWidget("details",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,
		  container_detail_cb,container);
    push = XtVaCreateManagedWidget("spatial",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,
		  container_spatial_cb,container);
}

void
container_relayout(Widget container)
{
    Widget clip = XtParent(container);
    WidgetList children;
    Cardinal nchildren;
    Dimension wwidth,wheight;
    Dimension iwidth,iheight;
    Position x,y;
    unsigned char layout,style;
    int i,margin = 10;

    XtVaGetValues(container,
		  XmNlayoutType,   &layout,
		  XmNspatialStyle, &style,
		  NULL);
    if (XmSPATIAL != layout || XmNONE != style) {
	XmContainerRelayout(container);
	return;
    }

    nchildren = XmContainerGetItemChildren(container,NULL,&children);
    XtVaGetValues(clip,
		  XtNwidth,  &wwidth,
		  XtNheight, &wheight,
		  NULL);

    wwidth -= 5;
    x = margin; y = margin;
    for (i = 0; i < nchildren; i++) {
	if (!XtIsManaged(children[i]))
	    continue;
	XtVaGetValues(children[i],
		      XtNwidth,&iwidth,
		      XtNheight,&iheight,
		      NULL);
	if (x > 0 && x + iwidth + margin > wwidth) {
	    /* new row */
	    x = margin; y += iheight + margin;
	}
	XtVaSetValues(children[i],
		      XtNx,x, XtNy,y,
		      XmNpositionIndex,i,
		      NULL);
	x += iwidth + margin;
    }

    if (wheight < y + iheight + margin)
	wheight = y + iheight + margin;
    XtVaSetValues(container,
		  XtNwidth,  wwidth,
		  XtNheight, wheight,
		  NULL);
    if (nchildren)
	XtFree((XtPointer)children);
}

void
container_delwidgets(Widget container)
{
    WidgetList children;
    Cardinal nchildren;
    unsigned int i;

    /* delete widgets */
    XtVaSetValues(container,
		  XmNselectedObjectCount,0,
		  NULL);
    nchildren = XmContainerGetItemChildren(container,NULL,&children);
    XtUnmanageChildren(children,nchildren);
    for (i = 0; i < nchildren; i++)
        XtDestroyWidget(children[i]);
    if (nchildren)
	XtFree((XtPointer)children);
}

/*----------------------------------------------------------------------*/

void file_set_icon(struct file_button *file, Pixmap s, Pixmap l)
{
    Pixmap large, small;
    Pixel background;

    if (file->info)
	return;

    XtVaGetValues(file->widget, XmNbackground,&background, NULL);
    small = x11_icon_fit(DisplayOfScreen(file->screen), s, background,
			 GET_ICON_SMALL(), GET_ICON_SMALL());
    large = x11_icon_fit(DisplayOfScreen(file->screen), l, background,
			 GET_ICON_LARGE(), GET_ICON_LARGE());
    XtVaSetValues(file->widget,
		  XmNsmallIconPixmap, small,
		  XmNlargeIconPixmap, large,
		  NULL);
    if (file->small)
	XFreePixmap(DisplayOfScreen(file->screen),file->small);
    if (file->large)
	XFreePixmap(DisplayOfScreen(file->screen),file->large);
    file->small = small;
    file->large = large;
}

void file_set_info(struct file_button *file, struct fileinfo *info)
{
    file->info = NULL;
    file_set_icon(file,info->small,info->large);
    file->info = info;
    fileinfo_details(file);
}

#if 0
static void
file_copy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct file_button *file = clientdata;

    XmeClipboardSource(file->widget,XmCOPY,XtLastTimestampProcessed(dpy));
}
#endif

/*----------------------------------------------------------------------*/

int file_cmp_alpha(const struct file_button *aa,
		   const struct file_button *bb)
{
    if (S_ISDIR(aa->st.st_mode) != S_ISDIR(bb->st.st_mode))
	return S_ISDIR(aa->st.st_mode) ? -1 : 1;
    return strcmp(aa->basename,bb->basename);
}

static void
file_destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct file_button *file = clientdata;
    int i;

    if (debug)
	fprintf(stderr,"file: del %p [%s]\n",file,file->filename);

    if (NULL != file->queue.next)
	fileinfo_cleanup(file);
    
    if (file->basename)
	free(file->basename);
    if (file->filename)
	free(file->filename);

    XtVaSetValues(file->widget,
		  XmNsmallIconPixmap, XmUNSPECIFIED_PIXMAP,
		  XmNlargeIconPixmap, XmUNSPECIFIED_PIXMAP,
		  NULL);
    if (file->small)
	XFreePixmap(DisplayOfScreen(file->screen),file->small);
    if (file->large)
	XFreePixmap(DisplayOfScreen(file->screen),file->large);

    if (file->label)
	XmStringFree(file->label);
    for (i = 0; i < DETAIL_COUNT; i++)
	if (file->details[i])
	    XmStringFree(file->details[i]);

    list_del(&file->global);
    list_del(&file->window);
    free(file);
}

int file_createwidgets(Widget parent, struct file_button *file)
{
    struct fileinfo *info;
    Pixmap pix;
    Arg args[8];
    int i, n = 0;

    if (debug)
	fprintf(stderr,"file: new %p [%s]\n",file,file->filename);

    file->screen = XtScreen(parent);
    file->label  = XmStringGenerate(file->basename, NULL, XmMULTIBYTE_TEXT,NULL);
    for (i = 0; i < DETAIL_COUNT; i++)
	file->details[i] = XmStringGenerate("-", NULL, XmMULTIBYTE_TEXT,NULL);

    XtSetArg(args[n], XmNlabelString,     file->label);   n++;
    XtSetArg(args[n], XmNdetail,          file->details); n++;
    XtSetArg(args[n], XmNdetailCount,     DETAIL_COUNT);  n++;
    file->widget = XmCreateIconGadget(parent,file->filename,args,n);
    XtAddCallback(file->widget,XtNdestroyCallback,file_destroy_cb,file);
    list_add_tail(&file->global,&files);

    info = fileinfo_cache_get(file->filename);
    if (info) {
	file_set_info(file,info);
    } else {
	pix = XmGetPixmap(file->screen,"question",0,0);
	file_set_icon(file,pix,pix);
    }
    return 0;
}
