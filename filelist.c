/*
 * file list management ("virtual photo album").
 * (c) 2003 Gerd Hoffmann <kraxel@bytesex.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/XShm.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/CascadeB.h>
#include <Xm/ScrolledW.h>
#include <Xm/SelectioB.h>
#include <Xm/Transfer.h>
#include <Xm/TransferP.h>
#include <Xm/Container.h>
#include <Xm/FileSB.h>
#include <Xm/Separator.h>

#include "RegEdit.h"
#include "ida.h"
#include "readers.h"
#include "viewer.h"
#include "browser.h"
#include "filter.h"
#include "x11.h"
#include "dither.h"
#include "selections.h"
#include "filebutton.h"
#include "filelist.h"
#include "xdnd.h"
#include "idaconfig.h"

/*----------------------------------------------------------------------*/

struct list_handle;

struct list_handle {
    char              *filename;
    struct list_head  files;

    Widget            shell;
    Widget            scroll;
    Widget            container;
    Widget            status;
    XmString          details[DETAIL_COUNT+1];

    Widget            loadbox;
    Widget            savebox;

    XtWorkProcId      wproc;
};

/* ---------------------------------------------------------------------- */

static void filelist_add(struct list_handle *h, char *filename)
{
    struct file_button *file;
    char *tmp;

    /* fixup filename */
    if (0 == strncmp(filename,"file:",5))
	filename += 5;
    if (NULL != (tmp = strchr(filename,'\n')))
	*tmp = 0;
    if (NULL != (tmp = strchr(filename,'\r')))
	*tmp = 0;
    if (0 == strlen(filename))
	return;

    /* add file */
    file = malloc(sizeof(*file));
    memset(file,0,sizeof(*file));
    
    tmp = strrchr(filename,'/');
    if (!tmp)
	goto oops;
    file->basename = strdup(tmp+1);
    file->filename = strdup(filename);
    
    if (-1 == stat(file->filename,&file->st)) {
	fprintf(stderr,"stat %s: %s\n",file->filename,strerror(errno));
	goto oops;
    }
    if (!S_ISREG(file->st.st_mode)) {
	fprintf(stderr,"%s: not a regular file\n",file->filename);
	goto oops;
    }

    list_add_tail(&file->window,&h->files);
    file_createwidgets(h->container, file);
    XtManageChild(file->widget);
    fileinfo_queue(file);
    container_relayout(h->container);
    return;

 oops:
    if (file->filename)
	free(file->filename);
    if (file->basename)
	free(file->basename);
    free(file);
}

static void filelist_file(struct list_handle *h, char *filename)
{
    if (h->filename == filename)
	return;
    if (h->filename)
	free(h->filename);
    h->filename = strdup(filename);
    XtVaSetValues(h->shell,XtNtitle,h->filename,NULL);
}

static void filelist_read(struct list_handle *h, char *filename)
{
    FILE *fp;
    char line[128];

    fp = fopen(filename,"r");
    if (NULL == fp) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return;
    }
    while (NULL != fgets(line, sizeof(line), fp)) {
	filelist_add(h, line);
    }
    fclose(fp);
    filelist_file(h,filename);
    container_relayout(h->container);
}

static void filelist_write(struct list_handle *h, char *filename)
{
    struct file_button *file;
    struct list_head *item;
    FILE *fp;

    fp = fopen(filename,"w");
    if (NULL == fp) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return;
    }
    list_for_each(item, &h->files) {
	file = list_entry(item, struct file_button, window);
	fprintf(fp,"%s\n",file->filename);
    }
    fclose(fp);
    filelist_file(h,filename);
}

static void filelist_delall(struct list_handle *h)
{
    struct file_button *file;
    struct list_head *item;

    list_for_each(item, &h->files) {
	file = list_entry(item, struct file_button, window);
	XtUnmanageChild(file->widget);
	XtDestroyWidget(file->widget);
    }
}

/* ---------------------------------------------------------------------- */
/* receive data (drops, paste)                                            */

static Atom targets[16];
static Cardinal ntargets;

static void
filelist_xfer(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;
    XmSelectionCallbackStruct *scs = call_data;
    unsigned char *cdata = scs->value;
    unsigned long *ldata = scs->value;
    Atom target = 0;
    unsigned int i,j,pending;
    char *file;

    if (debug) {
	char *y = !scs->type      ? NULL : XGetAtomName(dpy,scs->type);
	char *t = !scs->target    ? NULL : XGetAtomName(dpy,scs->target);
	char *s = !scs->selection ? NULL : XGetAtomName(dpy,scs->selection);
	fprintf(stderr,"list: id=%p target=%s type=%s selection=%s\n",
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
	    XmTransferValue(scs->transfer_id, target, filelist_xfer,
			    clientdata, XtLastTimestampProcessed(dpy));
	    pending++;
	}
	if (debug) {
	    fprintf(stderr,"list: available targets: ");
	    for (i = 0; i < scs->length; i++) {
		char *name = !ldata[i] ? NULL : XGetAtomName(dpy,ldata[i]);
		fprintf(stderr,"%s%s", i != 0 ? ", " : "", name);
		XFree(name);
	    }
	    fprintf(stderr,"\n");
	    if (0 == scs->length)
		fprintf(stderr,"list: Huh? no TARGETS available?\n");
	}
    }

    if (scs->target == XA_FILE_NAME ||
	scs->target == XA_FILE) {
	/* load file */
	if (debug)
	    fprintf(stderr,"list: => \"%s\"\n",cdata);
	filelist_add(h,cdata);
    }

    if (scs->target == _NETSCAPE_URL) {
	/* load file */
	if (debug)
	    fprintf(stderr,"list: => \"%s\"\n",cdata);
	filelist_add(h,cdata);
    }

    if (scs->target == MIME_TEXT_URI_LIST) {
	/* load file(s) */
	for (file = strtok(cdata,"\r\n");
	     NULL != file;
	     file = strtok(NULL,"\r\n")) {
	    if (debug)
		fprintf(stderr,"list: => \"%s\"\n",file);
	    filelist_add(h,file);
	}
    }

    XFree(scs->value);
    if (1 == pending) {
	/* all done -- clean up */
	if (debug)
	    fprintf(stderr,"list: all done\n");
	XmTransferDone(scs->transfer_id, XmTRANSFER_DONE_SUCCEED);
	XdndDropFinished(widget,scs);
    }
}

static void
filelist_dest_cb(Widget  w, XtPointer clientdata, XtPointer call_data)
{
    XmDestinationCallbackStruct *dcs = call_data;

    if (debug)
	fprintf(stderr,"list: xfer id=%p\n",dcs->transfer_id);
    XmTransferValue(dcs->transfer_id, XA_TARGETS, filelist_xfer,
		    clientdata, XtLastTimestampProcessed(dpy));
}

/*----------------------------------------------------------------------*/

static void
filelist_new_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;

    filelist_delall(h);
    if (h->filename) {
	free(h->filename);
	h->filename = NULL;
    }
    XtVaSetValues(h->shell,XtNtitle,"new list",NULL);
}

static void
init_file_box(Widget box, char *filename)
{
    char *dir,*file;
    XmString s1;

    if (NULL == filename) {
	dir = strdup(ida_lists);
    } else {
	dir = strdup(filename);
	file = strrchr(dir,'/');
	if (NULL == file)
	    return;
	*file = 0;
	file++;
    }

    s1 = XmStringGenerate(dir, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(box,
		  XmNdirectory, s1,
		  XmNpattern,   NULL,
		  NULL);
    XmFileSelectionDoSearch(box,NULL);
    XmStringFree(s1);
    free(dir);
}

static void
load_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    struct list_handle *h = clientdata;
    char *filename;

    if (cb->reason == XmCR_OK) {
	filename = XmStringUnparse(cb->value,NULL,
				   XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
				   NULL,0,0);
	if (debug)
	    fprintf(stderr,"read list from %s\n",filename);
	filelist_read(h, filename);
    }
    XtUnmanageChild(widget);
}

static void
filelist_load_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;
    Widget help;

    if (NULL == h->loadbox) {
	h->loadbox = XmCreateFileSelectionDialog(h->shell,"load",NULL,0);
	help = XmFileSelectionBoxGetChild(h->loadbox,XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help);
	XtAddCallback(h->loadbox,XmNokCallback,load_done_cb,h);
	XtAddCallback(h->loadbox,XmNcancelCallback,load_done_cb,h);
    }
    init_file_box(h->loadbox,h->filename);
    XtManageChild(h->loadbox);
}

static void
save_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    struct list_handle *h = clientdata;
    char *filename;

    if (cb->reason == XmCR_OK) {
	filename = XmStringUnparse(cb->value,NULL,
				   XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
				   NULL,0,0);
	if (debug)
	    fprintf(stderr,"write list to %s\n",filename);
	filelist_write(h, filename);
    }
    XtUnmanageChild(widget);
}

static void
filelist_save_as_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;
    Widget help;

    if (NULL == h->savebox) {
	h->savebox = XmCreateFileSelectionDialog(h->shell,"save",NULL,0);
	help = XmFileSelectionBoxGetChild(h->savebox,XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help);

	XtAddCallback(h->savebox,XmNokCallback,save_done_cb,h);
	XtAddCallback(h->savebox,XmNcancelCallback,save_done_cb,h);
    }
    init_file_box(h->savebox,h->filename);
    XtManageChild(h->savebox);
}

static void
filelist_save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;

    if (h->filename) {
	filelist_write(h, h->filename);
    } else {
	filelist_save_as_cb(widget, h, call_data);
    }
}

static void
filelist_destroy(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;

    if (h->filename)
	free(h->filename);
    ptr_unregister(h->shell);
    free(h);
}

static void
filelist_list_load(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct list_handle *h = clientdata;

    filelist_delall(h);
    filelist_read(h, XtName(widget));
}

static void filelist_builddir(Widget menu, char *path, XtPointer clientdata)
{
    Widget push,submenu;
    XmString str;
    char filename[1024];
    struct dirent *ent;
    struct stat st;
    DIR *dir;

    dir = opendir(path);
    while (NULL != (ent = readdir(dir))) {
	if (ent->d_name[0] == '.')
	    continue;
	snprintf(filename,sizeof(filename),"%s/%s",
		 path,ent->d_name);
	if (-1 == lstat(filename,&st))
	    continue;

	str = XmStringGenerate(ent->d_name,NULL, XmMULTIBYTE_TEXT,NULL);
	if (S_ISREG(st.st_mode)) {
	    push = XtVaCreateManagedWidget(filename,
					   xmPushButtonWidgetClass,menu,
					   XmNlabelString,str,
					   NULL);
	    XtAddCallback(push,XmNactivateCallback,filelist_list_load,clientdata);
	}
	if (S_ISDIR(st.st_mode)) {
	    submenu = XmCreatePulldownMenu(menu,"subdirM",NULL,0);
	    XtVaCreateManagedWidget("subdir",xmCascadeButtonWidgetClass,menu,
				    XmNlabelString,str,
				    XmNsubMenuId,submenu,
				    NULL);
	    filelist_builddir(submenu,filename,clientdata);
	}
	XmStringFree(str);
    }
    closedir(dir);
}

static void
filelist_lists(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    WidgetList children,list;
    Cardinal nchildren;
    int i;

    XtVaGetValues(widget,
		  XtNchildren,&children,
		  XtNnumChildren,&nchildren,
		  NULL);
    list = malloc(sizeof(Widget*)*nchildren);
    memcpy(list,children,sizeof(Widget*)*nchildren);
    for (i = 0; i < nchildren; i++)
        XtDestroyWidget(list[i]);
    free(list);

    filelist_builddir(widget,ida_lists,clientdata);
}

static void
filelist_action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmContainerSelectCallbackStruct *cd = call_data;
    char *file;

    if (XmCR_DEFAULT_ACTION == cd->reason && 1 == cd->selected_item_count) {
	file = XtName(cd->selected_items[0]);
	if (debug)
	    fprintf(stderr,"browser: action %s\n", file);
	new_file(file,1);
    }
}

/*----------------------------------------------------------------------*/

void
filelist_window(void)
{
    Widget form,clip,menubar,menu,push;
    struct list_handle *h;
    Arg args[8];
    int n = 0;

    if (0 == ntargets) {
	/* first time init */
	targets[ntargets++] = MIME_TEXT_URI_LIST;
	targets[ntargets++] = XA_FILE_NAME;
	targets[ntargets++] = XA_FILE;
	targets[ntargets++] = _NETSCAPE_URL;
    }

    h = malloc(sizeof(*h));
    if (NULL == h) {
	fprintf(stderr,"out of memory");
	return;
    }
    memset(h,0,sizeof(*h));
    INIT_LIST_HEAD(&h->files);
    
    h->shell = XtVaAppCreateShell("filelist","Ida",
				  topLevelShellWidgetClass,
				  dpy,
				  XtNclientLeader,app_shell,
				  XmNdeleteResponse,XmDESTROY,
				  XtNtitle,"new list",
				  NULL);
    XmdRegisterEditres(h->shell);
    XtAddCallback(h->shell,XtNdestroyCallback,filelist_destroy,h);

    /* widgets */
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, h->shell,
				   NULL);
    menubar = XmCreateMenuBar(form,"cbar",NULL,0);
    XtManageChild(menubar);
    h->status = XtVaCreateManagedWidget("status",xmLabelWidgetClass, form,
					NULL);

    /* scrolled container */
    h->details[0] = XmStringGenerate("Image", NULL, XmMULTIBYTE_TEXT,NULL);
    h->details[DETAIL_SIZE+1] =
	XmStringGenerate("Size", NULL, XmMULTIBYTE_TEXT,NULL);
    h->details[DETAIL_COMMENT+1] =
	XmStringGenerate("Comment", NULL, XmMULTIBYTE_TEXT,NULL);
    XtSetArg(args[n], XmNdetailColumnHeading, h->details); n++;
    XtSetArg(args[n], XmNdetailColumnHeadingCount, DETAIL_COUNT+1);  n++;
    
    h->scroll = XmCreateScrolledWindow(form, "scroll", NULL, 0);
    XtManageChild(h->scroll);
    h->container = XmCreateContainer(h->scroll,"container",
				     args,n);
    XtManageChild(h->container);
    XdndDropSink(h->container);

    XtAddCallback(h->scroll, XmNtraverseObscuredCallback,
		  container_traverse_cb, NULL);
    XtAddCallback(h->container,XmNdefaultActionCallback,
		  filelist_action_cb,h);
    XtAddCallback(h->container,XmNconvertCallback,
		  container_convert_cb,h);
    XtAddCallback(h->container,XmNdestinationCallback,
		  filelist_dest_cb,h);

    XtVaGetValues(h->scroll,XmNclipWindow,&clip,NULL);
    XtAddEventHandler(clip,StructureNotifyMask,True,container_resize_eh,NULL);

    /* menu - file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("new",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,filelist_new_cb,h);
    push = XtVaCreateManagedWidget("load",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,filelist_load_cb,h);
    push = XtVaCreateManagedWidget("save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,filelist_save_cb,h);
    push = XtVaCreateManagedWidget("saveas",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,filelist_save_as_cb,h);

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("close",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,destroy_cb,h->shell);

    /* menu - edit */
    menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
    XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    container_menu_edit(menu,h->container, 0,1,1,1);

    /* menu - view */
    menu = XmCreatePulldownMenu(menubar,"viewM",NULL,0);
    XtVaCreateManagedWidget("view",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    container_menu_view(menu,h->container);
    
    /* menu - lists */
    menu = XmCreatePulldownMenu(menubar,"listsM",NULL,0);
    XtVaCreateManagedWidget("lists",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    XtAddCallback(menu, XmNmapCallback, filelist_lists, h);

    /* read dir and show window */
    container_detail_cb(NULL,h->container,NULL);
    XtPopup(h->shell,XtGrabNone);
    ptr_register(h->shell);
}

void
filelist_ac(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    filelist_window();
}
