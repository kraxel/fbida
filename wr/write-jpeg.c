#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <jpeglib.h>

#include "readers.h"
#include "writers.h"
#include "misc.h"

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <Xm/SelectioB.h>
#include "RegEdit.h"
#include "ida.h"
#include "viewer.h"

/* ---------------------------------------------------------------------- */
/* jpeg writer                                                            */

static Widget jpeg_shell;
static Widget jpeg_text;
static int jpeg_quality = 75;

static void
jpeg_button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = call_data;

    if (XmCR_OK == cb->reason) {
	jpeg_quality = atoi(XmTextGetString(jpeg_text));
	do_save_print();
    }
    XtUnmanageChild(jpeg_shell);
}

static int
jpeg_conf(Widget parent, struct ida_image *img)
{
    char tmp[32];
    
    if (!jpeg_shell) {
	/* build dialog */
	jpeg_shell = XmCreatePromptDialog(parent,"jpeg",NULL,0);
	XmdRegisterEditres(XtParent(jpeg_shell));
	XtUnmanageChild(XmSelectionBoxGetChild(jpeg_shell,XmDIALOG_HELP_BUTTON));
        jpeg_text = XmSelectionBoxGetChild(jpeg_shell,XmDIALOG_TEXT);
	XtAddCallback(jpeg_shell,XmNokCallback,jpeg_button_cb,NULL);
	XtAddCallback(jpeg_shell,XmNcancelCallback,jpeg_button_cb,NULL);
    }
    sprintf(tmp,"%d",jpeg_quality);
    XmTextSetString(jpeg_text,tmp);
    XtManageChild(jpeg_shell);
    return 0;
}

static int
jpeg_write(FILE *fp, struct ida_image *img)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned char *line;
    unsigned int i;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width  = img->i.width;
    cinfo.image_height = img->i.height;
    if (img->i.dpi) {
	cinfo.density_unit = 1;
	cinfo.X_density = img->i.dpi;
	cinfo.Y_density = img->i.dpi;
    }
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, jpeg_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = img->data; i < img->i.height; i++, line += img->i.width*3)
        jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    return 0;
}

struct ida_writer jpeg_writer = {
    label:  "JPEG",
    ext:    { "jpg", "jpeg", NULL},
    write:  jpeg_write,
    conf:   jpeg_conf,
};

static void __init init_wr(void)
{
    write_register(&jpeg_writer);
}

