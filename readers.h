#include "list.h"

enum ida_extype {
    EXTRA_COMMENT = 1,
    EXTRA_EXIF    = 2,
};

struct ida_extra {
    enum ida_extype   type;
    unsigned char     *data;
    unsigned int      size;
    struct ida_extra  *next;
};

/* image data and metadata */
struct ida_image_info {
    unsigned int      width;
    unsigned int      height;
    unsigned int      dpi;
    unsigned int      npages;
    struct ida_extra  *extra;

    int               thumbnail;
    unsigned int      real_width;
    unsigned int      real_height;
};

struct ida_image {
    struct ida_image_info  i;
    unsigned char          *data;
};
struct ida_rect {
    int x1,y1,x2,y2;
};

/* load image files */
struct ida_loader {
    char  *magic;
    int   moff;
    int   mlen;
    char  *name;
    void* (*init)(FILE *fp, char *filename, unsigned int page,
		  struct ida_image_info *i, int thumbnail);
    void  (*read)(unsigned char *dst, unsigned int line, void *data);
    void  (*done)(void *data);
    struct list_head list;
};

/* filter + operations */
struct ida_op {
    char  *name;
    void* (*init)(struct ida_image *src, struct ida_rect *rect,
		  struct ida_image_info *i, void *parm);
    void  (*work)(struct ida_image *src, struct ida_rect *rect,
		  unsigned char *dst, int line,
		  void *data);
    void  (*done)(void *data);
};

void* op_none_init(struct ida_image *src, struct ida_rect *rect,
		   struct ida_image_info *i, void *parm);
void  op_none_done(void *data);
void  op_free_done(void *data);

/* ----------------------------------------------------------------------- */
/* resolution                                                              */

#define res_cm_to_inch(x) ((x * 2540 + 5) / 1000)
#define res_m_to_inch(x)  ((x * 2540 + 5) / 100000)
#define res_inch_to_m(x)  ((x * 100000 + 5) / 2540)

/* ----------------------------------------------------------------------- */

/* helpers */
void load_bits_lsb(unsigned char *dst, unsigned char *src, int width,
		   int on, int off);
void load_bits_msb(unsigned char *dst, unsigned char *src, int width,
		   int on, int off);
void load_gray(unsigned char *dst, unsigned char *src, int width);
void load_graya(unsigned char *dst, unsigned char *src, int width);
void load_rgba(unsigned char *dst, unsigned char *src, int width);

int load_add_extra(struct ida_image_info *info, enum ida_extype type,
		   unsigned char *data, unsigned int size);
struct ida_extra* load_find_extra(struct ida_image_info *info,
				  enum ida_extype type);
int load_free_extras(struct ida_image_info *info);

/* ----------------------------------------------------------------------- */

/* other */
extern int debug;
extern struct ida_loader ppm_loader;
extern struct ida_loader jpeg_loader;
extern struct ida_loader sane_loader;
extern struct ida_writer ps_writer;
extern struct ida_writer jpeg_writer;

/* lists */
#define __init __attribute__ ((constructor))
#define __fini __attribute__ ((destructor))

extern struct list_head loaders;
void load_register(struct ida_loader *loader);
