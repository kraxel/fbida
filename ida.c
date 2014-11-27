#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <langinfo.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <Xm/Xm.h>
#include <Xm/Primitive.h>
#include <Xm/Label.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/Separator.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>
#include <Xm/Protocols.h>
#include <Xm/List.h>
#include <Xm/Form.h>
#include <Xm/MessageB.h>
#include <Xm/SelectioB.h>
#include <Xm/Scale.h>
#include <Xm/Text.h>
#include <Xm/FileSB.h>
#include <Xm/ToggleB.h>
#include <Xm/DrawingA.h>
#include <Xm/Transfer.h>
#include <Xm/TransferP.h>

#include "RegEdit.h"
#include "ida.h"
#include "x11.h"
#include "man.h"
#include "readers.h"
#include "writers.h"
#include "viewer.h"
#include "op.h"
#include "lut.h"
#include "filter.h"
#include "color.h"
#include "icons.h"
#include "browser.h"
#include "filelist.h"
#include "xdnd.h"
#include "selections.h"
#include "sane.h"
#include "curl.h"
#include "idaconfig.h"

/* ---------------------------------------------------------------------- */

static void popup_ac(Widget, XEvent*, String*, Cardinal*);
static void exit_ac(Widget, XEvent*, String*, Cardinal*);
static void next_ac(Widget, XEvent*, String*, Cardinal*);
static void prev_ac(Widget, XEvent*, String*, Cardinal*);
static void next_page_ac(Widget, XEvent*, String*, Cardinal*);
static void prev_page_ac(Widget, XEvent*, String*, Cardinal*);
static void zoom_ac(Widget, XEvent*, String*, Cardinal*);
static void scroll_ac(Widget, XEvent*, String*, Cardinal*);
static void debug_ac(Widget, XEvent*, String*, Cardinal*);
static void load_ac(Widget, XEvent*, String*, Cardinal*);
static void save_ac(Widget, XEvent*, String*, Cardinal*);
static void scan_ac(Widget, XEvent*, String*, Cardinal*);
static void print_ac(Widget, XEvent*, String*, Cardinal*);

static void undo_ac(Widget, XEvent*, String*, Cardinal*);
static void filter_ac(Widget, XEvent*, String*, Cardinal*);
static void gamma_ac(Widget, XEvent*, String*, Cardinal*);
static void bright_ac(Widget, XEvent*, String*, Cardinal*);
static void contrast_ac(Widget, XEvent*, String*, Cardinal*);
static void color_ac(Widget, XEvent*, String*, Cardinal*);
static void f3x3_ac(Widget, XEvent*, String*, Cardinal*);
static void resize_ac(Widget, XEvent*, String*, Cardinal*);
static void rotate_ac(Widget, XEvent*, String*, Cardinal*);
static void sharpe_ac(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "Exit",     exit_ac      },
    { "Next",     next_ac      },
    { "Prev",     prev_ac      },
    { "NextPage", next_page_ac },
    { "PrevPage", prev_page_ac },
    { "Zoom",     zoom_ac      },
    { "Scroll",   scroll_ac    },
    { "Debug",    debug_ac     },
    { "Popup",    popup_ac     },
    { "Man",      man_action   },
    { "Load",     load_ac      },
    { "Save",     save_ac      },
    { "Scan",     scan_ac      },
    { "Print",    print_ac     },
    { "Browser",  browser_ac   },
    { "Filelist", filelist_ac  },

    { "Undo",     undo_ac      },
    { "Filter",   filter_ac    },
    { "Gamma",    gamma_ac     },
    { "Bright",   bright_ac    },
    { "Contrast", contrast_ac  },
    { "Color",    color_ac     },
    { "F3x3",     f3x3_ac      },
    { "Resize",   resize_ac    },
    { "Rotate",   rotate_ac    },
    { "Sharpe",   sharpe_ac    },

    { "Ipc",      ipc_ac       },
    { "Xdnd",     XdndAction   },
};

/* ---------------------------------------------------------------------- */

XtAppContext       app_context;
Display            *dpy;
Widget             app_shell;
int                gray=0;
char               *binary;
struct ida_viewer  *ida;

/* ---------------------------------------------------------------------- */

struct ARGS args;
unsigned int pcd_res;
unsigned int sane_res;

XtResource args_desc[] = {
    {
	"debug",
	XtCBoolean, XtRBoolean, sizeof(Boolean),
	XtOffset(struct ARGS*,debug),
	XtRString, "false"
    },{
	"help",
	XtCBoolean, XtRBoolean, sizeof(Boolean),
	XtOffset(struct ARGS*,help),
	XtRString, "false"
    },{
	"testload",
	XtCBoolean, XtRBoolean, sizeof(Boolean),
	XtOffset(struct ARGS*,testload),
	XtRString, "false"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-d",          "debug",       XrmoptionNoArg,  "true" },
    { "-debug",      "debug",       XrmoptionNoArg,  "true" },
    { "-testload",   "testload",    XrmoptionNoArg,  "true" },
    { "-h",          "help",        XrmoptionNoArg,  "true" },
    { "-help",       "help",        XrmoptionNoArg,  "true" },
    { "--help",      "help",        XrmoptionNoArg,  "true" },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

static String fallback_ressources[] = {
#include "Ida.ad.h"
    NULL
};

/* ---------------------------------------------------------------------- */

static struct ida_writer *cwriter;
static char *save_filename;
static char *print_command;

static Widget control_shell,status;
static Atom   wm_delete_window;

static Widget view,loadbox,savebox,printbox;

/* file list */
static Widget wlist;
static char **files = NULL;
static int  cfile   = -1;
static int  nfiles  = 0;
static int  cpage   = 0;
static int  npages  = 1;

/* filter controls */
static int gamma_val    = 100;
static int bright_val   = 0;
static int contrast_val = 0;
static int rotate_val   = 0;
static int sharpe_val   = 10;

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         mapped;
} my_toplevels [] = {
    { "control",   &control_shell },
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

/* ---------------------------------------------------------------------- */

static void
popup_ac(Widget widget, XEvent *event,
	 String *params, Cardinal *num_params)
{
    unsigned int i;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (name=%s)\n",
		    params[0]);
	    return;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (%p:%s)\n",
		    widget,XtName(widget));
	    return;
	}
    }

    /* popup/down window */
    if (!my_toplevels[i].mapped) {
	XtPopup(*(my_toplevels[i].shell), XtGrabNone);
	my_toplevels[i].mapped = 1;
    } else {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    }
}

static void
popupdown_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int i = 0;
    popup_ac(clientdata, NULL, NULL, &i);
}

/* ---------------------------------------------------------------------- */

void
destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtDestroyWidget(clientdata);
}

void
action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *calls, *action, *argv[16]; /* max: F3x3(9 args) */
    int argc;

    calls = strdup(clientdata);
    action = strtok(calls,"(),");
    for (argc = 0; NULL != (argv[argc] = strtok(NULL,"(),")); argc++)
	/* nothing */;
    XtCallActionProc(widget,action,NULL,argv,argc);
    free(calls);
}

static void
about_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget msgbox;
    
    msgbox = XmCreateInformationDialog(app_shell,"aboutbox",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
    XtManageChild(msgbox);
}

#if 0
static void
sorry_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget msgbox;
    
    msgbox = XmCreateErrorDialog(app_shell,"sorrybox",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
    XtManageChild(msgbox);
}
#endif

void
debug_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    unsigned int i;

    fprintf(stderr,"Debug:");
    for (i = 0; i < *num; i++)
	fprintf(stderr," %s",params[i]);
    fprintf(stderr,"\n");
}

static void
display_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmDisplayCallbackStruct *arg = call_data;
    
    switch (arg->reason) {
    case XmCR_NO_RENDITION:
	fprintf(stderr,"display_cb: no rendition: \"%s\"\n",arg->tag);
	break;
    case XmCR_NO_FONT:
	fprintf(stderr,"display_cb: no font: \"%s\"\n",arg->font_name);
	break;
    default:
	/* should not happen */
	fprintf(stderr,"display_cb: unknown reason [%d]\n",arg->reason);
	break;
    }
}

/* ---------------------------------------------------------------------- */

struct ptr_list {
    struct ptr_list *next;
    Widget widget;
};
struct ptr_list *ptr_head;

void ptr_register(Widget widget)
{
    struct ptr_list *item;

    if (XtWindow(widget))
	XDefineCursor(XtDisplay(widget), XtWindow(widget),
		      ptrs[POINTER_NORMAL]);
    item = malloc(sizeof(*item));
    memset(item,0,sizeof(*item));
    item->widget = widget;
    item->next = ptr_head;
    ptr_head = item;
}

void ptr_unregister(Widget widget)
{
    struct ptr_list *item,*fitem;

    if (ptr_head->widget == widget) {
	fitem = ptr_head;
	ptr_head = ptr_head->next;
	free(fitem);
	return;
    }
    for (item = ptr_head; NULL != item->next; item = item->next) {
	if (item->next->widget == widget) {
	    fitem = item->next;
	    item->next = fitem->next;
	    free(fitem);
	    return;
	}
    }
    /* shouldn't happen */
    fprintf(stderr,"Oops: widget not found in list\n");
}

void ptr_busy(void)
{
    struct ptr_list *item;

    for (item = ptr_head; NULL != item; item = item->next) {
	if (!XtWindow(item->widget))
	    continue;
	XDefineCursor(XtDisplay(item->widget), XtWindow(item->widget),
		      ptrs[POINTER_BUSY]);
    }
    XSync(dpy,False);
}

void ptr_idle(void)
{
    struct ptr_list *item;

    for (item = ptr_head; NULL != item; item = item->next) {
	if (!XtWindow(item->widget))
	    continue;
	XDefineCursor(XtDisplay(item->widget), XtWindow(item->widget),
		      ptrs[POINTER_NORMAL]);
    }
}

/* ---------------------------------------------------------------------- */

static Boolean
exit_wp(XtPointer client_data)
{
    exit(0);
}

static void
exit_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    XtAppAddWorkProc(app_context,exit_wp, NULL);
    XtDestroyWidget(app_shell);
}

void
exit_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    exit_cb(widget,NULL,NULL);
}

/* ---------------------------------------------------------------------- */

static void
list_update(void)
{
    XmStringTable tab;
    int i;

    tab = malloc(nfiles * sizeof(XmString));
    for (i = 0; i < nfiles; i++)
	tab[i] = XmStringGenerate(files[i], NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(wlist,
		  XmNitems, tab,
		  XmNitemCount, nfiles,
		  NULL);
    for (i = 0; i < nfiles; i++)
	XmStringFree(tab[i]);
    free(tab);
}

static int
list_append(char *filename)
{
    int i;

    for (i = 0; i < nfiles; i++)
	if (0 == strcmp(files[i],filename))
	    return i;
    files = realloc(files,sizeof(char*)*(nfiles+1));
    files[nfiles] = strdup(filename);
    nfiles++;
    return nfiles-1;
}

static void
list_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    XmListCallbackStruct *list = calldata;

    if (0 == list->selected_item_count)
	return;
    cfile = list->selected_item_positions[0]-1;
    cpage = 0;
    npages = viewer_loadimage(ida,files[cfile],cpage);
    if (-1 == npages)
	return;
    resize_shell();
}

static void
pcd_set(Widget widget)
{
    WidgetList items;
    Cardinal nitems;
    unsigned int i;
    int value;

    value = GET_PHOTOCD_RES();
    XtVaGetValues(widget,XtNchildren,&items,
		  XtNnumChildren,&nitems,NULL);
    for (i = 0; i < nitems; i++)
	XmToggleButtonSetState(items[i],value == i+1,False);
    pcd_res      = value;
}

static void
pcd_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    cfg_set_int(O_PHOTOCD_RES,(intptr_t)client_data);
    pcd_set(XtParent(widget));
}

static void
cfg_bool_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *option = XtName(widget);
    Boolean value = XmToggleButtonGetState(widget);
    cfg_set_bool(O_OPTIONS, option, value);
}

static void
cfg_save_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    ida_write_config();
}

static void
create_control(void)
{
    Widget form,menubar,tool,menu,smenu,push;

    control_shell = XtVaAppCreateShell("ctrl","Iv",
				       topLevelShellWidgetClass,
				       dpy,
				       XtNclientLeader,app_shell,
				       XmNdeleteResponse,XmDO_NOTHING,
				       NULL);
    XmdRegisterEditres(control_shell);
    XmAddWMProtocolCallback(control_shell,wm_delete_window,
			    popupdown_cb,control_shell);

    /* widgets */
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, control_shell,
				   NULL);
    menubar = XmCreateMenuBar(form,"bar",NULL,0);
    XtManageChild(menubar);
    tool = XtVaCreateManagedWidget("tool",xmRowColumnWidgetClass, form,
				   NULL);
    status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, form,
				     NULL);
    wlist = XmCreateScrolledList(form,"list",NULL,0);
    XtManageChild(wlist);
    XtAddCallback(wlist,XmNdefaultActionCallback,list_cb,NULL);
    XtAddCallback(wlist,XmNdestinationCallback,selection_dest,NULL);
    dnd_add(wlist);
    
    /* menu - file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("load",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Load()");
    push = XtVaCreateManagedWidget("save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Save()");
    push = XtVaCreateManagedWidget("browse",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Browser()");
    push = XtVaCreateManagedWidget("filelist",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filelist()");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
#ifdef HAVE_LIBSANE
    sane_menu(menu);
#endif
    push = XtVaCreateManagedWidget("print",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Print()");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,exit_cb,NULL);

    /* menu - edit */
    menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
    XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("undo",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Undo()");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("copy",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ipc(copy)");
    push = XtVaCreateManagedWidget("paste",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ipc(paste)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("flipv",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(flip-vert)");
    push = XtVaCreateManagedWidget("fliph",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(flip-horz)");
    push = XtVaCreateManagedWidget("rotcw",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(rotate-cw)");
    push = XtVaCreateManagedWidget("rotccw",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(rotate-ccw)");
    push = XtVaCreateManagedWidget("invert",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(invert)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("crop",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(crop)");
    push = XtVaCreateManagedWidget("acrop",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(autocrop)");
    push = XtVaCreateManagedWidget("scale",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Resize()");
    push = XtVaCreateManagedWidget("rotany",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Rotate()");

    /* menu - filters / operations */
    menu = XmCreatePulldownMenu(menubar,"opM",NULL,0);
    XtVaCreateManagedWidget("op",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("gamma",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Gamma()");
    push = XtVaCreateManagedWidget("bright",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Bright()");
    push = XtVaCreateManagedWidget("contr",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Contrast()");
    push = XtVaCreateManagedWidget("color",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Color()");
    push = XtVaCreateManagedWidget("gray",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(grayscale)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("blur",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "F3x3(1,1,1, 1,1,1, 1,1,1, 1,9,0)");
    push = XtVaCreateManagedWidget("sharpe",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Sharpe()");
    push = XtVaCreateManagedWidget("edge",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "F3x3(-1,-1,-1, -1,8,-1, -1,-1,-1)");
    push = XtVaCreateManagedWidget("emboss",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "F3x3(1,0,0, 0,0,0, 0,0,-1, 0,0,128)");

    /* menu - view */
    menu = XmCreatePulldownMenu(menubar,"viewM",NULL,0);
    XtVaCreateManagedWidget("view",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Prev()");
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Next()");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("prevpage",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"PrevPage()");
    push = XtVaCreateManagedWidget("nextpage",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"NextPage()");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("zoomin",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Zoom(inc)");
    push = XtVaCreateManagedWidget("zoomout",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Zoom(dec)");

    /* menu - options */
    menu = XmCreatePulldownMenu(menubar,"optM",NULL,0);
    push = XtVaCreateManagedWidget("opt",xmCascadeButtonWidgetClass,menubar,
				   XmNsubMenuId,menu,NULL);
    smenu = XmCreatePulldownMenu(menu,"pcdM",NULL,0);
    XtVaCreateManagedWidget("pcd",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("1",xmToggleButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,pcd_cb,(XtPointer)1);
    push = XtVaCreateManagedWidget("2",xmToggleButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,pcd_cb,(XtPointer)2);
    push = XtVaCreateManagedWidget("3",xmToggleButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,pcd_cb,(XtPointer)3);
    push = XtVaCreateManagedWidget("4",xmToggleButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,pcd_cb,(XtPointer)4);
    push = XtVaCreateManagedWidget("5",xmToggleButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,pcd_cb,(XtPointer)5);
    pcd_set(smenu);

    push = XtVaCreateManagedWidget("autozoom",xmToggleButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNvalueChangedCallback,cfg_bool_cb,NULL);
    XmToggleButtonSetState(push,GET_AUTOZOOM(),False);

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("cfgsave",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,cfg_save_cb,NULL);
    
    /* menu - help */
    menu = XmCreatePulldownMenu(menubar,"helpM",NULL,0);
    push = XtVaCreateManagedWidget("help",xmCascadeButtonWidgetClass,menubar,
				   XmNsubMenuId,menu,NULL);
    XtVaSetValues(menubar,XmNmenuHelpWidget,push,NULL);
    push = XtVaCreateManagedWidget("man",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Man(ida)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("about",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,about_cb,NULL);

    /* toolbar */
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Prev()");
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Next()");
    push = XtVaCreateManagedWidget("zoomin",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Zoom(inc)");
    push = XtVaCreateManagedWidget("zoomout",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Zoom(dec)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("flipv",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(flip-vert)");
    push = XtVaCreateManagedWidget("fliph",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(flip-horz)");
    push = XtVaCreateManagedWidget("rotccw",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(rotate-ccw)");
    push = XtVaCreateManagedWidget("rotcw",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Filter(rotate-cw)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("exit",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,exit_cb,NULL);
}

/* ---------------------------------------------------------------------- */

void
resize_shell(void)
{
    char *title,*base;
    Dimension x,y,w,h,sw,sh;
    XmString str;
    int len;
    
    XtVaGetValues(app_shell, XtNx,&x, XtNy,&y, NULL);

    /* resize shell + move shell
       size: image size + 2*shadowThickness */
    w = ida->scrwidth+2;
    h = ida->scrheight+2;
    sw = XtScreen(ida->widget)->width;
    sh = XtScreen(ida->widget)->height;
    if (w > sw)
	w = sw;
    if (h > sh)
	h = sh;
    if (x+w > sw)
	x = sw-w;
    if (y+h > sh)
	y = sh-h;

    base = strrchr(ida->file,'/');
    if (base)
	base++;
    else
	base = ida->file;
    title = malloc(strlen(base)+128);
    len = sprintf(title,"%s (%ux%u", base,
		  ida->img.i.width, ida->img.i.height);
    if (ida->img.i.dpi)
	len += sprintf(title+len," | %u dpi",
		       ida->img.i.dpi);
    if (ida->img.i.npages > 1)
        len += sprintf(title+len," | page %d/%u",
		       cpage+1, ida->img.i.npages);
    len += sprintf(title+len," | %d%%)", viewer_i2s(ida->zoom,100));
    XtVaSetValues(app_shell, XtNtitle,title,
		  /* XtNx,x, XtNy,y, */ XtNwidth,w, XtNheight,h,
		  NULL);
    str = XmStringGenerate(title,NULL,XmMULTIBYTE_TEXT,NULL);
    XtVaSetValues(status,XmNlabelString,str,NULL);
    XmStringFree(str);
    free(title);
}

static int
load_file(int nr, int np)
{
    if(nr < 0 || nr >= nfiles)
        return -1;
    npages = viewer_loadimage(ida,files[nr],np);
    if (-1 == npages)
	return -1;
    resize_shell();
#if 0
    XmListSelectPos(wlist,nr+1,False);
    cfile = nr;
#endif
    return npages;
}

char*
load_tmpfile(char *base)
{
    char *tmpdir;
    char *filename;

    tmpdir = getenv("TMPDIR");
    if (NULL == tmpdir)
	tmpdir="/tmp";
    filename = malloc(strlen(tmpdir)+strlen(base)+16);
    sprintf(filename,"%s/%s-XXXXXX",tmpdir,base);
    return filename;
}

static void
load_logo(void)
{
    static unsigned char logo[] = {
#include "logo.h"
    };
    char *filename = load_tmpfile("ida-logo");
    int fd;
    fd = mkstemp(filename);
    write(fd,logo,sizeof(logo));
    close(fd);
    cpage = 0;
    npages = 1;
    if (0 < viewer_loadimage(ida,filename,cpage)) {
	ida->file = "ida " VERSION;
	resize_shell();
    }
    unlink(filename);
    free(filename);
}

static void
load_stdin(void)
{
    char *filename = load_tmpfile("ida-stdin");
    char buf[4096];
    int rc,fd;
    fd = mkstemp(filename);
    for (;;) {
	rc = read(0,buf,sizeof(buf));
	if (rc <= 0)
	    break;
	write(fd,buf,rc);
    }
    close(fd);
    cpage = 0;
    npages = 1;
    if (0 < viewer_loadimage(ida,filename,cpage)) {
	ida->file = "stdin";
	resize_shell();
    }
    unlink(filename);
    free(filename);
}

void
next_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    for (;;) {
	if (cfile >= nfiles-1)
	    return;
	cfile++;
        cpage = 0;
	if (0 <= load_file(cfile,cpage))
	    break;
    }
}

void
prev_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    for (;;) {
	if (cfile < 1)
	    return;
	cfile--;
        cpage = 0;
	if (0 <= load_file(cfile,cpage))
	    break;
    }
}

void
next_page_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    for (;;) {
	if (cpage >= npages-1)
	    return;
	cpage++;
	if (0 <= load_file(cfile,cpage))
	    break;
    }
}

void
prev_page_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    for (;;) {
	if (cpage <= 0)
	    return;
	cpage--;
	if (0 <= load_file(cfile,cpage))
	    break;
    }
}

void
zoom_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    int zoom;
    
    if (0 == *num)
	return;

    if (0 == strcasecmp(params[0],"auto")) {
	viewer_autozoom(ida);
	return;
    }

    if (0 == strcasecmp(params[0],"inc")) {
	zoom = ida->zoom+1;
    } else if (0 == strcasecmp(params[0],"dec")) {
	zoom = ida->zoom-1;
    } else {
	zoom = atoi(params[0]);
    }
    viewer_setzoom(ida,zoom);
    resize_shell();
}

void
scroll_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    fprintf(stderr,"Scroll(): %s\n",XtName(widget));
}

/* ---------------------------------------------------------------------- */

void new_file(char *name, int complain)
{
    struct stat st;
    int n;

    if (curl_is_url(name))
	goto load;

    if (0 == strncasecmp(name,"file:",5))
	name += 5;
    if (-1 == stat(name,&st)) {
	if (complain)
	    fprintf(stderr,"stat %s: %s\n",name,strerror(errno));
	return;
    }
    switch (st.st_mode & S_IFMT) {
    case S_IFDIR:
	browser_window(name);
	break;
    case S_IFREG:
	goto load;
	break;
    }
    return;
    
 load:
    n = list_append(name);
    list_update();
    cpage = 0;
    load_file(n,cpage);
}

static void
load_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    char *line;

    if (cb->reason == XmCR_OK) {
        line = XmStringUnparse(cb->value,NULL,
                               XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
                               NULL,0,0);
	new_file(line,1);
    }
    XtUnmanageChild(widget);
}

void
load_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    Widget help;

    if (NULL == loadbox) {
	loadbox = XmCreateFileSelectionDialog(app_shell,"load",NULL,0);
	help = XmFileSelectionBoxGetChild(loadbox,XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help);
	XtAddCallback(loadbox,XmNokCallback,load_done_cb,NULL);
	XtAddCallback(loadbox,XmNcancelCallback,load_done_cb,NULL);
    } else {
	XmFileSelectionDoSearch(loadbox,NULL);
    }
    XtManageChild(loadbox);
}

void
scan_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
#ifdef HAVE_LIBSANE
    cpage = 0;
    if (*num)
	npages = viewer_loader_start(ida, &sane_loader, NULL, params[0], 0);
    else
	npages = viewer_loader_start(ida, &sane_loader, NULL, "", 0);
    if (-1 == npages)
	return;
    ida->file = "scanned image";
    resize_shell();
#endif
}

/* ---------------------------------------------------------------------- */

void
do_save_print(void)
{
    FILE *fp;
    
    if (save_filename) {
	XtUnmanageChild(savebox);
	ptr_busy();
	if (NULL == (fp = fopen(save_filename,"wb"))) {
	    fprintf(stderr,"save: can't open %s: %s\n",
		    save_filename,strerror(errno));
	} else if (-1 == cwriter->write(fp,&ida->img)) {
	    fclose(fp);
	    fprintf(stderr,"saving %s FAILED",save_filename);
	} else {
	    fclose(fp);
	    list_append(save_filename);
	    list_update();
	}
	ptr_idle();
    }
    if (print_command) {
	XtUnmanageChild(printbox);
	ptr_busy();
	if (NULL == (fp = popen(print_command,"w"))) {
	    fprintf(stderr,"print: can't exec %s: %s\n",
		    print_command,strerror(errno));
	} else {
	    if (-1 == cwriter->write(fp,&ida->img))
		fprintf(stderr,"printing FAILED");
	    fclose(fp);
	}
	ptr_idle();
    }
}

static void
save_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;

    if (cb->reason == XmCR_OK) {
	print_command = NULL;
	save_filename = XmStringUnparse(cb->value,NULL,
					XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
					NULL,0,0);
	if (cwriter->conf) {
	    cwriter->conf(widget,&ida->img);
	} else {
	    do_save_print();
	}
    } else {
	XtUnmanageChild(widget);
    }
}

static void
save_fmt_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    cwriter = clientdata;
}

static void
save_ext_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget option = clientdata;
    Widget menu;
    WidgetList children;
    Cardinal nchildren;
    struct ida_writer *wr = NULL;
    struct list_head *item;
    char *name,*ext;
    int i,j,pick;

    name = XmTextGetString(widget);
    ext = strrchr(name,'.');
    if (NULL == ext)
	return;
    if (strchr(ext,'/'))
	return;
    ext++;

    i = 0; pick = -1;
    list_for_each(item,&writers) {
	wr = list_entry(item, struct ida_writer, list);
	for (j = 0; NULL != wr->ext[j]; j++)
	    if (0 == strcasecmp(ext,wr->ext[j]))
		pick = i;
	if (-1 != pick)
	    break;
	i++;
    }
    if (-1 == pick)
	return;

    XtVaGetValues(option,XmNsubMenuId,&menu,NULL);
    XtVaGetValues(menu,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    XtVaSetValues(option,XmNmenuHistory,children[pick],NULL);
    cwriter = wr;
}

static void
save_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    Widget help,menu,option,push,text;
    Arg args[2];
    struct ida_writer *wr = NULL;
    struct list_head *item;

    if (NULL == savebox) {
	savebox = XmCreateFileSelectionDialog(app_shell,"save",NULL,0);
	help = XmFileSelectionBoxGetChild(savebox,XmDIALOG_HELP_BUTTON);
	text = XmFileSelectionBoxGetChild(savebox,XmDIALOG_TEXT);
	XtUnmanageChild(help);

	menu = XmCreatePulldownMenu(savebox,"formatM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,menu);
	option = XmCreateOptionMenu(savebox,"format",args,1);
	XtManageChild(option);
	list_for_each(item,&writers) {
	    wr = list_entry(item, struct ida_writer, list);
	    push = XtVaCreateManagedWidget(wr->label,
					   xmPushButtonWidgetClass,menu,
					   NULL);
	    XtAddCallback(push,XmNactivateCallback,save_fmt_cb,wr);
	}
	cwriter = list_entry(writers.next, struct ida_writer, list);

	XtAddCallback(text,XmNvalueChangedCallback,save_ext_cb,option);
	XtAddCallback(savebox,XmNokCallback,save_done_cb,NULL);
	XtAddCallback(savebox,XmNcancelCallback,save_done_cb,NULL);
    } else {
	XmFileSelectionDoSearch(savebox,NULL);
    }
    XtManageChild(savebox);
}

/* ---------------------------------------------------------------------- */

static void
print_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = call_data;

    if (cb->reason == XmCR_OK) {
	save_filename = NULL;
	print_command = XmStringUnparse(cb->value,NULL,
					XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
					NULL,0,0);
	cwriter = &ps_writer;
	cwriter->conf(widget,&ida->img);
    } else {
	XtUnmanageChild(widget);
    }
}

static void
print_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    if (NULL == printbox) {
	printbox = XmCreatePromptDialog(app_shell,"print",NULL,0);
	XtUnmanageChild(XmSelectionBoxGetChild(printbox,XmDIALOG_HELP_BUTTON));
	XtAddCallback(printbox,XmNokCallback,print_done_cb,NULL);
	XtAddCallback(printbox,XmNcancelCallback,print_done_cb,NULL);
    }
    XtManageChild(printbox);
}

/* ---------------------------------------------------------------------- */

static struct ida_op *ops[] = {
    &desc_flip_vert,
    &desc_flip_horz,
    &desc_rotate_cw,
    &desc_rotate_ccw,
    &desc_invert,
    &desc_crop,
    &desc_autocrop,
    &desc_grayscale,
    NULL
};

void
filter_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    struct ida_op *op = NULL;
    int i;

    if (*num < 1)
	return;
    for (i = 0; NULL != ops[i]; i++) {
	op = ops[i];
	if (0 == strcasecmp(op->name,params[0]))
	    break;
    }
    if (NULL == ops[i]) {
	fprintf(stderr,"Oops: unknown filter: %s\n",params[0]);
	return;
    }

    viewer_start_op(ida,op,NULL);
    if (ida->op_src.i.width  != ida->img.i.width ||
	ida->op_src.i.height != ida->img.i.height)
	resize_shell();
}

void
f3x3_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    struct op_3x3_parm p;

    if (*num < 9) {
	fprintf(stderr,"F3x3: wrong number of args (%d, need 9)\n",*num);
	return;
    }
    memset(&p,0,sizeof(p));
    p.f1[0] = atoi(params[0]);
    p.f1[1] = atoi(params[1]);
    p.f1[2] = atoi(params[2]);
    p.f2[0] = atoi(params[3]);
    p.f2[1] = atoi(params[4]);
    p.f2[2] = atoi(params[5]);
    p.f3[0] = atoi(params[6]);
    p.f3[1] = atoi(params[7]);
    p.f3[2] = atoi(params[8]);
    if (*num >  9) p.mul = atoi(params[ 9]);
    if (*num > 10) p.div = atoi(params[10]);
    if (*num > 11) p.add = atoi(params[11]);
    if (debug) {
	fprintf(stderr,"f3x3: -----------\n");
	fprintf(stderr,"f3x3: %3d %3d %3d\n",p.f1[0],p.f1[1],p.f1[2]);
	fprintf(stderr,"f3x3: %3d %3d %3d\n",p.f2[0],p.f2[1],p.f2[2]);
	fprintf(stderr,"f3x3: %3d %3d %3d\n",p.f3[0],p.f3[1],p.f3[2]);
	fprintf(stderr,"f3x3: *%d/%d+%d\n",p.mul,p.div,p.add);
    }
    viewer_start_op(ida,&desc_3x3,&p);
}

void
undo_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    Widget msgbox;
    int resize;

    resize = (ida->undo.i.width  != ida->img.i.width ||
	      ida->undo.i.height != ida->img.i.height);
    if (-1 == viewer_undo(ida)) {
	msgbox = XmCreateInformationDialog(app_shell,"noundobox",NULL,0);
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
	XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
	XtManageChild(msgbox);
    } else {
	if (resize)
	    resize_shell();
    }
}

/* ---------------------------------------------------------------------- */

struct ida_prompt {
    Widget shell;
    Widget box;
    Widget scale;
    Widget text;
    int apply;
    int value;
    int decimal;
    int factor;    /* 10^decimal */
    void (*notify)(int value, int preview);
};

static void
prompt_setvalue(struct ida_prompt *me, int value, int scale, int text)
{
    char str[32];
    int min,max;
    
    if (me->value == value)
	return;
    XtVaGetValues(me->scale,XmNminimum,&min,XmNmaximum,&max,NULL);
    if (value < min || value > max)
	return;

    me->value = value;
    if (scale)
	XmScaleSetValue(me->scale,value);
    if (text) {
	if (me->decimal) {
	    sprintf(str,"%*.*f",me->decimal+2,me->decimal,
		    (float)value/me->factor);
	} else {
	    sprintf(str,"%d",value);
	}
	XmTextSetString(me->text,str);
    }
    if (me->notify)
	me->notify(value,1);
}

static void
prompt_scale_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_prompt *me = client_data;
    XmScaleCallbackStruct *cd = calldata;

    prompt_setvalue(me,cd->value,0,1);
}

static void
prompt_text_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_prompt *me = client_data;
    float fvalue;
    int value;

    if (me->decimal) {
	fvalue  = atof(XmTextGetString(me->text));
	fvalue += 0.5/me->factor;
	value = (int)(fvalue * me->factor);
    } else {
	value = atoi(XmTextGetString(me->text));
    }
    prompt_setvalue(me,value,1,0);
}

static void
prompt_box_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_prompt *me = client_data;
    XmSelectionBoxCallbackStruct *cd = calldata;

    if (XmCR_OK == cd->reason)
	me->apply = 1;
    XtDestroyWidget(me->shell);
}

static void
prompt_shell_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_prompt *me = client_data;

    if (me->apply)
	me->notify(me->value,0);
    else
	viewer_cancel_preview(ida);
    free(me);
}

static void
prompt_init(char *name, int decimal, int value,
	    void (*notify)(int value, int preview))
{
    struct ida_prompt *me;

    me = malloc(sizeof(*me));
    memset(me,0,sizeof(*me));
    if (decimal) {
	int i;
	me->decimal = decimal;
	me->factor = 1;
	for (i = 0; i < decimal; i++)
	    me->factor *= 10;
    }
    me->notify = notify;
    
    me->box = XmCreatePromptDialog(app_shell,name,NULL,0);
    me->shell = XtParent(me->box);
    me->text = XmSelectionBoxGetChild(me->box,XmDIALOG_TEXT);
    XmdRegisterEditres(XtParent(me->box));
    XtUnmanageChild(XmSelectionBoxGetChild(me->box,XmDIALOG_HELP_BUTTON));
    me->scale = XtVaCreateManagedWidget("scale",xmScaleWidgetClass,
					me->box,NULL);

    XtAddCallback(me->scale,XmNdragCallback,prompt_scale_cb,me);
    XtAddCallback(me->scale,XmNvalueChangedCallback,prompt_scale_cb,me);
    XtAddCallback(me->text,XmNvalueChangedCallback,prompt_text_cb,me);
    XtAddCallback(me->box,XmNokCallback,prompt_box_cb,me);
    XtAddCallback(me->box,XmNcancelCallback,prompt_box_cb,me);
    XtAddCallback(me->shell,XmNdestroyCallback,prompt_shell_cb,me);
    
    XtManageChild(me->box);
    prompt_setvalue(me,value,1,1);
}

/* ---------------------------------------------------------------------- */

static void
gamma_notify(int value, int preview)
{
    struct op_map_parm param;
    float gamma = (float)value/100;

    param.red = op_map_nothing;
    param.red.gamma   = gamma;
    param.green = param.red;
    param.blue  = param.red;
    if (preview) {
	viewer_start_preview(ida,&desc_map,&param);
    } else {
	gamma_val = value;
	viewer_start_op(ida,&desc_map,&param);
    }
}

void
gamma_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    prompt_init("gamma",2,gamma_val,gamma_notify);
}

static void
bright_notify(int value, int preview)
{
    struct op_map_parm param;

    param.red = op_map_nothing;
    param.red.bottom += value;
    param.red.top    += value;
    param.green = param.red;
    param.blue  = param.red;
    if (preview) {
	viewer_start_preview(ida,&desc_map,&param);
    } else {
	bright_val = value;
	viewer_start_op(ida,&desc_map,&param);
    }
}

void
bright_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    prompt_init("bright",0,bright_val,bright_notify);
}

static void
contrast_notify(int value, int preview)
{
    struct op_map_parm param;

    param.red = op_map_nothing;
    param.red.bottom -= value;
    param.red.top    += value;
    param.green = param.red;
    param.blue  = param.red;
    if (preview) {
	viewer_start_preview(ida,&desc_map,&param);
    } else {
	contrast_val = value;
	viewer_start_op(ida,&desc_map,&param);
    }
}

void
contrast_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    prompt_init("contrast",0,contrast_val,contrast_notify);
}

static void
rotate_notify(int value, int preview)
{
    struct op_rotate_parm parm;

    parm.angle = value;
    if (preview) {
	viewer_start_preview(ida,&desc_rotate,&parm);
    } else {
	rotate_val = value;
	viewer_start_op(ida,&desc_rotate,&parm);
    }
}

void
rotate_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    prompt_init("rotate",0,rotate_val,rotate_notify);
}

static void
sharpe_notify(int value, int preview)
{
    struct op_sharpe_parm parm;

    parm.factor = value;
    if (preview) {
	viewer_start_preview(ida,&desc_sharpe,&parm);
    } else {
	sharpe_val = value;
	viewer_start_op(ida,&desc_sharpe,&parm);
    }
}

void
sharpe_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    prompt_init("sharpe",0,sharpe_val,sharpe_notify);
}

void
color_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    color_init(&ida->img);
}

/* ---------------------------------------------------------------------- */

struct ida_resize {
    Widget dlg,tx,ty,tr,lock,size,res,label;
    int yupdate,xupdate,rupdate;
    int apply;
};

static void
resize_phys_size(struct ida_resize *h)
{
    char buf[128];
    XmString str;
    int dpi;
    float x,y;

    dpi = atoi(XmTextGetString(h->tr));
    if (dpi) {
	x = (float)atoi(XmTextGetString(h->tx)) / dpi;
	y = (float)atoi(XmTextGetString(h->ty)) / dpi;
	sprintf(buf,"%.2f x %.2f inch\n%.2f x %.2f cm",
		x,y, x*2.54, y*2.54);
    } else {
	strcpy(buf,"unknown");
    }
    str = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT,NULL);
    XtVaSetValues(h->label,XmNlabelString,str,NULL);
}

static void
resize_sync_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_resize *h = client_data;
    char buf[32];
    int i,lock,res;

    lock = XmToggleButtonGetState(h->lock);
    res  = XmToggleButtonGetState(h->res);

    /* update text fields */
    if (h->tx == widget) {
	if (h->xupdate) {
	    h->xupdate--;
	    return;
	}
	i = atoi(XmTextGetString(h->tx));
	if (lock) {
	    sprintf(buf,"%d",i * ida->img.i.height / ida->img.i.width);
	    h->yupdate++;
	    XmTextSetString(h->ty,buf);
	    if (res) {
		sprintf(buf,"%d", ida->img.i.dpi * i / ida->img.i.width);
		h->rupdate++;
		XmTextSetString(h->tr,buf);
	    }
	} else {
	    if (res) {
		h->rupdate++;
		XmTextSetString(h->tr,"0");
	    }
	}
	resize_phys_size(h);
    }
    if (h->ty == widget) {
	if (h->yupdate) {
	    h->yupdate--;
	    return;
	}
	i = atoi(XmTextGetString(h->ty));
	if (lock) {
	    sprintf(buf,"%d",i * ida->img.i.width / ida->img.i.height);
	    h->xupdate++;
	    XmTextSetString(h->tx,buf);
	    if (res) {
		sprintf(buf,"%d", ida->img.i.dpi * i / ida->img.i.height);
		h->rupdate++;
		XmTextSetString(h->tr,buf);
	    }
	} else {
	    if (res) {
		h->rupdate++;
		XmTextSetString(h->tr,"0");
	    }
	}
	resize_phys_size(h);
    }
    if (h->tr == widget) {
	if (h->rupdate) {
	    h->rupdate--;
	    return;
	}
	i = atoi(XmTextGetString(h->tr));
	sprintf(buf,"%d", ida->img.i.width * i / ida->img.i.dpi);
	h->xupdate++;
	XmTextSetString(h->tx,buf);
	sprintf(buf,"%d", ida->img.i.height * i / ida->img.i.dpi);
	h->yupdate++;
	XmTextSetString(h->ty,buf);
	resize_phys_size(h);
    }

    /* radio buttons pressed */
    if (h->size == widget && XmToggleButtonGetState(h->size)) {
	XmToggleButtonSetState(h->res,0,False);
	sprintf(buf,"%u", ida->img.i.dpi);
	h->rupdate++;
	XmTextSetString(h->tr,buf);
	XtVaSetValues(h->tr,XmNsensitive,False,NULL);
	resize_phys_size(h);
    }
    if (h->res == widget && XmToggleButtonGetState(h->res)) {
	XmToggleButtonSetState(h->size,0,False);
	XtVaSetValues(h->tr,XmNsensitive,True,NULL);
    }
}

static void
resize_button_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_resize *h = client_data;
    XmSelectionBoxCallbackStruct *cb = calldata;

    if (cb->reason == XmCR_OK)
	h->apply = 1;
    XtDestroyWidget(XtParent(h->dlg));
}

static void
resize_destroy(Widget widget, XtPointer client_data, XtPointer calldata)
{
    struct ida_resize *h = client_data;
    struct op_resize_parm param;

    if (!h->apply)
	return;
    param.width  = atoi(XmTextGetString(h->tx));
    param.height = atoi(XmTextGetString(h->ty));
    param.dpi    = atoi(XmTextGetString(h->tr));
    if (0 == param.width  ||
	0 == param.height) {
	fprintf(stderr,"resize: invalid argument\n");
	return;
    }
	
    viewer_start_op(ida,&desc_resize,&param);
    resize_shell();
    free(h);
}

static void
resize_ac(Widget widget, XEvent *event, String *params, Cardinal *num)
{
    Widget rc,rc2;
    char buf[32];
    struct ida_resize *h;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    
    h->dlg = XmCreatePromptDialog(app_shell,"resize",NULL,0);
    XmdRegisterEditres(XtParent(h->dlg));
    XtUnmanageChild(XmSelectionBoxGetChild(h->dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(h->dlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(h->dlg,XmDIALOG_TEXT));
    rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass,h->dlg, NULL);
    XtVaCreateManagedWidget("lx", xmLabelWidgetClass,rc, NULL);
    h->tx = XtVaCreateManagedWidget("tx", xmTextWidgetClass,rc, NULL);
    XtVaCreateManagedWidget("ly", xmLabelWidgetClass,rc, NULL);
    h->ty = XtVaCreateManagedWidget("ty", xmTextWidgetClass,rc, NULL);
    XtVaCreateManagedWidget("lr", xmLabelWidgetClass,rc, NULL);
    h->tr = XtVaCreateManagedWidget("tr", xmTextWidgetClass,rc, NULL);
    h->lock = XtVaCreateManagedWidget("lock", xmToggleButtonWidgetClass,
				      rc, NULL);
    rc2 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass,rc, NULL);
    h->size = XtVaCreateManagedWidget("size", xmToggleButtonWidgetClass,
				      rc2, NULL);
    h->res  = XtVaCreateManagedWidget("res", xmToggleButtonWidgetClass,
				      rc2, NULL);
    XtVaCreateManagedWidget("phys", xmLabelWidgetClass,rc,NULL);
    h->label = XtVaCreateManagedWidget("label", xmLabelWidgetClass,
				       rc, NULL);

    sprintf(buf,"%u",ida->img.i.width);
    XmTextSetString(h->tx,buf);
    sprintf(buf,"%u",ida->img.i.height);
    XmTextSetString(h->ty,buf);
    sprintf(buf,"%u",ida->img.i.dpi);
    XmTextSetString(h->tr,buf);
    XtVaSetValues(h->tr,XmNsensitive,False,NULL);
    XmToggleButtonSetState(h->lock,1,False);
    XmToggleButtonSetState(h->size,1,False);
    XmToggleButtonSetState(h->res,0,False);
    if (!ida->img.i.dpi) {
    	XtVaSetValues(h->size,XmNsensitive,False,NULL);
	XtVaSetValues(h->res, XmNsensitive,False,NULL);
    }
    resize_phys_size(h);
    
    XtAddCallback(XtParent(h->dlg),XmNdestroyCallback,resize_destroy,h);
    XtAddCallback(h->dlg, XmNokCallback,           resize_button_cb, h);
    XtAddCallback(h->dlg, XmNcancelCallback,       resize_button_cb, h);
    XtAddCallback(h->tx,  XmNvalueChangedCallback, resize_sync_cb,   h);
    XtAddCallback(h->ty,  XmNvalueChangedCallback, resize_sync_cb,   h);
    XtAddCallback(h->tr,  XmNvalueChangedCallback, resize_sync_cb,   h);
    XtAddCallback(h->size,XmNvalueChangedCallback, resize_sync_cb,   h);
    XtAddCallback(h->res, XmNvalueChangedCallback, resize_sync_cb,   h);
    XtManageChild(h->dlg);
}

/* ---------------------------------------------------------------------- */

struct stderr_handler {
    Widget box;
    XmString str;
    int pipe,err;
    XtInputId id;
};

static void
stderr_input(XtPointer clientdata, int *src, XtInputId *id)
{
    struct stderr_handler *h = clientdata;
    XmString item;
    Widget label;
    char buf[1024];
    int rc;

    rc = read(h->pipe,buf,sizeof(buf)-1);
    if (rc <= 0) {
	/* Oops */
	XtRemoveInput(h->id);
	close(h->pipe);
	XtDestroyWidget(h->box);
	free(h);
    }
    buf[rc] = 0;
    write(h->err,buf,rc);
    item = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT,NULL);
    h->str = XmStringConcatAndFree(h->str,item);
    label = XmMessageBoxGetChild(h->box,XmDIALOG_MESSAGE_LABEL);
    XtVaSetValues(label,XmNlabelString,h->str,NULL);
    XtManageChild(h->box);
}

static void
stderr_ok_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    XtUnmanageChild(h->box);
}

static void
stderr_close_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
}

static void
stderr_init(void)
{
    struct stderr_handler *h;
    int p[2];

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    h->box = XmCreateErrorDialog(app_shell,"errbox",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(h->box,XmNokCallback,stderr_ok_cb,h);
    XtAddCallback(XtParent(h->box),XmNpopdownCallback,stderr_close_cb,h);
    XSync(XtDisplay(app_shell),False);
    if (!debug) {
	pipe(p);
	h->err = dup(2);
	dup2(p[1],2);
	close(p[1]);
	h->pipe = p[0];
	h->id = XtAppAddInput(app_context,h->pipe,(XtPointer)XtInputReadMask,
			      stderr_input,h);
    }
}

/* ---------------------------------------------------------------------- */

static void
create_mainwindow(void)
{
    Widget img;

    XmdRegisterEditres(app_shell);
    view = XmCreateScrolledWindow(app_shell,"view",NULL,0);
    XtManageChild(view);
    img = XtVaCreateManagedWidget("image", xmDrawingAreaWidgetClass,view,NULL);
    XtAddCallback(img,XmNdestinationCallback,selection_dest,NULL);
    XtAddCallback(img,XmNconvertCallback,selection_convert,NULL);
    dnd_add(img);
    ida = viewer_init(img);
    XtInstallAllAccelerators(img,app_shell);
}

static void
usage(void)
{
    fprintf(stderr,
	    "ida " VERSION " - image viewer & editor\n"
	    "usage: ida [ options ] [ files ]\n"
	    "options:\n"
	    "   -h, -help    this text\n"
	    "       -pcd n   pick PhotoCD size (n = 1 .. 5, default 3)\n"
	    "   -d, -debug   enable debug messages\n");
    exit(0);
}

int
main(int argc, char *argv[])
{
    int i, files, zero = 0;
    struct stat st;
    Pixel background;

#if 0
    setlocale(LC_ALL,"");
    if (0 == strcasecmp("utf-8", nl_langinfo(CODESET))) {
	/* ### FIXME ###
	 * for not-yet known reasons ida crashes somewhere deep in
	 * the Motif libraries when running in utf-8 locale ... */
	setenv("LC_ALL", "POSIX", 1);
	setlocale(LC_ALL,"");
    }
#endif

    binary = argv[0];
    ida_init_config();
    ida_read_config();

    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtAppInitialize(&app_context, "Ida",
				opt_desc, opt_count,
				&argc, argv,
				fallback_ressources,
				NULL, 0);
    dpy = XtDisplay(app_shell);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    pcd_res  = GET_PHOTOCD_RES();
    sane_res = GET_SANE_RES();
    if (args.help)
	usage();
    if (args.debug) {
	debug=1;
	xdnd_debug = 1;
	XSynchronize(dpy,1);
    }

    XtAppAddActions(app_context, actionTable,
		    sizeof(actionTable) / sizeof(XtActionsRec));
    if (0) {
	XtAddCallback(XmGetXmDisplay(dpy),XmNnoFontCallback,
		      display_cb,NULL);
	XtAddCallback(XmGetXmDisplay(dpy),XmNnoRenditionCallback,
		      display_cb,NULL);
    }
    XtVaGetValues(app_shell, XtNbackground,&background, NULL);
    x11_color_init(app_shell,&gray);
    x11_icons_init(dpy, background /* x11_gray */);
    stderr_init();
    ipc_init();

    wm_delete_window = XInternAtom(dpy,"WM_DELETE_WINDOW",False);
    create_mainwindow();
    create_control();
    XtRealizeWidget(app_shell);
    ptr_register(ida->widget);
    ptr_register(control_shell);

    /* handle cmd line args */
    if (2 == argc && 0 == strcmp(argv[1],"-")) {
	load_stdin();
    } else if (argc > 1) {
	for (files = 0, i = 1; i < argc; i++) {
 	    if (curl_is_url(argv[i])) {
		list_append(argv[i]);
		files++;
		continue;
	    }
	    if (-1 == stat(argv[i],&st)) {
		if (debug)
		    fprintf(stderr,"stat %s: %s\n",argv[i],strerror(errno));
		continue;
	    }
	    switch (st.st_mode & S_IFMT) {
	    case S_IFDIR:
		browser_window(argv[i]);
		break;
	    case S_IFREG:
		list_append(argv[i]);
		files++;
		break;
	    }
	}
	if (files) {
	    list_update();
	    next_ac(ida->widget,NULL,NULL,&zero);
	}
    }

    if (NULL == ida->file)
	load_logo();

    XtAppMainLoop(app_context);
    return 0; /* keep compiler happy */
}
