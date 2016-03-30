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

#include "vt.h"
#include "fbtools.h"
#include "drmtools.h"

/* ---------------------------------------------------------------------- */

gfxstate                   *gfx;
int                        debug;
PopplerDocument            *doc;
PopplerPage                *page;
cairo_surface_t            *surface;
cairo_t                    *context;

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

static void console_switch_redraw(void)
{
    /* TODO */
}

int main(int argc, char *argv[])
{
    GError *err = NULL;
    bool framebuffer = false;
    double w,h;

    setlocale(LC_ALL,"");

    if (argc < 2) {
        fprintf(stderr, "usage: %s <pdf-file>\n", argv[0]);
        exit(1);
    }
    doc = poppler_document_new_from_file(argv[1], NULL, &err);
    if (!doc) {
        fprintf(stderr, "loading %s failed: %s\n", argv[1], err->message);
        exit(1);
    }

    /* gfx init */
    gfx = drm_init(NULL, NULL);
    if (!gfx) {
        framebuffer = true;
        gfx = fb_init(NULL, NULL, 0);
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

    surface = cairo_image_surface_create_for_data(gfx->mem,
                                                  CAIRO_FORMAT_ARGB32,
                                                  gfx->hdisplay,
                                                  gfx->vdisplay,
                                                  gfx->stride);
    context = cairo_create(surface);

    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_paint(context);

    page = poppler_document_get_page(doc, 0);
    poppler_page_get_size(page, &w, &h);
    fprintf(stderr, "page: %.1lf x %.1lf\n", w, h);

    poppler_page_render(page, context);
    cairo_show_page(context);

    sleep(5);
    cleanup_and_exit(0);
    return 0;
}
