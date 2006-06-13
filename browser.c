/*
 * simple file browser
 * (c) 2001-03 Gerd Hoffmann <kraxel@bytesex.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fnmatch.h>
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
#include "misc.h"
#include "idaconfig.h"
#include "desktop.h"

/*----------------------------------------------------------------------*/

struct browser_handle;

struct browser_handle {
    char                 *dirname;
    char                 *lastdir;
    char                 *filter;
    Widget               shell;
    Widget               scroll;
    Widget               container;
    Widget               status;
    XmString             details[DETAIL_COUNT+1];

    struct list_head     files;
    struct list_head     *item;
    unsigned int         dirs,sfiles,afiles;

    XtWorkProcId         wproc;
};

/*----------------------------------------------------------------------*/

static void dir_info(struct file_button *file)
{
    char path[256];
    char comment[256];
    int len;
    
    snprintf(path,sizeof(path),"%s/.directory",file->filename);
    len = desktop_read_entry(path, "Comment=", comment, sizeof(comment));

    if (len) {
	XmStringFree(file->details[DETAIL_COMMENT]);
	file->details[DETAIL_COMMENT] =
	    XmStringGenerate(comment, NULL, XmMULTIBYTE_TEXT,NULL);
	XtVaSetValues(file->widget,
		      XmNdetail, file->details,
		      XmNdetailCount, DETAIL_COUNT,
		      NULL);
    }
}

static Boolean
browser_statfiles(XtPointer clientdata)
{
    struct browser_handle *h = clientdata;
    struct file_button *file;
    struct list_head *item;
    char line[80];
    XmString str;
    Pixmap pix;
    char *type;

    if (h->item == &h->files) {
	/* done => read thumbnails now */
	h->wproc = 0;
	list_for_each(item,&h->files) {
	    file = list_entry(item, struct file_button, window);
	    if ((file->st.st_mode & S_IFMT) != S_IFREG)
		continue;
	    fileinfo_queue(file);
	}

	list_for_each(item,&h->files) {
	    file = list_entry(item, struct file_button, window);
	    if ((file->st.st_mode & S_IFMT) != S_IFDIR)
		continue;
	    dir_info(file);
	}

	/* update status line */
	if (h->filter) {
	    snprintf(line, sizeof(line), "%d dirs, %d/%d files [%s]",
		     h->dirs,h->sfiles,h->afiles,h->filter);
	} else {
	    snprintf(line, sizeof(line), "%d dirs, %d files",
		     h->dirs,h->afiles);
	}
	str = XmStringGenerate(line, NULL, XmMULTIBYTE_TEXT, NULL);
	XtVaSetValues(h->status,XmNlabelString,str,NULL);
	XmStringFree(str);
	h->item = NULL;
	return TRUE;
    }

    /* handle file */
    file = list_entry(h->item, struct file_button, window);
    switch (file->st.st_mode & S_IFMT) {
    case S_IFDIR:
	type = "dir";
	break;
    case S_IFREG:
	type = "file";
	break;
    default:
	type = NULL;
    }
    if (type) {
	pix = XmGetPixmap(XtScreen(h->container),type,0,0);
	file_set_icon(file,pix,pix);
    }

    h->item = h->item->next;
    return FALSE;
}

static void list_add_sorted(struct browser_handle *h, struct file_button *add)
{
    struct file_button *file;
    struct list_head *item;

    list_for_each(item,&h->files) {
	file = list_entry(item, struct file_button, window);
	if (file_cmp_alpha(add,file) <= 0) {
	    list_add_tail(&add->window,&file->window);
	    return;
	}
    }
    list_add_tail(&add->window,&h->files);
}

static Boolean browser_readdir(XtPointer clientdata)
{
    struct browser_handle *h = clientdata;
    struct file_button *file;
    struct list_head *item;
    struct dirent *dirent;
    WidgetList children;
    struct file_button *lastdir = NULL;
    Cardinal nchildren;
    XmString str,elem;
    DIR *dir;
    unsigned int len;

    /* status line */
    str  = XmStringGenerate("scanning ", NULL, XmMULTIBYTE_TEXT, NULL);
    elem = XmStringGenerate(h->dirname, NULL, XmMULTIBYTE_TEXT, NULL);
    str  = XmStringConcatAndFree(str,elem);
    elem = XmStringGenerate(" ...", NULL, XmMULTIBYTE_TEXT, NULL);
    str  = XmStringConcatAndFree(str,elem);
    XtVaSetValues(h->status,XmNlabelString,str,NULL);
    XmStringFree(str);
    ptr_busy();

    /* read + sort dir */
    dir = opendir(h->dirname);
    if (NULL == dir) {
	fprintf(stderr,"opendir %s: %s\n",h->dirname,strerror(errno));
	return -1;
    }
    h->dirs = 0;
    h->sfiles = 0;
    h->afiles = 0;
    while (NULL != (dirent = readdir(dir))) {
	/* skip dotfiles */
	if (dirent->d_name[0] == '.' && 0 != strcmp(dirent->d_name,".."))
	    continue;

	/* get memory */
	file = malloc(sizeof(*file));
	memset(file,0,sizeof(*file));

	/* get file type */
	file->basename = strdup(dirent->d_name);
	file->d_type   = dirent->d_type;
	if (0 == strcmp(dirent->d_name, "..")) {
	    char *slash;
	    file->filename = strdup(h->dirname);
	    slash = strrchr(file->filename,'/');
	    if (slash == file->filename)
		slash++;
	    *slash = 0;
	} else {
	    len = strlen(h->dirname)+strlen(file->basename)+4;
	    file->filename = malloc(len);
	    if (0 == strcmp(h->dirname,"/")) {
		sprintf(file->filename,"/%s",file->basename);
	    } else {
		sprintf(file->filename,"%s/%s",h->dirname,
			file->basename);
	    }
	}
	if (file->d_type != DT_UNKNOWN) {
	    file->st.st_mode = DTTOIF(file->d_type);
	} else {
	    if (-1 == stat(file->filename, &file->st)) {
		fprintf(stderr,"stat %s: %s\n",
			file->filename,strerror(errno));
	    }
	}

	/* user-specified filter */
	if (S_ISDIR(file->st.st_mode)) {
	    h->dirs++;
	    if (h->lastdir && 0 == strcmp(h->lastdir,file->filename)) {
		lastdir = file;
	    }
	} else {
	    h->afiles++;
	    if (h->filter && 0 != fnmatch(h->filter,dirent->d_name,0)) {
		free(file);
		continue;
	    } else
		h->sfiles++;
	}

	list_add_sorted(h,file);
    }
    closedir(dir);

    /* create & manage widgets */
    list_for_each(item,&h->files) {
	file = list_entry(item, struct file_button, window);
	file_createwidgets(h->container, file);
    }
    nchildren = XmContainerGetItemChildren(h->container,NULL,&children);
    XtManageChildren(children,nchildren);
    if (nchildren)
	XtFree((XtPointer)children);
    container_relayout(h->container);
    if (h->lastdir) {
	if (lastdir) {
	    if (debug)
		fprintf(stderr,"lastdir: %s\n",h->lastdir);
	    XtVaSetValues(h->container,
			  XmNinitialFocus, lastdir->widget,
			  NULL);
// 	    XmScrollVisible(h->scroll, lastdir->widget, 25, 25);
//	    XtSetKeyboardFocus(h->shell,h->container);
	}
	free(h->lastdir);
	h->lastdir = NULL;
    }
    XtVaSetValues(h->shell,XtNtitle,h->dirname,NULL);
    ptr_idle();

    /* start bg processing */
    h->item  = h->files.next;
    h->wproc = XtAppAddWorkProc(app_context,browser_statfiles,h);
    return TRUE;
}

static void browser_bgcancel(struct browser_handle *h)
{
    if (h->wproc)
	XtRemoveWorkProc(h->wproc);
    h->wproc = 0;
}

/*----------------------------------------------------------------------*/

static void
browser_cd(struct browser_handle *h, char *dir)
{
    /* build new dir path */
    if (h->lastdir)
	free(h->lastdir);
    h->lastdir = h->dirname;
    h->dirname = strdup(dir);

    /* cleanup old stuff + read dir */
    browser_bgcancel(h);
    container_delwidgets(h->container);
    h->wproc = XtAppAddWorkProc(app_context,browser_readdir,h);
}

static void
browser_filter_done(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct browser_handle *h = clientdata;
    XmSelectionBoxCallbackStruct *cb = call_data;
    char *filter;

    if (cb->reason == XmCR_OK) {
	filter = XmStringUnparse(cb->value,NULL,
				 XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
				 NULL,0,0);
	if (h->filter)
	    free(h->filter);
	h->filter = NULL;
	if (strlen(filter) > 0)
	    h->filter = strdup(filter);
	XtFree(filter);
	
	if (debug)
	    fprintf(stderr,"filter: %s\n", h->filter ? h->filter : "[none]");
	browser_bgcancel(h);
	container_delwidgets(h->container);
	h->wproc = XtAppAddWorkProc(app_context,browser_readdir,h);
    }
    XtDestroyWidget(widget);
}

static void
browser_filter(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct browser_handle *h = clientdata;
    Widget shell;

    shell = XmCreatePromptDialog(h->shell,"filter",NULL,0);
    XtUnmanageChild(XmSelectionBoxGetChild(shell,XmDIALOG_HELP_BUTTON));
    XtAddCallback(shell,XmNokCallback,browser_filter_done,h);
    XtAddCallback(shell,XmNcancelCallback,browser_filter_done,h);
    XtManageChild(shell);
}

static void
browser_nofilter(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct browser_handle *h = clientdata;

    if (!h->filter)
	return;
    if (debug)
	fprintf(stderr,"filter: reset\n");
    free(h->filter);
    h->filter = NULL;
    
    browser_bgcancel(h);
    container_delwidgets(h->container);
    h->wproc = XtAppAddWorkProc(app_context,browser_readdir,h);
}

/*----------------------------------------------------------------------*/

static void
browser_action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmContainerSelectCallbackStruct *cd = call_data;
    struct browser_handle *h = clientdata;
    struct stat st;
    char *file;

    if (XmCR_DEFAULT_ACTION == cd->reason && 1 == cd->selected_item_count) {
	file = XtName(cd->selected_items[0]);
	if (debug)
	    fprintf(stderr,"browser: action %s\n", file);
	if (-1 == stat(file,&st)) {
	    fprintf(stderr,"stat %s: %s\n",file,strerror(errno));
	    return;
	}
	if (S_ISDIR(st.st_mode))
	    browser_cd(h,file);
	if (S_ISREG(st.st_mode))
	    new_file(file,1);
    }
}

static void
browser_bookmark_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct browser_handle *h = clientdata;
    browser_cd(h, cfg_get_str(O_BOOKMARKS, XtName(widget)));
}

/*----------------------------------------------------------------------*/

static void
browser_destroy(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct browser_handle *h = clientdata;

    if (debug)
	fprintf(stderr,"browser: destroy\n");
    browser_bgcancel(h);
    ptr_unregister(h->shell);
    free(h);
}

static char*
browser_fixpath(char *dir)
{
    char path[1024];
    char *s,*d;

    memset(path,0,sizeof(path));
    if (dir[0] == '/') {
	/* absolute */
	strncpy(path,dir,sizeof(path)-1);
    } else {
	/* relative */
	getcwd(path,sizeof(path)-1);
	if (strlen(path)+strlen(dir)+4 < sizeof(path)) {
	    strcat(path,"/");
	    strcat(path,dir);
	}
    }

    for (s = d = path; *s != 0;) {
	if (0 == strncmp(s,"//",2)) {
	    s++;
	    continue;
	}
	if (0 == strncmp(s,"/./",3)) {
	    s+=2;
	    continue;
	}
	if (0 == strcmp(s,"/"))
	    s++;
	if (0 == strcmp(s,"/."))
	    s+=2;
	*d = *s;
	s++, d++;
    }
    return strdup(path);
}

void
browser_window(char *dirname)
{
    Widget form,menubar,menu,push,clip;
    struct browser_handle *h;
    Arg args[8];
    char *list;
    int n = 0;

    h = malloc(sizeof(*h));
    if (NULL == h) {
	fprintf(stderr,"out of memory");
	return;
    }
    memset(h,0,sizeof(*h));
    INIT_LIST_HEAD(&h->files);
    h->dirname = browser_fixpath(dirname);

    h->shell = XtVaAppCreateShell("browser","Ida",
				  topLevelShellWidgetClass,
				  dpy,
				  XtNclientLeader,app_shell,
				  XmNdeleteResponse,XmDESTROY,
				  NULL);
    XmdRegisterEditres(h->shell);
    XtAddCallback(h->shell,XtNdestroyCallback,browser_destroy,h);

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
    XtAddCallback(h->container,XmNdefaultActionCallback,
		  browser_action_cb,h);

    XtAddCallback(h->scroll, XmNtraverseObscuredCallback,
		  container_traverse_cb, NULL);
    XtAddCallback(h->container,XmNconvertCallback,
		  container_convert_cb,h);

    XtVaGetValues(h->scroll,XmNclipWindow,&clip,NULL);
    XtAddEventHandler(clip,StructureNotifyMask,True,container_resize_eh,NULL);

    /* menu - file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("close",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,destroy_cb,h->shell);
    
    /* menu - edit */
    menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
    XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    container_menu_edit(menu,h->container, 0,1,0,0);

    /* menu - view */
    menu = XmCreatePulldownMenu(menubar,"viewM",NULL,0);
    XtVaCreateManagedWidget("view",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    container_menu_view(menu,h->container);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("filter",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,browser_filter,h);
    push = XtVaCreateManagedWidget("freset",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,browser_nofilter,h);
    
    /* menu - ops */
    menu = XmCreatePulldownMenu(menubar,"opsM",NULL,0);
    XtVaCreateManagedWidget("ops",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    container_menu_ops(menu,h->container);

    /* menu - dirs (bookmarks) */
    menu = XmCreatePulldownMenu(menubar,"dirsM",NULL,0);
    XtVaCreateManagedWidget("dirs",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    for (list  = cfg_entries_first(O_BOOKMARKS);
	 list != NULL;
	 list  = cfg_entries_next(O_BOOKMARKS,list)) {
	push = XtVaCreateManagedWidget(list,xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,browser_bookmark_cb,h);
    }
    
    /* read dir and show window */
    container_spatial_cb(NULL,h->container,NULL);
    browser_readdir(h);
    XtPopup(h->shell,XtGrabNone);
    ptr_register(h->shell);
}

void
browser_ac(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    if (*num_params > 0)
	browser_window(params[0]);
    else
	browser_window(".");
}
