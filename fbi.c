/*
 * image viewer, for framebuffer devices
 *
 *   (c) 1998-2002 Gerd Knorr <kraxel@bytesex.org>
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
int             dither = FALSE, pcd_res = 3, steps = 50;
int             textreading = 0, redraw = 0, statusline = 1;
int             new_image;
int             left, top;

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

int autodown = 0;
int autoup   = 0;
int comments = 0;

struct option fbi_options[] = {
    {"version",    no_argument,       NULL, 'V'},  /* version */
    {"help",       no_argument,       NULL, 'h'},  /* help */
    {"device",     required_argument, NULL, 'd'},  /* device */
    {"mode",       required_argument, NULL, 'm'},  /* video mode */
    {"gamma",      required_argument, NULL, 'g'},  /* set gamma */
    {"quiet",      no_argument,       NULL, 'q'},  /* quiet */
    {"verbose",    no_argument,       NULL, 'v'},  /* verbose */
    {"scroll",     required_argument, NULL, 's'},  /* set scrool */
    {"timeout",    required_argument, NULL, 't'},  /* timeout value */
    {"once",       no_argument,       NULL, '1'},  /* loop only once */
    {"resolution", required_argument, NULL, 'r'},  /* select resolution */
    {"random",     no_argument,       NULL, 'u'},  /* randomize images */
    {"font",       required_argument, NULL, 'f'},  /* font */
    {"autozoom",   no_argument,       NULL, 'a'},
    {"edit",       no_argument,       NULL, 'e'},  /* enable editing */
    {"list",       required_argument, NULL, 'l'},
    {"vt",         required_argument, NULL, 'T'},
    {"backup",     no_argument,       NULL, 'b'},
    {"preserve",   no_argument,       NULL, 'p'},

    /* long-only options */
    {"autoup",     no_argument,       &autoup,   1 },
    {"autodown",   no_argument,       &autodown, 1 },
    {"comments",   no_argument,       &comments, 1 },
    {0,0,0,0}
};

/* font handling */
static char *fontname = NULL;

/* ---------------------------------------------------------------------- */

static void
version(void)
{
    fprintf(stderr, "fbi version " VERSION
	    " (c) 1999-2003 Gerd Knorr; compiled on %s.\n", __DATE__ );
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
	    "  Usage: %s [ options ] file1 file2 ... fileN\n"
	    "\n"
	    "    --help       [-h]      Print this text\n"
	    "    --version    [-V]      Show the fbi version number\n"
	    "    --device     [-d] dev  Framebuffer device [%s]\n"
	    "    --mode       [-m] mode Video mode (must be listed in /etc/fb.modes)\n"
	    "                           - Default is current mode.\n"
	    "    --gamma      [-g] f    Set gamma\n"
	    "    --scroll     [-s] n    Set scroll steps in pixels (default: 50)\n"
	    "    --quiet      [-q]      don't print anything at all\n"
	    "    --verbose    [-v]      show print filenames all the time\n"
	    "    --timeout    [-t] n    Load next image after N sec without any keypress\n"
	    "    --once       [-1]      Don't loop (for use with -t).\n"
	    "    --resolution [-r] n    Select resolution [1..5] (PhotoCD)\n"
	    "    --random     [-u]      Show file1 .. fileN in a random order\n"
	    "    --font       [-f] fn   Use font fn (either console psf file or\n"
	    "                           X11 font spec if a font server is available\n"
	    "    --autozoom   [-a]      Automagically pick useful zoom factor.\n"
	    "      --autoup             Like the above, but upscale only.\n"
	    "      --autodown           Like the above, but downscale only.\n"
	    "    --edit       [-e]      enable editing commands (see man page).\n"
	    "      --backup   [-b]      create backup files when editing.\n"
	    "      --preserve [-p]      preserve timestamps when editing.\n"
	    "    --list       [-l] file read list of images from file\n"
	    "    --comments             display image comments\n"
	    "    --vt         [-T] vt   start on console #vt\n"
	    "\n"
	    "Large images can be scrolled using the cursor keys.  Zoom in/out\n"
	    "works with '+' and '-'.  Use ESC or 'q' to quit.  Space and PgDn\n"
	    "show the next, PgUp shows the previous image. Jumping to a image\n"
	    "works with <number>g.  Return acts like Space but additionally\n"
	    "prints the filename of the currently displayed image to stdout.\n"
	    "\n",
	    name, fbdev ? fbdev : "/dev/fb0");
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

static struct flist* flist_prev(struct flist *f)
{
    if (flist_isfirst(f))
	return f;
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

static void status(unsigned char *desc, char *info)
{
    int chars, ilen;
    char *str;
    
    if (!statusline)
	return;
    chars = fb_var.xres / fb_font_width();
    str = malloc(chars+1);
    if (info) {
	ilen = strlen(info);
	sprintf(str, "%-*.*s [ %s ] H - Help",
		chars-14-ilen, chars-14-ilen, desc, info);
    } else {
	sprintf(str, "%-*.*s | H - Help", chars-11, chars-11, desc);
    }
    fb_status_line(str);
    free(str);
}

static void show_error(unsigned char *msg)
{
    fb_status_line(msg);
    sleep(2);
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
    char *linebuffer[ARRAY_SIZE(tags)];

    if (!visible)
	return;

    ed = exif_data_new_from_file(f->name);
    if (NULL == ed) {
	status("image has no EXIF data", NULL);
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
	value[tag] = strdup(exif_entry_get_value(ee));
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
	linebuffer[count] = malloc(l1+l2+8);
	sprintf(linebuffer[count],"%-*.*s : %-*.*s",
		l1, l1, title[tag],
		l2, l2, value[tag]);
	count++;
    }
    fb_text_box(24,16,linebuffer,count);

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
    static char *help[] = {
	"keyboard commands",
	"~~~~~~~~~~~~~~~~~",
	"  ESC, Q      - quit",
	"  pgdn, space - next image",
	"  pgup        - previous image",
	"  +/-         - zoom in/out",
	"  A           - autozoom image",
	"  cursor keys - scroll image",
	"",
	"  H           - show this help text",
	"  I           - show EXIF info",
	"  P           - pause slideshow",
	"  V           - toggle statusline",
	"",
	"available if started with --edit switch,",
	"rotation works for jpeg images only:",
	"  shift+D     - delete image",
	"  R           - rotate clockwise",
	"  L           - rotate counter-clockwise",
    };

    fb_text_box(24,16,help,ARRAY_SIZE(help));
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
	len += sprintf(linebuffer+len, "%s%c",
		       key[i] < 0x20 ? "^" : "",
		       key[i] < 0x20 ? key[i] + 0x40 : key[i]);
    status(linebuffer, NULL);
}

static void
console_switch(int is_busy)
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
	redraw = 1;
	ioctl(fd,FBIOPAN_DISPLAY,&fb_var);
	fb_clear_screen();
	if (is_busy)
	    status("busy, please wait ...", NULL);		
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
	sprintf(command,"convert -depth 8 \"%s\" ppm:-",filename);
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
	    console_switch(1);
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
	    console_switch(1);
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
    ys = (float)fb_var.yres / img->i.height;
    scale = (xs < ys) ? xs : ys;
    return scale;
}

/* ---------------------------------------------------------------------- */

static unsigned char *
convert_line(int bpp, int line, int owidth,
	     char unsigned *dest, char unsigned *buffer)
{
    unsigned char  *ptr  = (void*)dest;
    unsigned short *ptr2 = (void*)dest;
    unsigned long  *ptr4 = (void*)dest;
    int x;

    switch (fb_var.bits_per_pixel) {
    case 8:
	dither_line(buffer, ptr, line, owidth);
	ptr += owidth;
	return ptr;
    case 15:
    case 16:
	for (x = 0; x < owidth; x++) {
	    ptr2[x] = lut_red[buffer[x*3]] |
		lut_green[buffer[x*3+1]] |
		lut_blue[buffer[x*3+2]];
	}
	ptr2 += owidth;
	return (char*)ptr2;
    case 24:
	for (x = 0; x < owidth; x++) {
	    ptr[3*x+2] = buffer[3*x+0];
	    ptr[3*x+1] = buffer[3*x+1];
	    ptr[3*x+0] = buffer[3*x+2];
	}
	ptr += owidth * 3;
	return ptr;
    case 32:
	for (x = 0; x < owidth; x++) {
	    ptr4[x] = lut_red[buffer[x*3]] |
		lut_green[buffer[x*3+1]] |
		lut_blue[buffer[x*3+2]];
	}
	ptr4 += owidth;
	return (char*)ptr4;
    default:
	/* keep compiler happy */
	return NULL;
    }
}

/* ---------------------------------------------------------------------- */

static void init_one(int32_t *lut, int bits, int shift)
{
    int i;
    
    if (bits > 8)
	for (i = 0; i < 256; i++)
	    lut[i] = (i << (bits + shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut[i] = (i >> (8 - bits)) << shift;
}

static void
lut_init(int depth)
{
    if (fb_var.red.length   &&
	fb_var.green.length &&
	fb_var.blue.length) {
	/* fb_var.{red|green|blue} looks sane, use it */
	init_one(lut_red,   fb_var.red.length,   fb_var.red.offset);
	init_one(lut_green, fb_var.green.length, fb_var.green.offset);
	init_one(lut_blue,  fb_var.blue.length,  fb_var.blue.offset);
    } else {
	/* fallback */
	int i;
	switch (depth) {
	case 15:
	    for (i = 0; i < 256; i++) {
		lut_red[i]   = (i & 0xf8) << 7;	/* bits -rrrrr-- -------- */
		lut_green[i] = (i & 0xf8) << 2;	/* bits ------gg ggg----- */
		lut_blue[i]  = (i & 0xf8) >> 3;	/* bits -------- ---bbbbb */
	    }
	    break;
	case 16:
	    for (i = 0; i < 256; i++) {
		lut_red[i]   = (i & 0xf8) << 8;	/* bits rrrrr--- -------- */
		lut_green[i] = (i & 0xfc) << 3;	/* bits -----ggg ggg----- */
		lut_blue[i]  = (i & 0xf8) >> 3;	/* bits -------- ---bbbbb */
	    }
	    break;
	case 24:
	    for (i = 0; i < 256; i++) {
		lut_red[i]   = i << 16;	/* byte -r-- */
		lut_green[i] = i << 8;	/* byte --g- */
		lut_blue[i]  = i;		/* byte ---b */
	    }
	    break;
	}
    }
}

static unsigned short calc_gamma(int n, int max)
{
    int ret =65535.0 * pow((float)n/(max), 1 / fbgamma); 
    if (ret > 65535) ret = 65535;
    if (ret <     0) ret =     0;
    return ret;
}

static void
linear_palette(int bit)
{
    int i, size = 256 >> (8 - bit);
    
    for (i = 0; i < size; i++)
        red[i] = green[i] = blue[i] = calc_gamma(i,size);
}

static void
svga_dither_palette(int r, int g, int b)
{
    int             rs, gs, bs, i;

    rs = 256 / (r - 1);
    gs = 256 / (g - 1);
    bs = 256 / (b - 1);
    for (i = 0; i < 256; i++) {
	red[i]   = calc_gamma(rs * ((i / (g * b)) % r), 255);
	green[i] = calc_gamma(gs * ((i / b) % g),       255);
	blue[i]  = calc_gamma(bs * ((i) % b),           255);
    }
}

static void
svga_display_image(struct ida_image *img, int xoff, int yoff)
{
    unsigned int     dwidth  = MIN(img->i.width,  fb_var.xres);
    unsigned int     dheight = MIN(img->i.height, fb_var.yres);
    unsigned int     data, video, bank, offset, bytes, y;

    if (!visible)
	return;
    bytes = (fb_var.bits_per_pixel+7)/8;

    /* offset for image data (image > screen, select visible area) */
    offset = (yoff * img->i.width + xoff) * 3;

    /* offset for video memory (image < screen, center image) */
    video = 0, bank = 0;
    if (img->i.width < fb_var.xres)
	video += bytes * ((fb_var.xres - img->i.width) / 2);
    if (img->i.height < fb_var.yres)
	video += fb_fix.line_length * ((fb_var.yres - img->i.height) / 2);

    /* go ! */
    for (data = 0, y = 0;
	 data < img->i.width * img->i.height * 3
	     && data / img->i.width / 3 < dheight;
	 data += img->i.width * 3, video += fb_fix.line_length) {
	convert_line(fb_var.bits_per_pixel, y++, dwidth,
		     fb_mem+video, img->data + data + offset);
    }
}

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
	if (img->i.height > fb_var.yres && !textreading)
	    top = (img->i.height - fb_var.yres) / 2;
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
	    svga_display_image(img, left, top);
	    status(desc, info);
	}
        if (switch_last != fb_switch_state) {
	    console_switch(0);
	    continue;
	}
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
		    (-1 != timeout && !paused) ? &limit : NULL);
        if (switch_last != fb_switch_state) {
	    console_switch(0);
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
		top += (fb_var.yres-100);
	    } else {
		return KEY_SPACE;
	    }

	} else if (0 == strcmp(key, "\x1b[A") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top -= steps;
	} else if (0 == strcmp(key, "\x1b[B") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top += steps;
	} else if (0 == strcmp(key, "\x1b[1~") && img->i.height > fb_var.yres) {
	    redraw = 1;
	    top = 0;
	} else if (0 == strcmp(key, "\x1b[4~")) {
	    redraw = 1;
	    top = img->i.height - fb_var.yres;
	} else if (0 == strcmp(key, "\x1b[D") && img->i.width > fb_var.xres) {
	    redraw = 1;
	    left -= steps;
	} else if (0 == strcmp(key, "\x1b[C") && img->i.width > fb_var.xres) {
	    redraw = 1;
	    left += steps;

	} else if (0 == strcmp(key, "\x1b[5~")) {
	    return KEY_PGUP;
	} else if (0 == strcmp(key, "\x1b[6~") ||
		   0 == strcmp(key, "n")       ||
		   0 == strcmp(key, "N")) {
	    return KEY_PGDN;
	    
	} else if (0 == strcmp(key, "+")) {
	    return KEY_PLUS;
	} else if (0 == strcmp(key, "-")) {
	    return KEY_MINUS;
	} else if (0 == strcmp(key, "a") ||
		   0 == strcmp(key, "A")) {
	    return KEY_ASCALE;
	    
	} else if (0 == strcmp(key, "p") ||
		   0 == strcmp(key, "P")) {
	    if (-1 != timeout) {
		paused = !paused;
		status(paused ? "pause on " : "pause off", NULL);
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
	} else if (rc == 1 && *key >= '0' && *key <= '9') {
	    *nr = *nr * 10 + (*key - '0');
	    sprintf(linebuffer, "> %d",*nr);
	    status(linebuffer, NULL);
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

static char edit_line(char *line, int max)
{
    int      len = strlen(line);
    int      pos = len;
    int      rc;
    char     key[11];
    fd_set  set;

    do {
	fb_edit_line(line,pos);

	FD_SET(0, &set);
	rc = select(1, &set, NULL, NULL, NULL);
        if (switch_last != fb_switch_state) {
	    console_switch(0);
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

static void edit_desc(char *filename)
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
    rc = edit_line(linebuffer, sizeof(linebuffer)-1);
    if (0 != rc)
	return;
    desktop_write_entry(desc, "Directory", "Comment=", linebuffer);
}

/* ---------------------------------------------------------------------- */

static void cleanup_and_exit(int code)
{
    fb_clear_mem();
    tty_restore();
    fb_cleanup();
    flist_print_tagged(stdout);
    exit(code);
}

int
main(int argc, char *argv[])
{
    int              timeout = -1;
    int              randomize = -1;
    int              opt_index = 0;
    int              vt = 0;
    int              backup = 0;
    int              preserve = 0;

    struct ida_image *fimg    = NULL;
    struct ida_image *simg    = NULL;
    struct ida_image *img     = NULL;
    float            scale    = 1;
    float            newscale = 1;

    int              c, editable = 0, once = 0;
    int              need_read, need_refresh;
    int              i, arg, key;

    char             *line, *info, *desc;
    char             linebuffer[128];

    if (NULL != (line = getenv("FRAMEBUFFER")))
	fbdev = line;
    if (NULL != (line = getenv("FBGAMMA")))
        fbgamma = atof(line);
    if (NULL != (line = getenv("FBFONT")))
	fontname = line;

#ifdef HAVE_LIBLIRC
    lirc = lirc_fbi_init();
#endif

    setlocale(LC_ALL,"");
    for (;;) {
	c = getopt_long(argc, argv, "u1evahPqVbpr:t:m:d:g:s:f:l:T:",
			fbi_options, &opt_index);
	if (c == -1)
	    break;
	switch (c) {
	case 0:
	    /* long option, nothing to do */
	    break;
	case '1':
	    once = 1;
	    break;
	case 'a':
	    autoup   = 1;
	    autodown = 1;
	    break;
	case 'q':
	    statusline = 0;
	    break;
	case 'v':
	    statusline = 1;
	    break;
	case 'P':
	    textreading = 1;
	    break;
	case 'g':
	    fbgamma = atof(optarg);
	    break;
	case 'r':
	    pcd_res = atoi(optarg);
	    break;
	case 's':
	    steps = atoi(optarg);
	    break;
	case 't':
	    timeout = atoi(optarg);
	    break;
	case 'u':
	    randomize = 1;
	    break;
	case 'd':
	    fbdev = optarg;
	    break;
	case 'm':
	    fbmode = optarg;
	    break;
	case 'f':
	    fontname = optarg;
	    break;
	case 'e':
	    editable = 1;
	    break;
	case 'b':
	    backup = 1;
	    break;
	case 'p':
	    preserve = 1;
	    break;
	case 'l':
	    flist_add_list(optarg);
	    break;
	case 'T':
	    vt = atoi(optarg);
	    break;
	case 'V':
	    version();
	    exit(0);
	    break;
	default:
	case 'h':
	    usage(argv[0]);
	    exit(1);
	}
    }

    for (i = optind; i < argc; i++) {
	flist_add(argv[i]);
    }
    flist_renumber();

    if (0 == fcount) {
	usage(argv[0]);
	exit(1);
    }

    if (randomize != -1)
	flist_randomize();
    fcurrent = flist_first();

    need_read = 1;
    need_refresh = 1;

    fb_text_init1(fontname);
    fd = fb_init(fbdev, fbmode, vt);
    fb_catch_exit_signals();
    fb_switch_init();
    signal(SIGTSTP,SIG_IGN);
    fb_text_init2();
    
    switch (fb_var.bits_per_pixel) {
    case 8:
	svga_dither_palette(8, 8, 4);
	dither = TRUE;
	init_dither(8, 8, 4, 2);
	break;
    case 15:
    case 16:
        if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
            linear_palette(5);
	if (fb_var.green.length == 5) {
	    lut_init(15);
	} else {
	    lut_init(16);
	}
	break;
    case 24:
        if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
            linear_palette(8);
	break;
    case 32:
        if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
            linear_palette(8);
	lut_init(24);
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		fb_var.bits_per_pixel);
	exit(1);
    }
    if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR ||
	fb_var.bits_per_pixel == 8) {
	if (-1 == ioctl(fd,FBIOPUTCMAP,&cmap)) {
	    perror("ioctl FBIOPUTCMAP");
	    exit(1);
	}
    }

    /* svga main loop */
    tty_raw();
    desc = NULL;
    info = NULL;
    for (;;) {
	if (need_read) {
	    need_read = 0;
	    need_refresh = 1;
	    sprintf(linebuffer,"loading %s ...",fcurrent->name);
	    status(linebuffer, NULL);
	    free_image(fimg);
	    free_image(simg);
	    fimg = read_image(fcurrent->name);
	    simg = NULL;
	    img  = NULL;
	    scale = 1;
	    if (fimg) {
		if (autoup || autodown) {
		    scale = auto_scale(fimg);
		    if (scale < 1 && !autodown)
			scale = 1;
		    if (scale > 1 && !autoup)
			scale = 1;
		}
		if (scale != 1) {
		    sprintf(linebuffer,"scaling (%.0f%%) %s ...",
			    scale*100, fcurrent->name);
		    status(linebuffer, NULL);
		    simg = scale_image(fimg,scale);
		    img = simg;
		} else {
		    img = fimg;
		}
		desc = make_desc(&fimg->i,fcurrent->name);
	    }
	    if (!img) {
		sprintf(linebuffer,"%s: FAILED",fcurrent->name);
		show_error(linebuffer);
	    }
	}
	if (img) {
	    if (need_refresh) {
		need_refresh = 0;
		if (img->i.width < fb_var.xres || img->i.height < fb_var.yres)
		    fb_clear_screen();
	    }
	    info = make_info(fimg,scale);
	}
	switch (key = svga_show(img, timeout, desc, info, &arg)) {
	case KEY_DELETE:
	    if (editable) {
		struct flist *fdel = fcurrent;
		if (flist_islast(fcurrent))
			fcurrent = flist_prev(fcurrent);
		else
			fcurrent = flist_next(fcurrent,0,0);
		unlink(fdel->name);
		flist_del(fdel);
		flist_renumber();
		need_read = 1;
		if (list_empty(&flist)) {
		    /* deleted last one */
		    fb_clear_mem();
		    tty_restore();
		    fb_cleanup();
		    exit(0);
		}
	    } else {
		show_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	case KEY_ROT_CW:
	case KEY_ROT_CCW:
	{
	    if (editable) {
		sprintf(linebuffer,"rotating %s ...",fcurrent->name);
		status(linebuffer, NULL);
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
		show_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	}
	case KEY_TAGFILE:
	    fcurrent->tag = !fcurrent->tag;
	    /* fall throuth */
	case KEY_SPACE:
	    need_read = 1;
	    fcurrent = flist_next(fcurrent,1,0);
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
	    fcurrent = flist_next(fcurrent,0,0);
	    break;
	case KEY_PGUP:
	    need_read = 1;
	    fcurrent = flist_prev(fcurrent);
	    break;
	case KEY_TIMEOUT:
	    need_read = 1;
	    fcurrent = flist_next(fcurrent,once,1);
	    if (NULL == fcurrent) {
		fb_clear_mem();
		tty_restore();
		fb_cleanup();
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
		newscale = arg / 100;
	    }
	    if (newscale < 0.1)
		newscale = 0.1;
	    if (newscale > 10)
		newscale = 10;
	    scale_fix_top_left(scale, newscale, img);
	    scale = newscale;
	    sprintf(linebuffer,"scaling (%.0f%%) %s ...",
		    scale*100, fcurrent->name);
	    status(linebuffer, NULL);
	    free_image(simg);
	    simg = scale_image(fimg,scale);
	    img = simg;
	    need_refresh = 1;
	    break;
	case KEY_GOTO:
	    if (arg > 0 && arg <= fcount) {
		need_read = 1;
		fcurrent = flist_goto(arg);
	    }
	    break;
	case KEY_VERBOSE:
	    statusline = !statusline;
	    need_refresh = 1;
	    break;
	case KEY_DESC:
	    if (!comments) {
		edit_desc(fcurrent->name);
		desc = make_desc(&fimg->i,fcurrent->name);
	    }
	    break;
	}
    }
}
