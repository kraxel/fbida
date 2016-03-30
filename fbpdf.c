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

#include "vt.h"
#include "kbd.h"
#include "fbtools.h"
#include "drmtools.h"

/* ---------------------------------------------------------------------- */

gfxstate                   *gfx;
int                        debug;
PopplerDocument            *doc;
cairo_surface_t            *surface1;
cairo_surface_t            *surface2;

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

    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_paint(context);

    cairo_translate(context, tx, ty);
    cairo_scale(context, scale, scale);
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

static void console_switch_redraw(void)
{
    gfx->restore_display();
}

int main(int argc, char *argv[])
{
    GError *err = NULL;
    bool framebuffer = false;
    bool quit, newpage;
    char key[32];
    uint32_t keycode, keymod;
    int index = 0;

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
    gfx = drm_init(NULL, NULL, true);
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

    surface1 = cairo_image_surface_create_for_data(gfx->mem,
                                                  CAIRO_FORMAT_ARGB32,
                                                  gfx->hdisplay,
                                                  gfx->vdisplay,
                                                  gfx->stride);
    if (gfx->mem2) {
        surface2 = cairo_image_surface_create_for_data(gfx->mem2,
                                                       CAIRO_FORMAT_ARGB32,
                                                       gfx->hdisplay,
                                                       gfx->vdisplay,
                                                       gfx->stride);
    }

    tty_raw();

    index = 0;
    newpage = true;
    for (quit = false; !quit;) {
        if (newpage) {
            page = poppler_document_get_page(doc, index);
            if (0)
                page_fit();
            if (1)
                page_fit_width();
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

        memset(key, 0, sizeof(key));
        read(0, key, sizeof(key)-1);
        keycode = kbd_parse(key, &keymod);

        switch (keycode) {
        case KEY_ESC:
        case KEY_Q:
            quit = true;
            break;
        case KEY_PAGEUP:
            if (index > 0) {
                index--;
                newpage = true;
            }
            break;
        case KEY_PAGEDOWN:
            if (index+1 < poppler_document_get_n_pages(doc)) {
                index++;
                newpage = true;
            }
            break;
        case KEY_UP:
            page_move(0, 0.2);
            break;
        case KEY_DOWN:
            page_move(0, -0.2);
            break;
        case KEY_LEFT:
            page_move(0.2, 0);
            break;
        case KEY_RIGHT:
            page_move(-0.2, 0);
            break;
        case KEY_KPMINUS:
            page_scale(0.7);
            break;
        case KEY_KPPLUS:
            page_scale(1.5);
            break;
        case KEY_SPACE:
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

    tty_restore();
    cleanup_and_exit(0);
    return 0;
}
