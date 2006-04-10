/*
 * image viewer, for framebuffer devices
 *
 *   (c) 1998-2004 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <getopt.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include <jpeglib.h>

#include <libexif/exif-data.h>

#ifdef HAVE_LIBLIRC
# include "lirc/lirc_client.h"
# include "lirc.h"
#endif

#include "readers.h"
#include "dither.h"
#include "fbtools.h"
#include "fb-gui.h"
#include "filter.h"
#include "desktop.h"
#include "list.h"
#include "fbiconfig.h"

#include "jpeg/transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"

#define TRUE            1
#define FALSE           0
#define MAX(x,y)        ((x)>(y)?(x):(y))
#define MIN(x,y)        ((x)<(y)?(x):(y))
#define ARRAY_SIZE(x)   (sizeof(x)/sizeof(x[0]))

#define KEY_EOF        -1       /* ^D */
#define KEY_ESC        -2
#define KEY_SPACE      -3
#define KEY_Q          -4
#define KEY_PGUP       -5
#define KEY_PGDN       -6
#define KEY_TIMEOUT    -7
#define KEY_TAGFILE    -8
#define KEY_PLUS       -9
#define KEY_MINUS     -10
#define KEY_VERBOSE   -11
#define KEY_ASCALE    -12
#define KEY_DESC      -13

/* with arg */
#define KEY_GOTO     -100
#define KEY_SCALE    -101
#define KEY_DELAY    -102

/* edit */
#define KEY_DELETE   -200
#define KEY_ROT_CW   -201
#define KEY_ROT_CCW  -202

#define DEFAULT_DEVICE  "/dev/fb0"

/* ---------------------------------------------------------------------- */

/* lirc fd */
int lirc = -1;

/* variables for read_image */
int32_t         lut_red[256], lut_green[256], lut_blue[256];
int             dither = FALSE, pcd_res = 3;
int             v_steps = 50;
int             h_steps = 50;
int             text_steps;
int             textreading = 0, redraw = 0, statusline = 1;
int             new_image;
int             left, top;
int             show_top;
int             show_bottom;
int             fitwidth;

/* file list */
struct flist {
    int               nr;
    int               tag;
    char              *name;
    struct list_head  list;
};
static LIST_HEAD(flist);
static int           fcount;
static struct flist  *fcurrent;

/* framebuffer */
char                       *fbdev = NULL;
char                       *fbmode  = NULL;
int                        fd, switch_last, debug;

unsigned short red[256],  green[256],  blue[256];
struct fb_cmap cmap  = { 0, 256, red,  green,  blue };

static float fbgamma = 1;

/* Command line options. */
int autodown;
int autoup;
int autofirst;
int comments;
int transparency = 40;

/* font handling */
static char *fontname = NULL;
static FT_Face face;

/* ---------------------------------------------------------------------- */

static void
version(void)
{
    fprintf(stderr,
	    "fbi version " VERSION ", compiled on %s\n"
	    "(c) 1999-2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]\n",
	    __DATE__ );
}

static void
usage(char *name)
{
    char           *h;

    if (NULL != (h = strrchr(name, '/')))
	name = h+1;
    fprintf(stderr,
	    "\n"
	    "This program displays images using the Linux framebuffer device.\n"
	    "Supported formats: PhotoCD, jpeg, ppm, gif, tiff, xwd, bmp, png.\n"
	    "It tries to use ImageMagick's convert for unknown file formats.\n"
	    "\n"
	    "usage: %s [ options ] file1 file2 ... fileN\n"
	    "\n",
	    name);

    cfg_help_cmdline(stderr,fbi_cmd,4,20,0);
    cfg_help_cmdline(stderr,fbi_cfg,4,20,40);

    fprintf(stderr,
	    "\n"
	    "Large images can be scrolled using the cursor keys.  Zoom in/out\n"
	    "works with '+' and '-'.  Use ESC or 'q' to quit.  Space and PgDn\n"
	    "show the next, PgUp shows the previous image. Jumping to a image\n"
	    "works with <number>g.  Return acts like Space but additionally\n"
	    "prints the filename of the currently displayed image to stdout.\n"
	    "\n");
}

/* ---------------------------------------------------------------------- */

static int flist_add(char *filename)
{
    struct flist *f;

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));
    f->name = strdup(filename);
    list_add_tail(&f->list,&flist);
    return 0;
}

static int flist_add_list(char *listfile)
{
    char filename[256];
    FILE *list;

    list = fopen(listfile,"r");
    if (NULL == list) {
	fprintf(stderr,"open %s: %s\n",listfile,strerror(errno));
	return -1;
    }
    while (NULL != fgets(filename,sizeof(filename)-1,list)) {
	size_t off = strcspn(filename,"\r\n");
	if (off)
	    filename[off] = 0;
	flist_add(filename);
    }
    fclose(list);
    return 0;
}

static int flist_del(struct flist *f)
{
    list_del(&f->list);
    free(f->name);
    free(f);
    return 0;
}

static void flist_renumber(void)
{
    struct list_head *item;
    struct flist *f;
    int i = 0;

    list_for_each(item,&flist) {
	f = list_entry(item, struct flist, list);
	f->nr = ++i;
    }
    fcount = i;
}

static int flist_islast(struct flist *f)
{
    return (&flist == f->list.next) ? 1 : 0;
}

static int flist_isfirst(struct flist *f)
{
    return (&flist == f->list.prev) ? 1 : 0;
}

static struct flist* flist_first(void)
{
    return list_entry(flist.next, struct flist, list);
}

static struct flist* flist_last(void)
{
    return list_entry(flist.prev, struct flist, list);
}

static struct flist* flist_next(struct flist *f, int eof, int loop)
{
    if (flist_islast(f)) {
	if (eof)
	    return NULL;
	if (loop)
	    return flist_first();
	return f;
    }
    return list_entry(f->list.next, struct flist, list);
}

static struct flist* flist_prev(struct flist *f, int loop)
{
    if (flist_isfirst(f)) {
	if (loop)
	    return flist_last();
	return f;
    }
    return list_entry(f->list.prev, struct flist, list);
}

static struct flist* flist_goto(int dest)
{
    struct list_head *item;
    struct flist *f;

    list_for_each(item,&flist) {
	f = list_entry(item, struct flist, list);
	if (f->nr == dest)
	    return f;
    }
    return NULL;
}

static void flist_randomize(void)
{
    struct flist *f;
    int count;

    srand((unsigned)time(NULL));
    flist_renumber();
    for (count = fcount; count > 0; count--) {
	f = flist_goto((rand() % count)+1);
	list_del(&f->list);
	list_add_tail(&f->list,&flist);
	flist_renumber();
    }
}

static void flist_print_tagged(FILE *fp)
{
    struct list_head *item;
    struct flist *f;

    list_for_each(item,&flist) {
	f = list_entry(item, struct flist, list);
	if (f->tag)
	    fprintf(fp,"%s\n",f->name);
    }
}

/* ---------------------------------------------------------------------- */

static void
shadow_draw_image(struct ida_image *img, int xoff, int yoff,
		  unsigned int first, unsigned int last)
{
    unsigned int     dwidth  = MIN(img->i.width,  fb_var.xres);
    unsigned int     dheight = MIN(img->i.height, fb_var.yres);
    unsigned int     data, offset, y, xs, ys;

    shadow_clear_lines(first, last);
    
    /* offset for image data (image > screen, select visible area) */
    offset = (yoff * img->i.width + xoff) * 3;

    /* offset for video memory (image < screen, center image) */
    xs = 0, ys = 0;
    if (img->i.width < fb_var.xres)
	xs += (fb_var.xres - img->i.width) / 2;
    if (img->i.height < fb_var.yres)
	ys += (fb_var.yres - img->i.height) / 2;

    /* go ! */
    for (data = 0, y = 0;
	 data < img->i.width * img->i.height * 3
	     && data / img->i.width / 3 < dheight;
	 data += img->i.width * 3, y++) {
	if (ys+y < first)
	    continue;
	if (ys+y > last)
	    continue;
	shadow_draw_rgbdata(xs, ys+y, dwidth, img->data + data + offset);
    }
}

static void status_prepare(struct ida_image *img)
{
    int y1 = fb_var.yres - (face->size->metrics.height >> 6);
    int y2 = fb_var.yres - 1;

    if (img) {
	shadow_draw_image(img, left, top, y1, y2);
	shadow_darkify(0, fb_var.xres-1, y1, y2, transparency);
    } else {
	shadow_clear_lines(y1, y2);
    }
    shadow_draw_line(0, fb_var.xres-1, y1-1, y1-1);
}

static void status_update(struct ida_image *img, unsigned char *desc, char *info)
{
    int yt = fb_var.yres + (face->size->metrics.descender >> 6);
    wchar_t str[128];
    
    if (!statusline)
	return;
    status_prepare(img);

    swprintf(str,sizeof(str),L"%s",desc);
    shadow_draw_string(face, 0, yt, str, -1);
    if (info) {
	swprintf(str,sizeof(str), L"[ %s ] H - Help", info);
    } else {
	swprintf(str,sizeof(str), L"| H - Help");
    }
    shadow_draw_string(face, fb_var.xres, yt, str, 1);

    shadow_render();
}

static void status_error(unsigned char *msg)
{
    int yt = fb_var.yres + (face->size->metrics.descender >> 6);
    wchar_t str[128];

    status_prepare(NULL);

    swprintf(str,sizeof(str), L"%s", msg);
    shadow_draw_string(face, 0, yt, str, -1);

    shadow_render();
    sleep(2);
}

static void status_edit(struct ida_image *img, unsigned char *msg, int pos)
{
    int yt = fb_var.yres + (face->size->metrics.descender >> 6);
    wchar_t str[128];

    status_prepare(img);

    swprintf(str,sizeof(str), L"%s", msg);
    shadow_draw_string_cursor(face, 0, yt, str, pos);

    shadow_render();
}

static void show_exif(struct flist *f)
{
    static unsigned int tags[] = {
	0x010f, // Manufacturer
	0x0110, // Model

	0x0112, // Orientation
	0x0132, // Date and Time

	0x01e3, // White Point
	0x829a, // Exposure Time
	0x829d, // FNumber
	0x9206, // Subject Distance
	0xa40c, // Subject Distance Range
	0xa405, // Focal Length In 35mm Film
	0x9209, // Flash
    };
    ExifData   *ed;
    ExifEntry  *ee;
    unsigned int tag,l1,l2,len,count,i;
    const char *title[ARRAY_SIZE(tags)];
    char *value[ARRAY_SIZE(tags)];
    wchar_t *linebuffer[ARRAY_SIZE(tags)];

    if (!visible)
	return;

    ed = exif_data_new_from_file(f->name);
    if (NULL == ed) {
	status_error("image has no EXIF data");
	return;
    }

    /* pass one -- get data + calc size */
    l1 = 0;
    l2 = 0;
    for (tag = 0; tag < ARRAY_SIZE(tags); tag++) {
	ee = exif_content_get_entry (ed->ifd[EXIF_IFD_0], tags[tag]);
	if (NULL == ee)
	    ee = exif_content_get_entry (ed->ifd[EXIF_IFD_EXIF], tags[tag]);
	if (NULL == ee) {
	    title[tag] = NULL;
	    value[tag] = NULL;
	    continue;
	}
	title[tag] = exif_tag_get_title(tags[tag]);
#ifdef HAVE_NEW_EXIF
	value[tag] = malloc(128);
	exif_entry_get_value(ee, value[tag], 128);
#else
	value[tag] = strdup(exif_entry_get_value(ee));
#endif
	len = strlen(title[tag]);
	if (l1 < len)
	    l1 = len;
	len = strlen(value[tag]);
	if (l2 < len)
	    l2 = len;
    }

    /* pass two -- print stuff */
    count = 0;
    for (tag = 0; tag < ARRAY_SIZE(tags); tag++) {
	if (NULL == title[tag])
	    continue;
	linebuffer[count] = malloc(sizeof(wchar_t)*(l1+l2+8));
	swprintf(linebuffer[count], l1+l2+8,
		 L"%-*.*s : %-*.*s",
		 l1, l1, title[tag],
		 l2, l2, value[tag]);
	count++;
    }
    shadow_draw_text_box(face, 24, 16, transparency,
			 linebuffer, count);
    shadow_render();

    /* pass three -- free data */
    for (tag = 0; tag < ARRAY_SIZE(tags); tag++)
	if (NULL != value[tag])
	    free(value[tag]);
    exif_data_unref (ed);
    for (i = 0; i < count; i++)
	free(linebuffer[i]);
}

static void show_help(void)
{
    static wchar_t *help[] = {
	L"keyboard commands",
	L"~~~~~~~~~~~~~~~~~",
	L"  ESC, Q      - quit",
	L"  pgdn, space - next image",
	L"  pgup        - previous image",
	L"  +/-         - zoom in/out",
	L"  A           - autozoom image",
	L"  cursor keys - scroll image",
	L"",
	L"  H           - show this help text",
	L"  I           - show EXIF info",
	L"  P           - pause slideshow",
	L"  V           - toggle statusline",
	L"",
	L"available if started with --edit switch,",
	L"rotation works for jpeg images only:",
	L"  shift+D     - delete image",
	L"  R           - rotate clockwise",
	L"  L           - rotate counter-clockwise",
    };

    shadow_draw_text_box(face, 24, 16, transparency,
			 help, ARRAY_SIZE(help));
    shadow_render();
}

/* ---------------------------------------------------------------------- */

struct termios  saved_attributes;
int             saved_fl;

static void
tty_raw(void)
{
    struct termios tattr;
    
    fcntl(0,F_GETFL,&saved_fl);
    tcgetattr (0, &saved_attributes);
    
    fcntl(0,F_SETFL,O_NONBLOCK);
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ICANON|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (0, TCSAFLUSH, &tattr);
}

static void
tty_restore(void)
{
    fcntl(0,F_SETFL,saved_fl);
    tcsetattr (0, TCSANOW, &saved_attributes);
}

/* testing: find key codes */
static void debug_key(char *key)
{
    char linebuffer[128];
    int i,len;

    len = sprintf(linebuffer,"key: ");
    for (i = 0; key[i] != '\0'; i++)
	len += snprintf(linebuffer+len, sizeof(linebuffer)-len,
			"%s%c",
			key[i] < 0x20 ? "^" : "",
			key[i] < 0x20 ? key[i] + 0x40 : key[i]);
    status_update(NULL, linebuffer, NULL);
}

static void
console_switch(void)
{
    switch (fb_switch_state) {
    case FB_REL_REQ:
	fb_switch_release();
    case FB_INACTIVE:
	visible = 0;
	break;
    case FB_ACQ_REQ:
	fb_switch_acquire();
    case FB_ACTIVE:
	visible = 1;
	ioctl(fd,FBIOPAN_DISPLAY,&fb_var);
	shadow_set_palette(fd);
	shadow_set_dirty();
	shadow_render();
	break;
    default:
	break;
    }
    switch_last = fb_switch_state;
    return;
}

/* ---------------------------------------------------------------------- */

static void free_image(struct ida_image *img)
{
    if (img) {
	if (img->data)
	    free(img->data);
	free(img);
    }
}

static struct ida_image*
read_image(char *filename)
{
    char command[1024];
    struct ida_loader *loader = NULL;
    struct ida_image *img;
    struct list_head *item;
    char blk[512];
    FILE *fp;
    unsigned int y;
    void *data;
    
    new_image = 1;

    /* open file */
    if (NULL == (fp = fopen(filename, "r"))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return NULL;
    }
    memset(blk,0,sizeof(blk));
    fread(blk,1,sizeof(blk),fp);
    rewind(fp);

    /* pick loader */
    list_for_each(item,&loaders) {
        loader = list_entry(item, struct ida_loader, list);
	if (NULL == loader->magic)
	    break;
	if (0 == memcmp(blk+loader->moff,loader->magic,loader->mlen))
	    break;
	loader = NULL;
    }
    if (NULL == loader) {
	/* no loader found, try to use ImageMagick's convert */
	snprintf(command,sizeof(command),
		 "convert -depth 8 \"%s\" ppm:-",filename);
	if (NULL == (fp = popen(command,"r")))
	    return NULL;
	loader = &ppm_loader;
    }

    /* load image */
    img = malloc(sizeof(*img));
    memset(img,0,sizeof(*img));
    data = loader->init(fp,filename,0,&img->i,0);
    if (NULL == data) {
	fprintf(stderr,"loading %s [%s] FAILED\n",filename,loader->name);
	free_image(img);
	return NULL;
    }
    img->data = malloc(img->i.width * img->i.height * 3);
    for (y = 0; y < img->i.height; y++) {
        if (switch_last != fb_switch_state)
	    console_switch();
	loader->read(img->data + img->i.width * 3 * y, y, data);
    }
    loader->done(data);
    return img;
}

static struct ida_image*
scale_image(struct ida_image *src, float scale)
{
    struct op_resize_parm p;
    struct ida_rect  rect;
    struct ida_image *dest;
    void *data;
    unsigned int y;

    dest = malloc(sizeof(*dest));
    memset(dest,0,sizeof(*dest));
    memset(&rect,0,sizeof(rect));
    memset(&p,0,sizeof(p));
    
    p.width  = src->i.width  * scale;
    p.height = src->i.height * scale;
    p.dpi    = src->i.dpi;
    if (0 == p.width)
	p.width = 1;
    if (0 == p.height)
	p.height = 1;
    
    data = desc_resize.init(src,&rect,&dest->i,&p);
    dest->data = malloc(dest->i.width * dest->i.height * 3);
    for (y = 0; y < dest->i.height; y++) {
	if (switch_last != fb_switch_state)
	    console_switch();
	desc_resize.work(src,&rect,
			 dest->data + 3 * dest->i.width * y,
			 y, data);
    }
    desc_resize.done(data);
    return dest;
}

static float auto_scale(struct ida_image *img)
{
    float xs,ys,scale;
    
    xs = (float)fb_var.xres / img->i.width;
    if (fitwidth)
	return xs;
    ys = (float)fb_var.yres / img->i.height;
    scale = (xs < ys) ? xs : ys;
    return scale;
}

/* ---------------------------------------------------------------------- */

static int
svga_show(struct ida_image *img, int timeout, char *desc, char *info, int *nr)
{
    static int      paused = 0;
    int             exif = 0, help = 0;
    int             rc;
    char            key[11];
    fd_set          set;
    struct timeval  limit;
    char            linebuffer[80];
    int             fdmax;

    *nr = 0;
    if (NULL == img)
	return KEY_SPACE; /* skip */
    
    if (new_image) {
	/* start with centered image, if larger than screen */
	if (img->i.width > fb_var.xres)
	    left = (img->i.width - fb_var.xres) / 2;
	if (img->i.height > fb_var.yres) {
	    top = (img->i.height - fb_var.yres) / 2;
	    if (show_top) {
		show_top = 0;
		top = 0;
	    }
	    if (show_bottom) {
		show_bottom = 0;
		top = img->i.height - fb_var.yres;
	    }
	}
	new_image = 0;
    }

    redraw = 1;
    for (;;) {
	if (redraw) {
	    redraw = 0;
	    if (img->i.height <= fb_var.yres) {
		top = 0;
	    } else {
		if (top < 0)
		    top = 0;
		if (top + fb_var.yres > img->i.height)
		    top = img->i.height - fb_var.yres;
	    }
	    if (img->i.width <= fb_var.xres) {
		left = 0;
	    } else {
		if (left < 0)
		    left = 0;
		if (left + fb_var.xres > img->i.width)
		    left = img->i.width - fb_var.xres;
	    }
	    shadow_draw_image(img, left, top, 0, fb_var.yres-1);
	    status_update(img, desc, info);
	    shadow_render();
	}
        if (switch_last != fb_switch_state) {
	    console_switch();
	    continue;
	}
	FD_ZERO(&set);
	FD_SET(0, &set);
	fdmax = 1;
#ifdef HAVE_LIBLIRC
	if (-1 != lirc) {
	    FD_SET(lirc,&set);
	    fdmax = lirc+1;
	}
#endif
	limit.tv_sec = timeout;
	limit.tv_usec = 0;
	rc = select(fdmax, &set, NULL, NULL,
		    (0 != timeout && !paused) ? &limit : NULL);
        if (switch_last != fb_switch_state) {
	    console_switch();
	    continue;
	}
	if (0 == rc)
	    return KEY_TIMEOUT;

	if (FD_ISSET(0,&set)) {
	    /* stdin, i.e. keyboard */
	    rc = read(0, key, sizeof(key)-1);
	    if (rc < 1) {
		/* EOF */
		return KEY_EOF;
	    }
	    key[rc] = 0;
	}
#ifdef HAVE_LIBLIRC
	if (lirc != -1 && FD_ISSET(lirc,&set)) {
	    /* lirc input */
	    if (-1 == lirc_fbi_havedata(&rc,key)) {
		fprintf(stderr,"lirc: connection lost\n");
		close(lirc);
		lirc = -1;
	    }
	    key[rc] = 0;
        }
#endif

	if (rc == 1 && (*key == 'q'    || *key == 'Q' ||
			*key == 'e'    || *key == 'E' ||
			*key == '\x1b' || *key == '\n')) {
	    if (*key == '\n')
		return KEY_TAGFILE;
	    if (*key == '\x1b')
		return KEY_ESC;
	    return KEY_Q;

	} else if (0 == strcmp(key, " ")) {
	    if (textreading && top < (int)(img->i.height - fb_var.yres)) {
		redraw = 1;
		top += text_steps;
	    } else {
		return KEY_SPACE;
	    }

	} else if (0 == strcmp(key, "\x1b[A") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top -= v_steps;
	} else if (0 == strcmp(key, "\x1b[B") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top += v_steps;
	} else if (0 == strcmp(key, "\x1b[1~") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top = 0;
	} else if (0 == strcmp(key, "\x1b[4~")) {
	    redraw = 1;
	    top = img->i.height - fb_var.yres;
	} else if (0 == strcmp(key, "\x1b[D") && img->i.width > fb_var.xres) {
	    redraw = 1;
	    left -= h_steps;
	} else if (0 == strcmp(key, "\x1b[C") && img->i.width > fb_var.xres) {
	    redraw = 1;
	    left += h_steps;

	} else if (0 == strcmp(key, "\x1b[5~") ||
		   0 == strcmp(key, "j")       ||
		   0 == strcmp(key, "J")) {
	    if (textreading && top > 0) {
		redraw = 1;
		top -= text_steps;
	    } else {
		return KEY_PGUP;
	    }

	} else if (0 == strcmp(key, "\x1b[6~") ||
		   0 == strcmp(key, "k")       ||
		   0 == strcmp(key, "K")       ||
		   0 == strcmp(key, "n")       ||
		   0 == strcmp(key, "N")) {
	    if (textreading && top < (int)(img->i.height - fb_var.yres)) {
		redraw = 1;
		top += text_steps;
	    } else {
		return KEY_PGDN;
	    }
	    
	} else if (0 == strcmp(key, "+")) {
	    return KEY_PLUS;
	} else if (0 == strcmp(key, "-")) {
	    return KEY_MINUS;
	} else if (0 == strcmp(key, "a") ||
		   0 == strcmp(key, "A")) {
	    return KEY_ASCALE;
	    
	} else if (0 == strcmp(key, "p") ||
		   0 == strcmp(key, "P")) {
	    if (0 != timeout) {
		paused = !paused;
		status_update(img, paused ? "pause on " : "pause off", NULL);
	    }

	} else if (0 == strcmp(key, "D")) {
	    return KEY_DELETE;
	} else if (0 == strcmp(key, "r") ||
		   0 == strcmp(key, "R")) {
	    return KEY_ROT_CW;
	} else if (0 == strcmp(key, "l") ||
		   0 == strcmp(key, "L")) {
	    return KEY_ROT_CCW;
	    
	} else if (0 == strcmp(key, "h") ||
		   0 == strcmp(key, "H")) {
	    if (!help) {
		show_help();
		help = 1;
	    } else {
		redraw = 1;
		help = 0;
	    }
	    exif = 0;

	} else if (0 == strcmp(key, "i") ||
		   0 == strcmp(key, "I")) {
	    if (!exif) {
		show_exif(fcurrent);
		exif = 1;
	    } else {
		redraw = 1;
		exif = 0;
	    }
	    help = 0;

	} else if (0 == strcmp(key, "v") ||
		   0 == strcmp(key, "V")) {
	    return KEY_VERBOSE;

	} else if (0 == strcmp(key, "t") ||
		   0 == strcmp(key, "T")) {
	    return KEY_DESC;

	} else if (rc == 1 && (*key == 'g' || *key == 'G')) {
	    return KEY_GOTO;
	} else if (rc == 1 && (*key == 's' || *key == 'S')) {
	    return KEY_SCALE;
	} else if (rc == 1 && (*key == 'x' || *key == 'X')) {
	    return KEY_DELAY;
	} else if (rc == 1 && *key >= '0' && *key <= '9') {
	    *nr = *nr * 10 + (*key - '0');
	    snprintf(linebuffer, sizeof(linebuffer), "> %d",*nr);
	    status_update(img, linebuffer, NULL);
	} else {
	    *nr = 0;
#if 0
	    debug_key(key);
#endif
	}
    }
}

static void scale_fix_top_left(float old, float new, struct ida_image *img)
{
    unsigned int width, height;
    float cx,cy;

    cx = (float)(left + fb_var.xres/2) / (img->i.width  * old);
    cy = (float)(top  + fb_var.yres/2) / (img->i.height * old);

    width  = img->i.width  * new;
    height = img->i.height * new;
    left   = cx * width  - fb_var.xres/2;
    top    = cy * height - fb_var.yres/2;
}

/* ---------------------------------------------------------------------- */

static char *my_basename(char *filename)
{
    char *h;
    
    h = strrchr(filename,'/');
    if (h)
	return h+1;
    return filename;
}

static char *file_desktop(char *filename)
{
    static char desc[128];
    char *h;

    strncpy(desc,filename,sizeof(desc)-1);
    if (NULL != (h = strrchr(filename,'/'))) {
	snprintf(desc,sizeof(desc),"%.*s/%s", 
		 (int)(h - filename), filename,
		 ".directory");
    } else {
	strcpy(desc,".directory");
    }
    return desc;
}

static char *make_desc(struct ida_image_info *img, char *filename)
{
    static char linebuffer[128];
    struct ida_extra *extra;
    char *desc;
    int len;

    memset(linebuffer,0,sizeof(linebuffer));
    strncpy(linebuffer,filename,sizeof(linebuffer)-1);

    if (comments) {
	extra = load_find_extra(img, EXTRA_COMMENT);
	if (extra)
	    snprintf(linebuffer,sizeof(linebuffer),"%.*s",
		     extra->size,extra->data);
    } else {
	desc = file_desktop(filename);
	len = desktop_read_entry(desc, "Comment=", linebuffer, sizeof(linebuffer));
	if (0 != len)
	    snprintf(linebuffer+len,sizeof(linebuffer)-len,
		     " (%s)", my_basename(filename));
    }
    return linebuffer;
}

static char *make_info(struct ida_image *img, float scale)
{
    static char linebuffer[128];
    
    snprintf(linebuffer, sizeof(linebuffer),
	     "%s%.0f%% %dx%d %d/%d",
	     fcurrent->tag ? "* " : "",
	     scale*100,
	     img->i.width, img->i.height,
	     fcurrent->nr, fcount);
    return linebuffer;
}

static char edit_line(struct ida_image *img, char *line, int max)
{
    int      len = strlen(line);
    int      pos = len;
    int      rc;
    char     key[11];
    fd_set  set;

    do {
	status_edit(img,line,pos);
	
	FD_SET(0, &set);
	rc = select(1, &set, NULL, NULL, NULL);
        if (switch_last != fb_switch_state) {
	    console_switch();
	    continue;
	}
	rc = read(0, key, sizeof(key)-1);
	if (rc < 1) {
	    /* EOF */
	    return KEY_EOF;
	}
	key[rc] = 0;

	if (0 == strcmp(key,"\x0a")) {
	    /* Enter */
	    return 0;
	    
	} else if (0 == strcmp(key,"\x1b")) {
	    /* ESC */
	    return KEY_ESC;
	    
	} else if (0 == strcmp(key,"\x1b[C")) {
	    /* cursor right */
	    if (pos < len)
		pos++;

	} else if (0 == strcmp(key,"\x1b[D")) {
	    /* cursor left */
	    if (pos > 0)
		pos--;

	} else if (0 == strcmp(key,"\x1b[1~")) {
	    /* home */
	    pos = 0;
	    
	} else if (0 == strcmp(key,"\x1b[4~")) {
	    /* end */
	    pos = len;
	    
	} else if (0 == strcmp(key,"\x7f")) {
	    /* backspace */
	    if (pos > 0) {
		memmove(line+pos-1,line+pos,len-pos+1);
		pos--;
		len--;
	    }

	} else if (0 == strcmp(key,"\x1b[3~")) {
	    /* delete */
	    if (pos < len) {
		memmove(line+pos,line+pos+1,len-pos);
		len--;
	    }

	} else if (1 == rc && isprint(key[0]) && len < max) {
	    /* new key */
	    if (pos < len)
		memmove(line+pos+1,line+pos,len-pos+1);
	    line[pos] = key[0];
	    pos++;
	    len++;
	    line[len] = 0;

	} else if (0 /* debug */) {
	    debug_key(key);
	    sleep(1);
	}
    } while (1);
}

static void edit_desc(struct ida_image *img, char *filename)
{
    static char linebuffer[128];
    char *desc;
    int len, rc;

    desc = file_desktop(filename);
    len = desktop_read_entry(desc, "Comment=", linebuffer, sizeof(linebuffer));
    if (0 == len) {
	linebuffer[0] = 0;
	len = 0;
    }
    rc = edit_line(img, linebuffer, sizeof(linebuffer)-1);
    if (0 != rc)
	return;
    desktop_write_entry(desc, "Directory", "Comment=", linebuffer);
}

/* ---------------------------------------------------------------------- */

static void cleanup_and_exit(int code)
{
    shadow_fini();
    fb_clear_mem();
    tty_restore();
    fb_cleanup();
    flist_print_tagged(stdout);
    exit(code);
}

int
main(int argc, char *argv[])
{
    int              timeout = 0;
    int              backup = 0;
    int              preserve = 0;

    struct ida_image *fimg    = NULL;
    struct ida_image *simg    = NULL;
    struct ida_image *img     = NULL;
    float            scale    = 1;
    float            newscale = 1;

    int              editable = 0, once = 0;
    int              need_read;
    int              i, arg, key;

    char             *info, *desc, *filelist;
    char             linebuffer[128];

#if 0
    /* debug aid, to attach gdb ... */ 
    fprintf(stderr,"pid %d\n",getpid());
    sleep(10);
#endif

    setlocale(LC_ALL,"");
#ifdef HAVE_LIBLIRC
    lirc = lirc_fbi_init();
#endif
    fbi_read_config();
    cfg_parse_cmdline(&argc,argv,fbi_cmd);
    cfg_parse_cmdline(&argc,argv,fbi_cfg);

    if (GET_AUTO_ZOOM()) {
	cfg_set_bool(O_AUTO_UP,   1);
	cfg_set_bool(O_AUTO_DOWN, 1);
    }

    if (GET_HELP()) {
	usage(argv[0]);
	exit(0);
    }
    if (GET_VERSION()) {
	version();
	exit(0);
    }
    if (GET_WRITECONF())
	fbi_write_config();

    once        = GET_ONCE();
    autoup      = GET_AUTO_UP();
    autodown    = GET_AUTO_DOWN();
    autofirst   = GET_AUTO_FIRST();
    fitwidth    = GET_FIT_WIDTH();
    statusline  = GET_VERBOSE();
    textreading = GET_TEXT_MODE();
    editable    = GET_EDIT();
    backup      = GET_BACKUP();
    preserve    = GET_PRESERVE();

    v_steps     = GET_SCROLL();
    h_steps     = GET_SCROLL();
    timeout     = GET_TIMEOUT();
    pcd_res     = GET_PCD_RES();

    fbgamma     = GET_GAMMA();

    fontname    = cfg_get_str(O_FONT);
    filelist    = cfg_get_str(O_FILE_LIST);
    
    if (filelist)
	flist_add_list(filelist);
    for (i = optind; i < argc; i++)
	flist_add(argv[i]);
    flist_renumber();

    if (0 == fcount) {
	usage(argv[0]);
	exit(1);
    }

    if (GET_RANDOM())
	flist_randomize();
    fcurrent = flist_first();
    need_read = 1;

    font_init();
    if (NULL == fontname)
	fontname = "monospace:size=16";
    face = font_open(fontname);
    if (NULL == face) {
	fprintf(stderr,"can't open font: %s\n",fontname);
	exit(1);
    }
    fd = fb_init(cfg_get_str(O_DEVICE),
		 cfg_get_str(O_VIDEO_MODE),
		 GET_VT());
    fb_catch_exit_signals();
    fb_switch_init();
    shadow_init();
    shadow_set_palette(fd);
    signal(SIGTSTP,SIG_IGN);
    
    if (textreading) {
	show_top = 1;
	text_steps = fb_var.yres - 100;
    }

    /* svga main loop */
    tty_raw();
    desc = NULL;
    info = NULL;
    scale = 1;
    for (;;) {
	if (need_read) {
	    need_read = 0;
	    free_image(fimg);
	    free_image(simg);
	    fimg = NULL;
	    simg = NULL;
	    img  = NULL;
	    snprintf(linebuffer,sizeof(linebuffer),"loading %s ...",fcurrent->name);
	    status_update(img,linebuffer, NULL);
	    fimg = read_image(fcurrent->name);
	    if (fimg) {
		if (autoup || autodown) {
		    scale = auto_scale(fimg);
		    if (scale < 1 && !autodown)
			scale = 1;
		    if (scale > 1 && !autoup)
			scale = 1;
		    if (autofirst)
			autoup = autodown = 0;
		}
		if (scale != 1) {
		    snprintf(linebuffer, sizeof(linebuffer),
			     "scaling (%.0f%%) %s ...",
			     scale*100, fcurrent->name);
		    status_update(img,linebuffer, NULL);
		    simg = scale_image(fimg,scale);
		    img = simg;
		} else {
		    img = fimg;
		}
		desc = make_desc(&fimg->i,fcurrent->name);
	    }
	    if (!img) {
		snprintf(linebuffer,sizeof(linebuffer),
			 "%s: FAILED",fcurrent->name);
		status_error(linebuffer);
	    }
	}
	if (img) {
	    int pages = ceil((float)img->i.height / fb_var.yres);
	    text_steps = ceil((float)img->i.height / pages);
	    info = make_info(fimg,scale);
	}
	switch (key = svga_show(img, timeout, desc, info, &arg)) {
	case KEY_DELETE:
	    if (editable) {
		struct flist *fdel = fcurrent;
		if (flist_islast(fcurrent))
		    fcurrent = flist_prev(fcurrent,0);
		else
		    fcurrent = flist_next(fcurrent,0,0);
		unlink(fdel->name);
		flist_del(fdel);
		flist_renumber();
		need_read = 1;
		if (list_empty(&flist)) {
		    /* deleted last one */
		    cleanup_and_exit(0);
		}
	    } else {
		status_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	case KEY_ROT_CW:
	case KEY_ROT_CCW:
	{
	    if (editable) {
		snprintf(linebuffer,sizeof(linebuffer),
			 "rotating %s ...",fcurrent->name);
		status_update(img, linebuffer, NULL);
		jpeg_transform_inplace
		    (fcurrent->name,
		     (key == KEY_ROT_CW) ? JXFORM_ROT_90 : JXFORM_ROT_270,
		     NULL,
		     NULL,0,
		     (backup   ? JFLAG_FILE_BACKUP    : 0) | 
		     (preserve ? JFLAG_FILE_KEEP_TIME : 0) | 
		     JFLAG_TRANSFORM_IMAGE     |
		     JFLAG_TRANSFORM_THUMBNAIL |
		     JFLAG_UPDATE_ORIENTATION);
		need_read = 1;
	    } else {
		status_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	}
	case KEY_TAGFILE:
	    fcurrent->tag = !fcurrent->tag;
	    /* fall throuth */
	case KEY_SPACE:
	    need_read = 1;
	    fcurrent = flist_next(fcurrent,1,0);
	    if (textreading)
		show_top = 1;
	    if (NULL != fcurrent)
		break;
	    /* else fall */
	case KEY_ESC:
	case KEY_Q:
	case KEY_EOF:
	    cleanup_and_exit(0);
	    break;
	case KEY_PGDN:
	    need_read = 1;
	    fcurrent = flist_next(fcurrent,0,1);
	    if (textreading)
		show_top = 1;
	    break;
	case KEY_PGUP:
	    need_read = 1;
	    fcurrent = flist_prev(fcurrent,1);
	    if (textreading)
		show_bottom = 1;
	    break;
	case KEY_TIMEOUT:
	    need_read = 1;
	    fcurrent = flist_next(fcurrent,once,1);
	    if (NULL == fcurrent) {
		cleanup_and_exit(0);
	    }
	    /* FIXME: wrap around */
	    break;
	case KEY_PLUS:
	case KEY_MINUS:
	case KEY_ASCALE:
	case KEY_SCALE:
	    if (key == KEY_PLUS) {
		newscale = scale * 1.6;
	    } else if (key == KEY_MINUS) {
		newscale = scale / 1.6;
	    } else if (key == KEY_ASCALE) {
		newscale = auto_scale(fimg);
	    } else {
		newscale = arg / 100.0;
	    }
	    if (newscale < 0.1)
		newscale = 0.1;
	    if (newscale > 10)
		newscale = 10;
	    scale_fix_top_left(scale, newscale, img);
	    scale = newscale;
	    snprintf(linebuffer,sizeof(linebuffer),
		     "scaling (%.0f%%) %s ...",
		     scale*100, fcurrent->name);
	    status_update(NULL, linebuffer, NULL);
	    free_image(simg);
	    simg = scale_image(fimg,scale);
	    img = simg;
	    break;
	case KEY_GOTO:
	    if (arg > 0 && arg <= fcount) {
		need_read = 1;
		fcurrent = flist_goto(arg);
	    }
	    break;
	case KEY_DELAY:
	    timeout = arg;
	    break;
	case KEY_VERBOSE:
#if 0 /* fbdev testing/debugging hack */
	    {
		ioctl(fd,FBIOBLANK,1);
		sleep(1);
		ioctl(fd,FBIOBLANK,0);
	    }
#endif
	    statusline = !statusline;
	    break;
	case KEY_DESC:
	    if (!comments) {
		edit_desc(img, fcurrent->name);
		desc = make_desc(&fimg->i,fcurrent->name);
	    }
	    break;
	}
    }
}
