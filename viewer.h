#include <X11/Intrinsic.h>

#define POINTER_NORMAL    0
#define POINTER_BUSY      1
#define POINTER_PICK      2
#define RUBBER_NEW        3
#define RUBBER_MOVE       4
#define RUBBER_X1         5
#define RUBBER_Y1         6
#define RUBBER_X2         7
#define RUBBER_Y2         8
#define POINTER_COUNT     9

extern Cursor ptrs[];

/* ----------------------------------------------------------------------- */

typedef void (*viewer_pick_cb)(int x, int y, unsigned char *pix,
			       XtPointer data);

struct ida_viewer {
    /* x11 stuff */
    Widget           widget;
    GC               wgc;
    XtIntervalId     timer;

    /* image data */
    struct ida_image img;
    struct ida_image undo;
    char             *file;

    /* view data */
    int              zoom;
    unsigned int     scrwidth, scrheight;
    XImage           *ximage;
    void             *ximage_shm;
    unsigned char    *rgb_line;
    unsigned char    *dither_line;
    unsigned char    *preview_line;

    /* marked rectangle */
    struct ida_rect  current;
    int              marked,state;
    int              last_x,last_y;
    unsigned long    mask;

    /* pixel picker */
    viewer_pick_cb   pick_cb;
    XtPointer        pick_data;

    /* workproc state */
    XtWorkProcId     wproc;
    unsigned int     line;
    unsigned int     steps;

    /* image loader */
    unsigned int     load_line;
    void             (*load_read)(unsigned char *dst, unsigned int line,
				  void *data);
    void             (*load_done)(void *data);
    void             *load_data;

    /* image operation */
    struct ida_image op_src;
    struct ida_rect  op_rect;
    unsigned int     op_line;
    unsigned int     op_preview;
    void             (*op_work)(struct ida_image *src, struct ida_rect *rect,
				unsigned char *dst, int line,
				void *data);
    void             (*op_done)(void *data);
    void             *op_data;
};

/* ----------------------------------------------------------------------- */

Pixmap image_to_pixmap(struct ida_image *img);

/* ----------------------------------------------------------------------- */

struct ida_viewer* viewer_init(Widget widget);
int viewer_loader_start(struct ida_viewer *ida, struct ida_loader *loader,
			FILE *fp, char *filename, unsigned int page);
int viewer_loadimage(struct ida_viewer *ida, char *filename, unsigned int page);
int viewer_setimage(struct ida_viewer *ida, struct ida_image *img, char *name);
void viewer_autozoom(struct ida_viewer *ida);
void viewer_setzoom(struct ida_viewer *ida, int zoom);
int viewer_i2s(int zoom, int val);
int viewer_undo(struct ida_viewer *ida);
int viewer_start_op(struct ida_viewer *ida, struct ida_op *op, void *parm);
int viewer_start_preview(struct ida_viewer *ida, struct ida_op *op,
			 void *parm);
int viewer_cancel_preview(struct ida_viewer *ida);

void viewer_pick(struct ida_viewer *ida, viewer_pick_cb cb, XtPointer data);
void viewer_unpick(struct ida_viewer *ida);
