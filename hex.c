/*
 * file viewer (hex dump)
 * quick & dirty for now, handling xxl files doesn't work very well ...
 *
 * (c) 2001 Gerd Hoffmann <kraxel@bytesex.org>
 *
 */

#include <stdio.h>
#include <ctype.h>

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
#include "hex.h"

extern Display *dpy;
extern Widget app_shell;

/*----------------------------------------------------------------------*/

static Widget hex_dlg, hex_label;

static void
hex_destroy(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtDestroyWidget(hex_dlg);
    hex_dlg = NULL;
}

static XmString
hexify(int addr,unsigned char *data, int bytes)
{
    XmString xmpage,xmchunk;
    char line[120];
    int len,y,i;
    
    xmpage = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT, NULL);
    for (y = 0; y < bytes; y += 16) {
	/* hexify line */
	len = sprintf(line,"%04x  ",addr+y);
	for (i = 0; i < 16; i++) {
	    if (y+i < bytes)
		len += sprintf(line+len,"%02x ",data[y+i]);
	    else
		len += sprintf(line+len,"   ");
	    if (3 == i%4)
		len += sprintf(line+len," ");
	}
	for (i = 0; i < 16; i++) {
	    if (y+i < bytes)
		if (isprint(data[y+i]))
		    len += sprintf(line+len,"%c",data[y+i]);
		else
		    len += sprintf(line+len,".");
	    else
		len += sprintf(line+len," ");
	}
	len += sprintf(line+len,"\n");
	/* add last chunk for line */
	xmchunk = XmStringGenerate(line, NULL, XmMULTIBYTE_TEXT, NULL);
	xmpage  = XmStringConcatAndFree(xmpage, xmchunk);
    }
    return xmpage;
}

/* guess whenever we have ascii or binary data */
static int
is_binary(unsigned char *data, int bytes)
{
    int i, bin;

    bin = 0;
    for (i = 0; i < 64 && i < bytes; i++) {
	if (!isprint(data[i]) &&
	    data[i] != '\t' &&
	    data[i] != '\n')
	    bin = 1;
    }
    return bin;
}

void
hex_display(char *filename)
{
    Widget view;
    XmString xmpage,chunk;
    unsigned char data[32768+1];
    int chars;
    FILE *fp;

    /* build dialog */
    if (!hex_dlg) {
	hex_dlg = XmCreatePromptDialog(app_shell,"hex",NULL,0);
	XmdRegisterEditres(XtParent(hex_dlg));
	XtUnmanageChild(XmSelectionBoxGetChild(hex_dlg,
					       XmDIALOG_SELECTION_LABEL));
	XtUnmanageChild(XmSelectionBoxGetChild(hex_dlg,XmDIALOG_HELP_BUTTON));
	XtUnmanageChild(XmSelectionBoxGetChild(hex_dlg,
					       XmDIALOG_CANCEL_BUTTON));
	XtUnmanageChild(XmSelectionBoxGetChild(hex_dlg,XmDIALOG_TEXT));
	XtAddCallback(hex_dlg,XmNokCallback,hex_destroy,NULL);
	view = XmCreateScrolledWindow(hex_dlg,"view",NULL,0);
	XtManageChild(view);
	hex_label = XtVaCreateManagedWidget("label", xmLabelWidgetClass,
					    view, NULL);
    }
    XtManageChild(hex_dlg);

    /* read the file (32k max) */
    fp = fopen(filename,"r");
    memset(data,0,sizeof(data));
    chars = fread(data, 1, sizeof(data)-1, fp);
    if (is_binary(data,chars)) {
	xmpage = hexify(0, data, chars);
	if (sizeof(data)-1 == chars) {
	    chunk = XmStringGenerate("[ ... ]\n",NULL,
				     XmMULTIBYTE_TEXT, NULL);
	    xmpage  = XmStringConcatAndFree(xmpage,chunk);
	}
    } else {
	xmpage = XmStringGenerate(data, NULL, XmMULTIBYTE_TEXT, NULL);
	if (sizeof(data)-1 == chars) {
	    chunk = XmStringGenerate("\n[ ... ]\n",NULL,
				     XmMULTIBYTE_TEXT, NULL);
	    xmpage  = XmStringConcatAndFree(xmpage,chunk);
	}
    }
    XtVaSetValues(hex_label,XmNlabelString,xmpage,NULL);
    XmStringFree(xmpage);
    XtVaSetValues(XtParent(hex_dlg),XmNtitle,filename,NULL);
    fclose(fp);
}
