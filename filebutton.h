#include <sys/stat.h>
#include <Xm/Xm.h>
#include "list.h"

#define DETAIL_SIZE      0
#define DETAIL_COMMENT   1
#define DETAIL_COUNT     2

struct fileinfo_cache;

struct file_button {
    /* file info */
    char               *filename;
    char               *basename;
    unsigned char      d_type;
    struct stat        st;
    struct fileinfo    *info;

    /* Widget + other X11 stuff */
    Screen             *screen;
    Widget             widget;
    XmString           label;
    XmString           details[DETAIL_COUNT];
    Pixmap             small,large;

    /* lists */
    struct list_head   global;
    struct list_head   window;

    /* private for file info + icon loader */
    struct list_head   queue;
    int                state,y;
    struct ida_loader  *loader;
    void               *wdata;
    struct ida_image   wimg;
    struct ida_image   simg;
};

void fileinfo_queue(struct file_button *file);
void fileinfo_invalidate(char *filename);
void file_set_icon(struct file_button *file, Pixmap s, Pixmap l);
void file_set_info(struct file_button *file, struct fileinfo *info);

/*----------------------------------------------------------------------*/

void container_detail_cb(Widget widget, XtPointer clientdata,
			 XtPointer call_data);
void container_spatial_cb(Widget widget, XtPointer clientdata,
			  XtPointer call_data);

void container_resize_eh(Widget widget, XtPointer clientdata,
			 XEvent *event, Boolean *d);
void container_convert_cb(Widget widget, XtPointer clientdata,
			  XtPointer call_data);
void container_traverse_cb(Widget scroll, XtPointer clientdata,
			   XtPointer call_data);

void container_menu_edit(Widget menu, Widget container,
			 int cut, int copy, int paste, int del);
void container_menu_view(Widget menu, Widget container);
void container_menu_ops(Widget menu, Widget container);

void container_relayout(Widget container);
void container_setsize(Widget container);
void container_delwidgets(Widget container);

/*----------------------------------------------------------------------*/

int file_cmp_alpha(const struct file_button *aa,
		   const struct file_button *bb);
int file_createwidgets(Widget parent, struct file_button *file);
