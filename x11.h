#define PSEUDOCOLOR 1
#define TRUECOLOR   2

extern int      display_type;
extern int      display_depth;
extern XVisualInfo *info;

extern int      x11_grays;
extern unsigned long *x11_map;

extern unsigned long x11_lut_red[256];
extern unsigned long x11_lut_green[256];
extern unsigned long x11_lut_blue[256];
extern unsigned long x11_lut_gray[256];
extern unsigned long x11_map_color[256];
extern unsigned long x11_map_gray[64];

#define x11_black         x11_map_gray[0]
#define x11_gray          x11_map_gray[47*x11_grays/64]
#define x11_lightgray     x11_map_gray[55*x11_grays/64]
#define x11_white         x11_map_gray[63*x11_grays/64]

extern unsigned long x11_red;
extern unsigned long x11_green;
extern unsigned long x11_blue;

extern int      have_shmem;

int             x11_color_init(Widget shell, int *gray);

void            x11_data_to_ximage(unsigned char *rgb, unsigned char *ximage,
				   int x, int y, int sy, int gray);
XImage         *x11_create_ximage(Widget shell, int width, int height, void **shm);
void            x11_destroy_ximage(Widget shell, XImage * ximage, void *shm);
Pixmap          x11_create_pixmap(Widget shell, unsigned char *data,
				  int width, int height, int gray);

#define XPUTIMAGE(dpy,dr,gc,xi,a,b,c,d,w,h)                          \
    if (have_shmem)                                                  \
	XShmPutImage(dpy,dr,gc,xi,a,b,c,d,w,h,True);                 \
    else                                                             \
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h)
