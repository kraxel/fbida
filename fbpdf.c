/*
 * pdf viewer, for framebuffer devices
 *
 *   (c) 1998-2016 Gerd Hoffmann <gerd@kraxel.org>
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
#include <inttypes.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <setjmp.h>

#include <sys/time.h>
#include <sys/ioctl.h>

#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/fb.h>

#include <poppler.h>
#include <cairo.h>

#include <epoxy/egl.h>

#include "vt.h"
#include "kbd.h"
#include "fbtools.h"
#include "drmtools.h"
#include "fbiconfig.h"

/* ---------------------------------------------------------------------- */

/* options */
int                        fitwidth;
char                       *device;
char                       *output;
char                       *mode;

/* gfx output state */
gfxstate                   *gfx;
int                        debug;
PopplerDocument            *doc;
cairo_surface_t            *surface1;
cairo_surface_t            *surface2;

/* pdf render state */
PopplerPage                *page;
double                     pw, ph; /* pdf page size */
double                     scale;
double                     sw, sh; /* scaled pdf page size */
double                     tx, ty;

/* ---------------------------------------------------------------------- */

static void page_check_scroll(void)
{
    if (gfx->vdisplay < sh) {
        /* range check vertical scroll */
        if (ty < gfx->vdisplay - sh)
            ty = gfx->vdisplay - sh;
        if (ty > 0)
            ty = 0;
    } else {
        /* no need to scroll -> center */
        ty = (gfx->vdisplay - sh) / 2;
    }

    if (gfx->hdisplay < sw) {
        /* range check vertical scroll */
        if (tx < gfx->hdisplay - sw)
            tx = gfx->hdisplay - sw;
        if (tx > 0)
            tx = 0;
    } else {
        /* no need to scroll -> center */
        tx = (gfx->hdisplay - sw) / 2;
    }
}

static void page_move(double dx, double dy)
{
    tx += dx * gfx->hdisplay;
    ty += dy * gfx->vdisplay;
    page_check_scroll();
}

static void page_scale(double factor)
{
    scale *= factor;
    tx *= factor;
    ty *= factor;
    sw = pw * scale;
    sh = ph * scale;
    page_check_scroll();
}

static void page_fit(void)
{
    double sx, sy;

    poppler_page_get_size(page, &pw, &ph);
    sx = gfx->hdisplay / pw;
    sy = gfx->vdisplay / ph;
    scale = sx < sy ? sx : sy;
    sw = pw * scale;
    sh = ph * scale;
    page_check_scroll();
}

static void page_fit_width(void)
{
    poppler_page_get_size(page, &pw, &ph);
    scale = gfx->hdisplay / pw;
    sw = pw * scale;
    sh = ph * scale;
    ty = 0;
    page_check_scroll();
}

static void page_render(void)
{
    static bool second;
    cairo_t *context;

    if (surface2)
        second = !second;
    context = cairo_create(second ? surface2 : surface1);

    cairo_translate(context, tx, ty);
    cairo_scale(context, scale, scale);
    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_paint(context);
    poppler_page_render(page, context);
    cairo_show_page(context);
    cairo_destroy(context);

    if (gfx->flush_display)
        gfx->flush_display(second);
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
    gfx->cleanup_display();
    console_switch_cleanup();
    exit(code);
}

static void console_switch_suspend(void)
{
    kbd_suspend();
}

static void console_switch_resume(void)
{
    gfx->resume_display();
    kbd_resume();
}

static void
version(void)
{
    fprintf(stdout,
	    "fbpdf version " VERSION "\n"
	    "(c) 1998-2016 Gerd Hoffmann <gerd@kraxel.org>\n");
}

static void
usage(FILE *fp, char *name)
{
    char           *h;

    if (NULL != (h = strrchr(name, '/')))
	name = h+1;
    fprintf(fp,
	    "\n"
	    "This program displays pfd files using the Linux fbdev or drm device.\n"
	    "\n"
	    "usage: %s [ options ] pdf-file\n"
	    "\n",
	    name);

    cfg_help_cmdline(fp,fbpdf_cmd,4,20,0);
    cfg_help_cmdline(fp,fbpdf_cfg,4,20,40);

    fprintf(fp,
	    "\n");
}

int main(int argc, char *argv[])
{
    GError *err = NULL;
    bool framebuffer = false;
    bool use_libinput;
    bool quit, newpage, pageflip;
    char cwd[1024];
    char uri[1048];
    char key[32];
    uint32_t keycode, keymod;
    int index = 0;

    setlocale(LC_ALL,"");

    fbi_read_config(".config/fbpdf.conf", ".fbpdf.rc");
    cfg_parse_cmdline(&argc,argv,fbpdf_cmd);
    cfg_parse_cmdline(&argc,argv,fbpdf_cfg);

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

    if (optind+1 != argc ) {
	usage(stderr, argv[0]);
        exit(1);
    }

    /* filename -> uri */
    if (strncmp(argv[optind], "file:/", 6) == 0) {
        /* is uri already */
        snprintf(uri, sizeof(uri), "%s", argv[optind]);
    } else if (strncmp(argv[optind], "/", 1) == 0) {
        /* absolute path */
        snprintf(uri, sizeof(uri), "file:%s", argv[optind]);
    } else {
        /* relative path */
        getcwd(cwd, sizeof(cwd));
        snprintf(uri, sizeof(uri), "file:%s/%s", cwd, argv[optind]);
    }
    doc = poppler_document_new_from_file(uri, NULL, &err);
    if (!doc) {
        fprintf(stderr, "loading %s failed: %s\n", uri, err->message);
        exit(1);
    }

    /* gfx init */
    device = cfg_get_str(O_DEVICE);
    output = cfg_get_str(O_OUTPUT);
    mode = cfg_get_str(O_VIDEO_MODE);
    fitwidth = GET_FIT_WIDTH();
    pageflip = GET_PAGEFLIP();
    use_libinput = GET_LIBINPUT();

    if (device) {
        /* device specified */
        if (strncmp(device, "/dev/d", 6) == 0) {
            gfx = drm_init(device, output, mode, pageflip);
        } else {
            framebuffer = true;
            gfx = fb_init(device, mode);
        }
    } else {
        /* try drm first, failing that fb */
        gfx = drm_init(NULL, output, mode, pageflip);
        if (!gfx) {
            framebuffer = true;
            gfx = fb_init(NULL, mode);
        }
    }
    if (!gfx) {
        fprintf(stderr, "graphics init failed\n");
        exit(1);
    }
    exit_signals_init();
    signal(SIGTSTP,SIG_IGN);
    if (console_switch_init(console_switch_suspend,
                            console_switch_resume) < 0) {
        fprintf(stderr, "NOTICE: No vt switching available on terminal.\n");
        fprintf(stderr, "NOTICE: Not started from linux console?  CONFIG_VT=n?\n");
        if (framebuffer) {
            fprintf(stderr, "WARNING: Running on framebuffer and can't manage access.\n");
            fprintf(stderr, "WARNING: Other processes (fbcon too) can write to display.\n");
            fprintf(stderr, "WARNING: Also can't properly cleanup on exit.\n");
        }
    }

    surface1 = cairo_image_surface_create_for_data(gfx->mem,
                                                   gfx->fmt->cairo,
                                                   gfx->hdisplay,
                                                   gfx->vdisplay,
                                                   gfx->stride);
    if (gfx->mem2) {
        surface2 = cairo_image_surface_create_for_data(gfx->mem2,
                                                       gfx->fmt->cairo,
                                                       gfx->hdisplay,
                                                       gfx->vdisplay,
                                                       gfx->stride);
    }

    kbd_init(use_libinput, gfx->devnum);
    if (use_libinput && (libinput_deverror != 0 ||
                         libinput_devcount == 0)) {
        fprintf(stderr, "ERROR: failed to open input devices (%d ok, %d failed)\n",
                libinput_devcount, libinput_deverror);
        cleanup_and_exit(0);
    }

    index = 0;
    newpage = true;
    for (quit = false; !quit;) {
        if (newpage) {
            page = poppler_document_get_page(doc, index);
            if (fitwidth) {
                page_fit_width();
            } else {
                page_fit();
            }
            newpage = false;
        }
        page_render();

        if (check_console_switch()) {
	    continue;
	}
        kbd_wait(0);
        if (check_console_switch()) {
	    continue;
	}

        kbd_read(key, sizeof(key), &keycode, &keymod);

        switch (keycode) {
        case XKB_KEY_Escape:
        case XKB_KEY_Q:
            quit = true;
            break;
        case XKB_KEY_Page_Up:
            if (index > 0) {
                index--;
                newpage = true;
            }
            break;
        case XKB_KEY_Page_Down:
            if (index+1 < poppler_document_get_n_pages(doc)) {
                index++;
                newpage = true;
            }
            break;
        case XKB_KEY_Up:
            page_move(0, 0.2);
            break;
        case XKB_KEY_Down:
            page_move(0, -0.2);
            break;
        case XKB_KEY_Left:
            page_move(0.2, 0);
            break;
        case XKB_KEY_Right:
            page_move(-0.2, 0);
            break;
        case XKB_KEY_KP_Subtract:
            page_scale(0.7);
            break;
        case XKB_KEY_KP_Add:
            page_scale(1.5);
            break;
        case XKB_KEY_space:
            if (ty > gfx->vdisplay - sh) {
                page_move(0, -0.75);
                break;
            } else if (index+1 < poppler_document_get_n_pages(doc)) {
                index++;
                newpage = true;
            } else {
                quit = true;
            }
            break;
        }
    }

    kbd_fini();
    cleanup_and_exit(0);
    return 0;
}
