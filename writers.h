#include "list.h"
#include <X11/Intrinsic.h>

/* save image files */
struct ida_writer {
    char  *label;
    char  *ext[8];
    int   (*write)(FILE *fp, struct ida_image *img);
    int   (*conf)(Widget widget, struct ida_image *img);
    struct list_head list;
};

extern struct list_head writers;
void write_register(struct ida_writer *writer);
