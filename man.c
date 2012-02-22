/*
 * motif-based man page renderer
 * (c) 2001 Gerd Hoffmann <gerd@kraxel.org>
 *
 */

#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ScrolledW.h>
#include <Xm/SelectioB.h>

#include "RegEdit.h"
#include "man.h"

extern Display *dpy;
extern Widget app_shell;

/*----------------------------------------------------------------------*/

#define MAN_UNDEF      0
#define MAN_NORMAL     1
#define MAN_BOLD       2
#define MAN_UNDERLINE  3

static XmStringTag man_tags[] = {
    NULL, NULL, "bold", "underline"
};

static void
man_destroy(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtDestroyWidget(clientdata);
}

void
man(char *page)
{
    Widget dlg,view,label;
    XmString xmpage,xmchunk;
    char line[1024],chunk[256];
    int s,d,cur,last;
    FILE *fp;

    /* build dialog */
    dlg = XmCreatePromptDialog(app_shell,"man",NULL,0);
    XmdRegisterEditres(XtParent(dlg));
    XtUnmanageChild(XmSelectionBoxGetChild(dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(dlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(dlg,XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(dlg,XmDIALOG_TEXT));
    XtAddCallback(dlg,XmNokCallback,man_destroy,dlg);
    view = XmCreateScrolledWindow(dlg,"view",NULL,0);
    XtManageChild(view);
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass,view, NULL);
    XtManageChild(dlg);

    /* fetch page and render into XmString */
    sprintf(line,"man %s 2>/dev/null",page);
    fp = popen(line,"r");
    xmpage = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT, NULL);
    while (NULL != fgets(line,sizeof(line)-1,fp)) {
	last = MAN_UNDEF;
	for (s = 0, d = 0; line[s] != '\0';) {
	    /* check current char */
	    cur = MAN_NORMAL;
	    if (line[s+1] == '\010' && line[s] == line[s+2])
		cur = MAN_BOLD;
	    if (line[s] == '_' && line[s+1] == '\010')
		cur = MAN_UNDERLINE;
	    /* add chunk if completed */
	    if (MAN_UNDEF != last  &&  cur != last) {
	        xmchunk = XmStringGenerate(chunk,NULL,XmMULTIBYTE_TEXT,
					   man_tags[last]);
		xmpage  = XmStringConcatAndFree(xmpage,xmchunk);
		d = 0;
	    }
	    /* add char to chunk */
	    switch (cur) {
	    case MAN_BOLD:
	    case MAN_UNDERLINE:
		s += 2;
	    case MAN_NORMAL:
		chunk[d++] = line[s++];
		break;
	    }
	    chunk[d] = '\0';
	    last = cur;
	}
	/* add last chunk for line */
	xmchunk = XmStringGenerate(chunk, NULL, XmMULTIBYTE_TEXT,
				   man_tags[last]);
	xmpage  = XmStringConcatAndFree(xmpage,xmchunk);
    }
    XtVaSetValues(label,XmNlabelString,xmpage,NULL);
    XmStringFree(xmpage);
    fclose(fp);
}

void
man_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *page = clientdata;
    man(page);
}

void
man_action(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    if (*num_params > 0)
	man(params[0]);
}
