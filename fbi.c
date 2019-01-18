/*
 * image viewer, for framebuffer devices
 *
 *   (c) 1998-2012 Gerd Hoffmann <gerd@kraxel.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <math.h>
#include <signal.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/fb.h>

#include <jpeglib.h>

#include <libexif/exif-data.h>

#include "readers.h"
#include "vt.h"
#include "kbd.h"
#include "fbtools.h"
#include "drmtools.h"
#include "fb-gui.h"
#include "filter.h"
#include "desktop.h"
#include "fbiconfig.h"

#include "transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"

#define TRUE            1
#define FALSE           0
#undef  MAX
#define MAX(x,y)        ((x)>(y)?(x):(y))
#undef  MIN
#define MIN(x,y)        ((x)<(y)?(x):(y))
#define ARRAY_SIZE(x)   (sizeof(x)/sizeof(x[0]))

/* ---------------------------------------------------------------------- */

/* variables for read_image */
int32_t         lut_red[256], lut_green[256], lut_blue[256];
int             pcd_res = 3;
int             v_steps = 50;
int             h_steps = 50;
int             textreading = 0, redraw = 0, statusline = 1;
int             fitwidth;

/* file list */
struct flist {
    /* file list */
    int               nr;
    int               tag;
    char              *name;
    struct list_head  list;
    bool              list_check;

    /* image cache */
    int               seen;
    int               top;
    int               left;
    int               text_steps;
    float             scale;
    struct ida_image  *fimg;
    struct ida_image  *simg;
    struct list_head  lru;
};
static LIST_HEAD(flist);
static LIST_HEAD(flru);
static int           fcount;
static struct flist  *fcurrent;
static struct ida_image *img;
static const char *filelist;
static struct stat liststat;

/* accounting */
static int img_cnt, min_cnt = 2, max_cnt = 16;
static int img_mem, max_mem_mb;

/* graphics interface */
gfxstate                   *gfx;
int                        debug;

/* framebuffer */
char                       *fbdev = NULL;
char                       *fbmode  = NULL;

unsigned short red[256],  green[256],  blue[256];
struct fb_cmap cmap  = { 0, 256, red,  green,  blue };

/* Command line options. */
int autodown;
int autoup;
int comments;
int transparency = 40;
int timeout;
int backup;
int preserve;
int read_ahead;
int editable;
int blend_msecs;
int perfmon = 0;
int interactive = 0;

/* font handling */
static char *fontname = NULL;
static FT_Face face;

/* ---------------------------------------------------------------------- */
/* fwd declarations                                                       */

static struct ida_image *flist_img_get(struct flist *f);
static void flist_img_load(struct flist *f, int prefetch);
static void flist_img_free(struct flist *f);

/* ---------------------------------------------------------------------- */

static void
version(void)
{
    fprintf(stdout,
	    "fbi version " VERSION ", compiled on %s\n"
	    "(c) 1998-2016 Gerd Hoffmann <gerd@kraxel.org>\n",
	    __DATE__ );
}

static void
usage(FILE *fp, char *name)
{
    char           *h;

    if (NULL != (h = strrchr(name, '/')))
	name = h+1;
    fprintf(fp,
	    "\n"
	    "This program displays images using the Linux framebuffer device.\n"
	    "Supported formats: PhotoCD, jpeg, ppm, gif, tiff, xwd, bmp, png,\n"
	    "webp. It tries to use ImageMagick's convert for unknown file formats.\n"
	    "\n"
	    "usage: %s [ options ] file1 file2 ... fileN\n"
	    "\n",
	    name);

    cfg_help_cmdline(fp,fbi_cmd,4,20,0);
    cfg_help_cmdline(fp,fbi_cfg,4,20,40);

    fprintf(fp,
	    "\n"
	    "Large images can be scrolled using the cursor keys.  Zoom in/out\n"
	    "works with '+' and '-'.  Use ESC or 'q' to quit.  Space and PgDn\n"
	    "show the next, PgUp shows the previous image. Jumping to a image\n"
	    "works with <i>g.  Return acts like Space but additionally prints\n"
	    "prints the filename of the currently displayed image to stdout.\n"
	    "\n");
}

/* ---------------------------------------------------------------------- */

static struct flist *flist_add(const char *filename)
{
    struct flist *f;

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));
    f->name = strdup(filename);
    list_add_tail(&f->list,&flist);
    INIT_LIST_HEAD(&f->lru);
    return f;
}

static struct flist *flist_find(const char *filename)
{
    struct list_head *item;
    struct flist *f;

    list_for_each(item,&flist) {
	f = list_entry(item, struct flist, list);
        if (strcmp(filename, f->name) == 0)
            return f;
    }
    return NULL;
}

static int flist_add_list(const char *listfile)
{
    char filename[256];
    struct flist *f;
    FILE *list;

    list = fopen(listfile,"r");
    if (NULL == list) {
	fprintf(stderr,"open %s: %s\n",listfile,strerror(errno));
	return -1;
    }
    fstat(fileno(list), &liststat);
    while (NULL != fgets(filename,sizeof(filename)-1,list)) {
	size_t off = strcspn(filename,"\r\n");
	if (off)
	    filename[off] = 0;
        f = flist_find(filename);
        if (!f) {
            f = flist_add(filename);
        }
        f->list_check = true;
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

static int flist_check_reload_list(const char *listfile)
{
    struct list_head *item, *safe;
    struct flist *f;
    struct stat st;
    int ret;

    ret = stat(listfile, &st);
    if (ret < 0)
        return ret;

    if (st.st_mtime == liststat.st_mtime)
        return 0;

    ret = flist_add_list(listfile);
    if (ret != 0)
        return ret;

    list_for_each_safe(item, safe, &flist) {
	f = list_entry(item, struct flist, list);
        if (f->list_check) {
            f->list_check = false;
        } else {
            flist_img_free(f);
            flist_del(f);
        }
    }
    flist_renumber();
    return 0;
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
		  unsigned int first, unsigned int last, int weight)
{
    unsigned int     dwidth  = MIN(img->i.width,  gfx->hdisplay);
    unsigned int     dheight = MIN(img->i.height, gfx->vdisplay);
    unsigned int     data, offset, y, xs, ys;

    if (100 == weight)
	shadow_clear_lines(first, last);
    else
	shadow_darkify(0, gfx->hdisplay-1, first, last, 100 - weight);

    /* offset for image data (image > screen, select visible area) */
    offset = (yoff * img->i.width + xoff) * 3;

    /* offset for video memory (image < screen, center image) */
    xs = 0, ys = 0;
    if (img->i.width < gfx->hdisplay)
	xs += (gfx->hdisplay - img->i.width) / 2;
    if (img->i.height < gfx->vdisplay)
	ys += (gfx->vdisplay - img->i.height) / 2;

    /* go ! */
    for (data = 0, y = 0;
	 data < img->i.width * img->i.height * 3
	     && data / img->i.width / 3 < dheight;
	 data += img->i.width * 3, y++) {
	if (ys+y < first)
	    continue;
	if (ys+y > last)
	    continue;
	if (100 == weight)
	  shadow_draw_rgbdata(xs, ys+y, dwidth,
			      ida_image_scanline(img, y) + offset);
	else
	  shadow_merge_rgbdata(xs, ys+y, dwidth, weight,
                               ida_image_scanline(img, y) + offset);
    }
}

static void status_prepare(void)
{
    struct ida_image *img = flist_img_get(fcurrent);
    int y1 = gfx->vdisplay - (face->size->metrics.height >> 6);
    int y2 = gfx->vdisplay - 1;

    if (img) {
	shadow_draw_image(img, fcurrent->left, fcurrent->top, y1, y2, 100);
	shadow_darkify(0, gfx->hdisplay-1, y1, y2, transparency);
    } else {
	shadow_clear_lines(y1, y2);
    }
    shadow_draw_line(0, gfx->hdisplay-1, y1-1, y1-1);
}

static void status_update(unsigned char *desc, char *info)
{
    int yt = gfx->vdisplay + (face->size->metrics.descender >> 6);
    wchar_t str[128];

    if (!statusline)
	return;
    status_prepare();

    swprintf(str,ARRAY_SIZE(str),L"%s",desc);
    shadow_draw_string(face, 0, yt, str, -1);
    if (info) {
	swprintf(str,ARRAY_SIZE(str), L"[ %s ] H - Help", info);
    } else {
	swprintf(str,ARRAY_SIZE(str), L"| H - Help");
    }
    shadow_draw_string(face, gfx->hdisplay, yt, str, 1);

    shadow_render(gfx);
}

static void status_error(unsigned char *msg)
{
    int yt = gfx->vdisplay + (face->size->metrics.descender >> 6);
    wchar_t str[128];

    status_prepare();

    swprintf(str,ARRAY_SIZE(str), L"%s", msg);
    shadow_draw_string(face, 0, yt, str, -1);

    shadow_render(gfx);
    sleep(2);
}

static void status_edit(unsigned char *msg, int pos)
{
    int yt = gfx->vdisplay + (face->size->metrics.descender >> 6);
    wchar_t str[128];

    status_prepare();

    swprintf(str,ARRAY_SIZE(str), L"%s", msg);
    shadow_draw_string_cursor(face, 0, yt, str, pos);

    shadow_render(gfx);
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

	0xa002, // Pixel X Dimension
	0xa003, // Pixel Y Dimension
    };
    ExifData   *ed;
    ExifEntry  *ee;
    unsigned int tag,l1,l2,len,count,i;
    const char *title[ARRAY_SIZE(tags)];
    char *value[ARRAY_SIZE(tags)];
    wchar_t *linebuffer[ARRAY_SIZE(tags)];

    if (!console_visible)
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
	value[tag] = malloc(128);
	exif_entry_get_value(ee, value[tag], 128);
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
    shadow_render(gfx);

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
	L"  cursor keys    - scroll image",
	L"  PgUp, k        - previous image",
	L"  PgDn, SPACE, j - next image",
	L"  <i>g           - jump to image #i",
	L"",
	L"  a              - autozoom image",
	L"  +/-            - zoom in/out",
	L"  <i>s           - set zoom to <i>%",
	L"",
	L"  ESC, q         - quit",
	L"  v              - toggle statusline",
	L"  h              - show this help text",
	L"  i              - show EXIF info",
	L"  p              - pause slideshow",
	L"",
	L"available if started with --edit switch,",
	L"rotation works for jpeg images only:",
	L"  D, Shift+d     - delete image",
	L"  r              - rotate 90 degrees clockwise",
	L"  l              - rotate 90 degrees counter-clockwise",
	L"  x              - mirror image vertically (top / bottom)",
	L"  y              - mirror image horizontally (left to right)",
    };

    shadow_draw_text_box(face, 24, 16, transparency,
			 help, ARRAY_SIZE(help));
    shadow_render(gfx);
}

/* ---------------------------------------------------------------------- */

static void free_image(struct ida_image *img)
{
    if (img) {
	if (img->p) {
	    img_mem -= img->i.width * img->i.height * 3;
	    ida_image_free(img);
	}
	free(img);
    }
}

static struct ida_image*
read_image(char *filename)
{
    struct ida_loader *loader = NULL;
    struct ida_image *img;
    struct list_head *item;
    char blk[512];
    FILE *fp;
    unsigned int y;
    void *data;

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
	int p[2];

	if (0 != pipe(p))
	    return NULL;
	switch (fork()) {
	case -1: /* error */
	    perror("fork");
	    close(p[0]);
	    close(p[1]);
	    return NULL;
	case 0: /* child */
	    dup2(p[1], 1 /* stdout */);
	    close(p[0]);
	    close(p[1]);
	    execlp("convert", "convert", "-depth", "8", filename, "ppm:-", NULL);
	    exit(1);
	default: /* parent */
	    close(p[1]);
	    fp = fdopen(p[0], "r");
	    if (NULL == fp)
		return NULL;
	    loader = &ppm_loader;
	}
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
    ida_image_alloc(img);
    img_mem += img->i.width * img->i.height * 3;
    for (y = 0; y < img->i.height; y++) {
        check_console_switch();
	loader->read(ida_image_scanline(img, y), y, data);
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
    ida_image_alloc(dest);
    img_mem += dest->i.width * dest->i.height * 3;
    for (y = 0; y < dest->i.height; y++) {
        check_console_switch();
	desc_resize.work(src, &rect, ida_image_scanline(dest, y), y, data);
    }
    desc_resize.done(data);
    return dest;
}

static float auto_scale(struct ida_image *img)
{
    float xs,ys,scale;

    xs = (float)gfx->hdisplay / img->i.width;
    if (fitwidth)
	return xs;
    ys = (float)gfx->vdisplay / img->i.height;
    scale = (xs < ys) ? xs : ys;
    return scale;
}

static int calculate_text_steps(int height, int yres)
{
    int pages = ceil((float)height / yres);
    int text_steps = ceil((float)height / pages);
    return text_steps;
}

/* ---------------------------------------------------------------------- */

static void effect_blend(struct flist *f, struct flist *t)
{
    struct timeval start, now;
    int msecs, weight = 0;
    char linebuffer[80];
    int pos = 0;
    int count = 0;

    gettimeofday(&start, NULL);
    do {
	gettimeofday(&now, NULL);
	msecs  = (now.tv_sec  - start.tv_sec)  * 1000;
	msecs += (now.tv_usec - start.tv_usec) / 1000;
	weight = msecs * 100 / blend_msecs;
	if (weight > 100)
	    weight = 100;
	shadow_draw_image(flist_img_get(f), f->left, f->top,
			  0, gfx->vdisplay-1, 100);
	shadow_draw_image(flist_img_get(t), t->left, t->top,
			  0, gfx->vdisplay-1, weight);

	if (perfmon) {
	    pos += snprintf(linebuffer+pos, sizeof(linebuffer)-pos,
			    " %d%%", weight);
	    status_update(linebuffer, NULL);
	    count++;
	}

	shadow_render(gfx);
    } while (weight < 100);

    if (perfmon) {
	gettimeofday(&now, NULL);
	msecs  = (now.tv_sec  - start.tv_sec)  * 1000;
	msecs += (now.tv_usec - start.tv_usec) / 1000;
	pos += snprintf(linebuffer+pos, sizeof(linebuffer)-pos,
			" | %d/%d -> %d msec",
			msecs, count, msecs/count);
	status_update(linebuffer, NULL);
	shadow_render(gfx);
	sleep(2);
    }
}

static int
svga_show(struct flist *f, struct flist *prev,
	  int timeout, char *desc, char *info, int *nr)
{
    static int        paused = 0, skip = -1;

    struct ida_image  *img = flist_img_get(f);
    int               exif = 0, help = 0;
    int               rc;
    char              key[16];
    uint32_t          keycode, keymod;
    char              linebuffer[80];

    *nr = 0;
    if (NULL == img)
	return skip;

    redraw = 1;
    for (;;) {
	if (redraw) {
	    redraw = 0;
	    if (img->i.height <= gfx->vdisplay) {
		f->top = 0;
	    } else {
		if (f->top < 0)
		    f->top = 0;
		if (f->top + gfx->vdisplay > img->i.height)
		    f->top = img->i.height - gfx->vdisplay;
	    }
	    if (img->i.width <= gfx->hdisplay) {
		f->left = 0;
	    } else {
		if (f->left < 0)
		    f->left = 0;
		if (f->left + gfx->hdisplay > img->i.width)
		    f->left = img->i.width - gfx->hdisplay;
	    }
	    if (blend_msecs && prev && prev != f &&
		flist_img_get(prev) && flist_img_get(f)) {
		effect_blend(prev, f);
		prev = NULL;
	    } else {
		shadow_draw_image(img, f->left, f->top, 0, gfx->vdisplay-1, 100);
	    }
	    status_update(desc, info);
	    shadow_render(gfx);

	    if (read_ahead) {
		struct flist *f = flist_next(fcurrent,1,0);
		if (f && !f->fimg)
		    flist_img_load(f,1);
		status_update(desc, info);
		shadow_render(gfx);
	    }
	}
        if (check_console_switch()) {
	    continue;
	}

	if (!interactive) {
	    sleep(timeout);
	    return -1;
	}

        rc = kbd_wait(timeout);
        if (check_console_switch()) {
	    continue;
	}
	if (rc < 1)
	    return -1; /* timeout */

        rc = kbd_read(key, sizeof(key), &keycode, &keymod);
        if (rc < 0)
            return KEY_ESC; /* EOF */

        switch (keycode) {
        case KEY_SPACE:
	    if (textreading && f->top < (int)(img->i.height - gfx->vdisplay)) {
		redraw = 1;
		f->top += f->text_steps;
	    } else {
		skip = KEY_SPACE;
		return KEY_SPACE;
	    }
            break;

        case KEY_UP:
	    redraw = 1;
	    f->top -= v_steps;
            break;
        case KEY_DOWN:
	    redraw = 1;
	    f->top += v_steps;
            break;
        case KEY_HOME:
	    redraw = 1;
	    f->top = 0;
            break;
        case KEY_END:
	    redraw = 1;
	    f->top = img->i.height - gfx->vdisplay;
            break;
        case KEY_LEFT:
	    redraw = 1;
	    f->left -= h_steps;
            break;
        case KEY_RIGHT:
	    redraw = 1;
	    f->left += h_steps;
            break;

        case KEY_PAGEUP:
        case KEY_K:
	    if (textreading && f->top > 0) {
		redraw = 1;
		f->top -= f->text_steps;
	    } else {
		skip = KEY_PAGEUP;
		return KEY_PAGEUP;
	    }
            break;
        case KEY_PAGEDOWN:
        case KEY_J:
        case KEY_N:
	    if (textreading && f->top < (int)(img->i.height - gfx->vdisplay)) {
		redraw = 1;
		f->top += f->text_steps;
	    } else {
		skip = KEY_PAGEDOWN;
		return KEY_PAGEDOWN;
	    }
            break;

        case KEY_P:
	    if (0 != timeout) {
		paused = !paused;
		status_update(paused ? "pause on " : "pause off", NULL);
	    }
            break;

        case KEY_H:
	    if (!help) {
		show_help();
		help = 1;
	    } else {
		redraw = 1;
		help = 0;
	    }
	    exif = 0;
            break;

        case KEY_I:
	    if (!exif) {
		show_exif(fcurrent);
		exif = 1;
	    } else {
		redraw = 1;
		exif = 0;
	    }
	    help = 0;
            break;

        case KEY_0:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
	    *nr = *nr * 10;
            if (keycode != KEY_0)
                *nr += keycode - KEY_1 + 1;
	    snprintf(linebuffer, sizeof(linebuffer), "> %d",*nr);
	    status_update(linebuffer, NULL);
            break;

        case KEY_D:
            /* need shift state for this one */
            return KEY_D | (keymod << 16);

        case KEY_RESERVED:
            /* ignored event */
            break;

        default:
            return keycode;
        }
    }
}

static void scale_fix_top_left(struct flist *f, float old, float new)
{
    struct ida_image *img = flist_img_get(f);
    unsigned int width, height;
    float cx,cy;

    cx = (float)(f->left + gfx->hdisplay/2) / (img->i.width  * old);
    cy = (float)(f->top  + gfx->vdisplay/2) / (img->i.height * old);

    width   = img->i.width  * new;
    height  = img->i.height * new;
    f->left = cx * width  - gfx->hdisplay/2;
    f->top  = cy * height - gfx->vdisplay/2;

    if (textreading) {
        f->text_steps = calculate_text_steps(height, gfx->vdisplay);
    }
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
	     "%s%.0f%% %ux%u %d/%d",
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
    char     key[16];
    uint32_t keycode, keymod;

    do {
	status_edit(line,pos);

        kbd_wait(0);
        if (check_console_switch()) {
	    continue;
	}

        rc = kbd_read(key, sizeof(key), &keycode, &keymod);
	if (rc < 0)
	    return KEY_ESC; /* EOF */

        switch (keycode) {
        case KEY_ENTER:
	    return 0;
        case KEY_ESC:
	    return KEY_ESC;

        case KEY_RIGHT:
	    if (pos < len)
		pos++;
        case KEY_LEFT:
	    if (pos > 0)
		pos--;
        case KEY_HOME:
	    pos = 0;
        case KEY_END:
	    pos = len;
        case KEY_BACKSPACE:
	    if (pos > 0) {
		memmove(line+pos-1,line+pos,len-pos+1);
		pos--;
		len--;
	    }
        case KEY_DELETE:
	    if (pos < len) {
		memmove(line+pos,line+pos+1,len-pos);
		len--;
	    }

        default:
            if (1 == rc && isprint(key[0]) && len < max) {
                /* new key */
                if (pos < len)
                    memmove(line+pos+1,line+pos,len-pos+1);
                line[pos] = key[0];
                pos++;
                len++;
                line[len] = 0;
            }
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

static struct ida_image *flist_img_get(struct flist *f)
{
    if (1 != f->scale)
	return f->simg;
    else
	return f->fimg;
}

static void flist_img_free(struct flist *f)
{
    if (!f->fimg)
	return;

    free_image(f->fimg);
    if (f->simg)
	free_image(f->simg);
    f->fimg = NULL;
    f->simg = NULL;
    list_del(&f->lru);
    img_cnt--;
}

static int flist_img_free_lru(void)
{
    struct flist *f;

    if (img_cnt <= min_cnt)
	return -1;
    f = list_entry(flru.next, struct flist, lru);
    flist_img_free(f);
    return 0;
}

static void flist_img_release_memory(void)
{
    int try_release;

    for (;;) {
	try_release = 0;
	if (img_cnt > max_cnt)
	    try_release = 1;
	if (img_mem > max_mem_mb * 1024 * 1024)
	    try_release = 1;
	if (!try_release)
	    break;
	if (0 != flist_img_free_lru())
	    break;
    }
    return;
}

static void flist_img_scale(struct flist *f, float scale, int prefetch)
{
    char linebuffer[128];

    if (!f->fimg)
	return;
    if (f->simg && f->scale == scale)
	return;

    if (f->simg) {
	free_image(f->simg);
	f->simg = NULL;
    }
    if (scale != 1) {
	if (!prefetch) {
	    snprintf(linebuffer, sizeof(linebuffer),
		     "scaling (%.0f%%) %s ...",
		     scale*100, f->name);
	    status_update(linebuffer, NULL);
	}
	f->simg = scale_image(f->fimg,scale);
	if (!f->simg) {
	    snprintf(linebuffer,sizeof(linebuffer),
		     "%s: scaling FAILED",f->name);
	    status_error(linebuffer);
	}
    }
    f->scale = scale;
}

static void flist_img_load(struct flist *f, int prefetch)
{
    char linebuffer[128];
    float scale = 1;

    if (f->fimg) {
	/* touch */
	list_del(&f->lru);
	list_add_tail(&f->lru, &flru);
	return;
    }

    snprintf(linebuffer,sizeof(linebuffer),"%s %s ...",
	     prefetch ? "prefetch" : "loading", f->name);
    status_update(linebuffer, NULL);
    f->fimg = read_image(f->name);
    if (!f->fimg) {
	snprintf(linebuffer,sizeof(linebuffer),
		 "%s: loading FAILED",f->name);
	status_error(linebuffer);
	return;
    }

    if (!f->seen) {
	scale = 1;
	if (autoup || autodown) {
	    scale = auto_scale(f->fimg);
	    if (scale < 1 && !autodown)
		scale = 1;
	    if (scale > 1 && !autoup)
		scale = 1;
	}
    } else {
	scale = f->scale;
    }
    flist_img_scale(f, scale, prefetch);

    if (!f->seen) {
 	struct ida_image *img = flist_img_get(f);
	if (img->i.width > gfx->hdisplay)
	    f->left = (img->i.width - gfx->hdisplay) / 2;
	if (img->i.height > gfx->vdisplay) {
	    f->top = (img->i.height - gfx->vdisplay) / 2;
	    if (textreading) {
                f->text_steps = calculate_text_steps(img->i.height, gfx->vdisplay);
		f->top = 0;
	    }
	}
    }

    list_add_tail(&f->lru, &flru);
    f->seen = 1;
    img_cnt++;
}

/* ---------------------------------------------------------------------- */

static jmp_buf fb_fatal_cleanup;

static void catch_exit_signal(int signal)
{
    siglongjmp(fb_fatal_cleanup,signal);
}

static void exit_signals_init(void)
{
    struct sigaction act,old;
    int termsig;

    memset(&act,0,sizeof(act));
    act.sa_handler = catch_exit_signal;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act,&old);
    sigaction(SIGQUIT,&act,&old);
    sigaction(SIGTERM,&act,&old);

    sigaction(SIGABRT,&act,&old);
    sigaction(SIGTSTP,&act,&old);

    sigaction(SIGBUS, &act,&old);
    sigaction(SIGILL, &act,&old);
    sigaction(SIGSEGV,&act,&old);

    if (0 == (termsig = sigsetjmp(fb_fatal_cleanup,0)))
	return;

    /* cleanup */
    gfx->cleanup_display();
    console_switch_cleanup();
    fprintf(stderr,"Oops: %s\n",strsignal(termsig));
    exit(42);
}

/* ---------------------------------------------------------------------- */

static void cleanup_and_exit(int code)
{
    shadow_fini();
    kbd_fini();
    gfx->cleanup_display();
    console_switch_cleanup();
    flist_print_tagged(stdout);
    exit(code);
}

static void console_switch_redraw(void)
{
    gfx->restore_display();
    shadow_set_dirty();
    shadow_render(gfx);
}

int main(int argc, char *argv[])
{
    int              once;
    int              i, arg, key;
    bool             framebuffer = false;
    bool             use_libinput;
    char             *info, *desc, *device, *output, *mode;
    char             linebuffer[128];
    struct flist     *fprev = NULL;

#if 0
    /* debug aid, to attach gdb ... */
    fprintf(stderr,"pid %d\n",getpid());
    sleep(10);
#endif

    setlocale(LC_ALL,"");
    fbi_read_config(".fbirc");
    cfg_parse_cmdline(&argc,argv,fbi_cmd);
    cfg_parse_cmdline(&argc,argv,fbi_cfg);

    if (GET_AUTO_ZOOM()) {
	cfg_set_bool(O_AUTO_UP,   1);
	cfg_set_bool(O_AUTO_DOWN, 1);
    }

    if (GET_HELP()) {
	usage(stdout, argv[0]);
	exit(0);
    }
    if (GET_VERSION()) {
	version();
	exit(0);
    }
    if (GET_DEVICE_INFO()) {
        drm_info(cfg_get_str(O_DEVICE));
        exit(0);
    }
    if (GET_WRITECONF())
	fbi_write_config();

    once        = GET_ONCE();
    autoup      = GET_AUTO_UP();
    autodown    = GET_AUTO_DOWN();
    fitwidth    = GET_FIT_WIDTH();
    statusline  = GET_VERBOSE();
    comments    = GET_COMMENTS();
    textreading = GET_TEXT_MODE();
    editable    = GET_EDIT();
    backup      = GET_BACKUP();
    preserve    = GET_PRESERVE();
    read_ahead  = GET_READ_AHEAD();

    max_mem_mb  = GET_CACHE_MEM();
    blend_msecs = GET_BLEND_MSECS();
    v_steps     = GET_SCROLL();
    h_steps     = GET_SCROLL();
    timeout     = GET_TIMEOUT();
    pcd_res     = GET_PCD_RES();
    interactive = GET_INTERACTIVE();
    use_libinput = GET_LIBINPUT();

    fontname    = cfg_get_str(O_FONT);
    filelist    = cfg_get_str(O_FILE_LIST);

    if (filelist)
	flist_add_list(filelist);
    for (i = optind; i < argc; i++)
	flist_add(argv[i]);
    flist_renumber();

    if (0 == fcount) {
	usage(stderr, argv[0]);
	exit(1);
    }

    if (GET_RANDOM())
	flist_randomize();
    fcurrent = flist_first();

    font_init();
    if (NULL == fontname)
	fontname = "monospace:size=16";
    face = font_open(fontname);
    if (NULL == face) {
	fprintf(stderr,"can't open font: %s\n",fontname);
	exit(1);
    }

    /* gfx device init */
    device = cfg_get_str(O_DEVICE);
    output = cfg_get_str(O_OUTPUT);
    mode = cfg_get_str(O_VIDEO_MODE);
    if (device) {
        /* device specified */
        if (strncmp(device, "/dev/d", 6) == 0) {
            gfx = drm_init(device, output, mode, false);
        } else {
            framebuffer = true;
            gfx = fb_init(device, mode, GET_VT());
        }
    } else {
        /* try drm first, failing that fb */
        gfx = drm_init(NULL, output, mode, false);
        if (!gfx) {
            framebuffer = true;
            gfx = fb_init(NULL, mode, GET_VT());
        }
    }
    if (!gfx) {
        fprintf(stderr, "graphics init failed\n");
        exit(1);
    }
    exit_signals_init();
    signal(SIGTSTP,SIG_IGN);
    if (console_switch_init(console_switch_redraw) < 0) {
        fprintf(stderr, "NOTICE: No vt switching available on terminal.\n");
        fprintf(stderr, "NOTICE: Not started from linux console?  CONFIG_VT=n?\n");
        if (framebuffer) {
            fprintf(stderr, "WARNING: Running on framebuffer and can't manage access.\n");
            fprintf(stderr, "WARNING: Other processes (fbcon too) can write to display.\n");
            fprintf(stderr, "WARNING: Also can't properly cleanup on exit.\n");
        }
    }
    shadow_init(gfx);

    /* svga main loop */
    kbd_init(use_libinput, gfx->devnum);
    desc = NULL;
    info = NULL;
    for (;;) {
	flist_img_load(fcurrent, 0);
	flist_img_release_memory();
	img = flist_img_get(fcurrent);
	if (img) {
	    desc = make_desc(&fcurrent->fimg->i, fcurrent->name);
	    info = make_info(fcurrent->fimg, fcurrent->scale);
	}

	key = svga_show(fcurrent, fprev, timeout, desc, info, &arg);
	fprev = fcurrent;
	switch (key) {
	case KEY_D | (KEY_MOD_SHIFT << 16):
	    if (editable) {
		struct flist *fdel = fcurrent;
		if (flist_islast(fcurrent))
		    fcurrent = flist_prev(fcurrent,0);
		else
		    fcurrent = flist_next(fcurrent,0,0);
		unlink(fdel->name);
		flist_img_free(fdel);
		flist_del(fdel);
		flist_renumber();
		if (list_empty(&flist)) {
		    /* deleted last one */
		    cleanup_and_exit(0);
		}
	    } else {
		status_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	case KEY_R:
	case KEY_L:
	{
	    if (editable) {
		snprintf(linebuffer,sizeof(linebuffer),
			 "rotating %s ...",fcurrent->name);
		status_update(linebuffer, NULL);
		jpeg_transform_inplace
		    (fcurrent->name,
		     (key == KEY_R) ? JXFORM_ROT_90 : JXFORM_ROT_270,
		     NULL,
		     NULL,0,
		     (backup   ? JFLAG_FILE_BACKUP    : 0) |
		     (preserve ? JFLAG_FILE_KEEP_TIME : 0) |
		     JFLAG_TRANSFORM_IMAGE     |
		     JFLAG_TRANSFORM_THUMBNAIL |
		     JFLAG_TRANSFORM_TRIM      |
		     JFLAG_UPDATE_ORIENTATION);
		flist_img_free(fcurrent);
	    } else {
		status_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	}
	case KEY_X:
	case KEY_Y:
	{
	    if (editable) {
		snprintf(linebuffer,sizeof(linebuffer),
			 "mirroring %s ...",fcurrent->name);
		status_update(linebuffer, NULL);
		jpeg_transform_inplace
		    (fcurrent->name,
		     (key == KEY_X) ? JXFORM_FLIP_V : JXFORM_FLIP_H,
		     NULL,
		     NULL,0,
		     (backup   ? JFLAG_FILE_BACKUP    : 0) |
		     (preserve ? JFLAG_FILE_KEEP_TIME : 0) |
		     JFLAG_TRANSFORM_IMAGE     |
		     JFLAG_TRANSFORM_THUMBNAIL |
		     JFLAG_TRANSFORM_TRIM      |
		     JFLAG_UPDATE_ORIENTATION);
		flist_img_free(fcurrent);
	    } else {
		status_error("readonly mode, sorry [start with --edit?]");
	    }
	    break;
	}
	case KEY_ENTER:
	    fcurrent->tag = !fcurrent->tag;
	    /* fall throuth */
	case KEY_SPACE:
            fcurrent = flist_next(fcurrent,1,0);
	    if (NULL != fcurrent)
		break;
	    /* else fall */
	case KEY_ESC:
	case KEY_Q:
	case KEY_E:
	    cleanup_and_exit(0);
	    break;
	case KEY_PAGEDOWN:
        case BTN_LEFT:
	    fcurrent = flist_next(fcurrent,0,1);
	    break;
	case KEY_PAGEUP:
        case BTN_RIGHT:
	    fcurrent = flist_prev(fcurrent,1);
	    break;
	case -1: /* timeout */
            if (filelist && !once && flist_islast(fcurrent)) {
                flist_check_reload_list(filelist);
                fcurrent = flist_first();
            } else {
                fcurrent = flist_next(fcurrent,once,1);
            }
	    if (NULL == fcurrent) {
		cleanup_and_exit(0);
	    }
	    /* FIXME: wrap around */
	    break;
	case KEY_KPPLUS:
	case KEY_KPMINUS:
	case KEY_A:
	case KEY_S:
	    {
		float newscale, oldscale = fcurrent->scale;

		if (key == KEY_KPPLUS) {
		    newscale = fcurrent->scale * 1.6;
		} else if (key == KEY_KPMINUS) {
		    newscale = fcurrent->scale / 1.6;
		} else if (key == KEY_A) {
		    newscale = auto_scale(fcurrent->fimg);
		} else {
		    newscale = arg / 100.0;
		}
		if (newscale < 0.1)
		    newscale = 0.1;
		if (newscale > 10)
		    newscale = 10;
		flist_img_scale(fcurrent, newscale, 0);
		scale_fix_top_left(fcurrent, oldscale, newscale);
		break;
	    }
	case KEY_G:
	    if (arg > 0 && arg <= fcount)
		fcurrent = flist_goto(arg);
	    break;
#if 0 /* FIXME */
	case KEY_DELAY:
	    timeout = arg;
	    break;
#endif
	case KEY_V:
	    statusline = !statusline;
	    break;
	case KEY_T:
	    if (!comments) {
		edit_desc(img, fcurrent->name);
		desc = make_desc(&fcurrent->fimg->i,fcurrent->name);
	    }
	    break;
	}
    }
}
