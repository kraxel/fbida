#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

#include "readers.h"
#include "viewer.h"
#include "sane.h"
#include "ida.h"

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/PushB.h>
#include <Xm/CascadeB.h>
#include <Xm/RowColumn.h>

extern int sane_res;

/* ---------------------------------------------------------------------- */

static void
build_menu(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    WidgetList children,wlist;
    Cardinal nchildren;
    const SANE_Device **list;
    Widget push;
    XmString str;
    char action[256];
    int rc,i;

    /* del old */
    XtVaGetValues(widget,
		  XtNchildren,&children,
		  XtNnumChildren,&nchildren,
		  NULL);
    wlist = malloc(sizeof(Widget*)*nchildren);
    memcpy(wlist,children,sizeof(Widget*)*nchildren);
    for (i = 0; i < nchildren; i++)
        XtDestroyWidget(wlist[i]);
    free(wlist);

    /* create new */
    if (SANE_STATUS_GOOD != (rc = sane_init(NULL,NULL))) {
	fprintf(stderr,"sane_init: %s\n",sane_strstatus(rc));
	goto done;
    }
    sane_get_devices(&list,0);
    if (NULL == list[0])
	goto done;

    for (i = 0; list[i] != NULL; i++) {
	if (debug)
	    fprintf(stderr,"sane dev: %s | %s | %s | %s\n",
		    list[i]->name, list[i]->vendor,
		    list[i]->model, list[i]->type);
	str = XmStringGenerate((char*)list[i]->model,
			       NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(list[i]->name,
				       xmPushButtonWidgetClass,widget,
				       XmNlabelString,str,
				       NULL);
	XmStringFree(str);
	sprintf(action,"Scan(%s)",list[i]->name);
	XtAddCallback(push,XmNactivateCallback,action_cb,strdup(action));
    }

 done:
    sane_exit();
}

void
sane_menu(Widget menu)
{
    Widget submenu;
    int rc;
    
    if (SANE_STATUS_GOOD != (rc = sane_init(NULL,NULL))) {
	fprintf(stderr,"sane_init: %s\n",sane_strstatus(rc));
	goto done;
    }
    submenu = XmCreatePulldownMenu(menu,"scanM",NULL,0);
    XtVaCreateManagedWidget("scan",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,submenu,NULL);
    XtAddCallback(submenu, XmNmapCallback, build_menu, NULL);

 done:
    sane_exit();
}

/* ---------------------------------------------------------------------- */
/* load                                                                   */

#define BUF_LINES 16   /* read bigger chunks to reduce overhead */

struct sane_state {
    SANE_Handle sane;
    SANE_Parameters parm;
    SANE_Byte *buf;
    int started;
};

static void dump_desc(SANE_Handle h, int nr, const SANE_Option_Descriptor *opt)
{
    SANE_Bool b;
    SANE_Int i,flags;
    
    fprintf(stderr,"sane opt: name=%s title=%s type=%d unit=%d cap=%d\n",
	    opt->name, opt->title, opt->type, opt->unit, opt->cap);
    switch (opt->type) {
    case SANE_TYPE_BOOL:
	sane_control_option(h, nr, SANE_ACTION_GET_VALUE,
			    &b, &flags);
	fprintf(stderr,"  value=%s [bool]\n",b ? "true" : "false");
	break;
    case SANE_TYPE_INT:
	sane_control_option(h, nr, SANE_ACTION_GET_VALUE,
			    &i, &flags);
	fprintf(stderr,"  value=%d [int]\n",i);
	break;
    case SANE_TYPE_FIXED:
    case SANE_TYPE_STRING:
    case SANE_TYPE_BUTTON:
    case SANE_TYPE_GROUP:
	break;
    }
    switch (opt->constraint_type) {
    case SANE_CONSTRAINT_NONE:
	break;
    case SANE_CONSTRAINT_RANGE:
	fprintf(stderr,"  range=%d-%d\n",
		opt->constraint.range->min,
		opt->constraint.range->max);
	break;
    case SANE_CONSTRAINT_WORD_LIST:
	fprintf(stderr,"  constraint word_list:");
	for (i = 1; i <= opt->constraint.word_list[0]; i++)
	    fprintf(stderr," %d",opt->constraint.word_list[i]);
	fprintf(stderr,"\n");
	break;
    case SANE_CONSTRAINT_STRING_LIST:
	fprintf(stderr,"  constraint string_list:");
	for (i = 0; opt->constraint.string_list[i] != NULL; i++)
	    fprintf(stderr," %s",opt->constraint.string_list[i]);
	fprintf(stderr,"\n");
	break;
    };
}

static void*
sane_idainit(FILE *fp, char *filename, unsigned int page, struct ida_image_info *info,
	     int thumbnail)
{
    const SANE_Option_Descriptor *opt;
    SANE_Int flags, count;
    struct sane_state *h;
    int rc,i,value,dpi = 0;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    if (SANE_STATUS_GOOD != (rc = sane_init(NULL,NULL))) {
	fprintf(stderr,"sane_init: %s\n",sane_strstatus(rc));
	goto oops;
    }
    if (SANE_STATUS_GOOD != (rc = sane_open(filename,&h->sane))) {
	fprintf(stderr,"sane_open: %s\n",sane_strstatus(rc));
	goto oops;
    }

    /* set options */
    opt = sane_get_option_descriptor(h->sane,0);
    rc = sane_control_option(h->sane, 0, SANE_ACTION_GET_VALUE,
			     &count, &flags);
    for (i = 1; i < count; i++) {
	opt = sane_get_option_descriptor(h->sane,i);
	if (opt->name && 0 == strcmp(opt->name,SANE_NAME_SCAN_TL_X)) {
	    value = opt->constraint.range->min;
	    sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				&value, &flags);
	} else if (opt->name && 0 == strcmp(opt->name,SANE_NAME_SCAN_TL_Y)) {
	    value = opt->constraint.range->min;
	    sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				&value, &flags);
	} else if (opt->name && 0 == strcmp(opt->name,SANE_NAME_SCAN_BR_X)) {
	    value = opt->constraint.range->max;
	    sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				&value, &flags);
	} else if (opt->name && 0 == strcmp(opt->name,SANE_NAME_SCAN_BR_Y)) {
	    value = opt->constraint.range->max;
	    sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				&value, &flags);
	} else if (opt->name && 0 == strcmp(opt->name,SANE_NAME_PREVIEW)) {
	    value = SANE_FALSE;
	    sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				&value, &flags);
	} else if (opt->cap & SANE_CAP_AUTOMATIC)
	    sane_control_option(h->sane, i, SANE_ACTION_SET_AUTO,
				NULL, &flags);
	if (opt->name && 0 == strcmp(opt->name,SANE_NAME_SCAN_RESOLUTION)) {
	    if (sane_res) {
		dpi = sane_res;
		sane_control_option(h->sane, i, SANE_ACTION_SET_VALUE,
				    &dpi, &flags);
	    }
	    sane_control_option(h->sane, i, SANE_ACTION_GET_VALUE,
				&dpi, &flags);
	}
	if (debug)
	    dump_desc(h->sane,i,opt);
    }
    
    if (SANE_STATUS_GOOD != (rc = sane_start(h->sane))) {
	fprintf(stderr,"sane_start: %s\n",sane_strstatus(rc));
	goto oops;
    }
    h->started = 1;

    if (SANE_STATUS_GOOD != (rc = sane_get_parameters(h->sane,&h->parm))) {
	fprintf(stderr,"sane_get_parameters: %s\n",sane_strstatus(rc));
	goto oops;
    }

    if (h->parm.format != SANE_FRAME_GRAY &&
	h->parm.format != SANE_FRAME_RGB) {
	fprintf(stderr,"sane: unsupported frame format (%d)\n",h->parm.format);
	goto oops;
    }
    if (h->parm.depth != 8) {
	fprintf(stderr,"sane: unsupported color depth (%d)\n",h->parm.depth);
	goto oops;
    }
    if (-1 == h->parm.lines) {
	fprintf(stderr,"sane: can't handle unknown image size\n");
	goto oops;
    }

    info->width  = h->parm.pixels_per_line;
    info->height = h->parm.lines;
    if (dpi)
	info->dpi  = dpi;
    h->buf = malloc(h->parm.bytes_per_line * BUF_LINES);
    if (debug)
	fprintf(stderr,"sane: scanning %ux%u %s\n",info->width,info->height,
		(h->parm.format == SANE_FRAME_GRAY) ? "gray" : "color");

    return h;

 oops:
    if (h->buf)
	free(h->buf);
    if (h->started)
	sane_cancel(h->sane);
    if (h->sane)
	sane_close(h->sane);
    sane_exit();
    free(h);
    return NULL;
}

static void
sane_idaread(unsigned char *dst, unsigned int line, void *data)
{
    struct sane_state *h = data;
    unsigned int lines, total, offset, len;
    int rc, i;
    SANE_Byte *row;

    if (0 == (line % BUF_LINES)) {
	lines = BUF_LINES;
	if (lines > h->parm.lines - line)
	    lines = h->parm.lines - line;
	total  = h->parm.bytes_per_line * lines;
	offset = 0;
	while (offset < total) {
	    rc = sane_read(h->sane, h->buf + offset, total - offset, &len);
	    if (rc != SANE_STATUS_GOOD)
		return;
	    offset += len;
	}
    }
    row = h->buf + (line % BUF_LINES) * h->parm.bytes_per_line;
    switch (h->parm.format) {
    case SANE_FRAME_GRAY:
	for (i = 0; i < h->parm.pixels_per_line; i++) {
	    dst[3*i+0] = row[i];
	    dst[3*i+1] = row[i];
	    dst[3*i+2] = row[i];
	}
	break;
    case SANE_FRAME_RGB:
	memcpy(dst,row,h->parm.pixels_per_line*3);
	break;
    default:
	fprintf(stderr,"sane: read: internal error\n");
	exit(1);
    }
}

static void
sane_idadone(void *data)
{
    struct sane_state *h = data;

    sane_cancel(h->sane);
    sane_close(h->sane);
    sane_exit();
    free(h->buf);
    free(h);
}

struct ida_loader sane_loader = {
    name:  "sane interface",
    init:  sane_idainit,
    read:  sane_idaread,
    done:  sane_idadone,
};

static void __init init_rd(void)
{
    load_register(&sane_loader);
}
