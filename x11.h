#define PSEUDOCOLOR 1
#define TRUECOLOR   2

extern int      display_type;
extern int      display_depth;
extern XVisualInfo *info;

extern unsigned long x11_lut_red[256];
extern unsigned long x11_lut_green[256];
extern unsigned long x11_lut_blue[256];

extern unsigned long x11_red;
extern unsigned long x11_green;
extern unsigned long x11_blue;
extern unsigned long x11_gray;

extern int      have_shmem;

int             x11_color_init(Widget shell);

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
