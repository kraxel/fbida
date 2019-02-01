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
#include <pty.h>

#include <sys/time.h>
#include <sys/ioctl.h>

#include <linux/input.h>

#include <cairo.h>
#include <libudev.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

#include "fbtools.h"
#include "drmtools.h"
#include "vt.h"
#include "kbd.h"
#include "tmt.h"

/* ---------------------------------------------------------------------- */

static char *seat_name = "seat0";
static char *font_name = "monospace";
static int font_size = 16;

static gfxstate *gfx;
static cairo_surface_t *surface1;
static cairo_surface_t *surface2;
static cairo_t *context1;
static cairo_t *context2;
cairo_font_extents_t extents;

static TMT *vt;
static int clear, dirty, pty;
static struct udev *udev;
static struct libinput *kbd;

static struct xkb_context *xkb;
static struct xkb_keymap *map;
static struct xkb_state *state;
static struct xkb_rule_names layout = {
    .rules   = NULL,
    .model   = "pc105",
    .layout  = "us",
    .variant = NULL,
    .options = NULL,
};

int debug = 0;

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

static void cleanup_and_exit(int code)
{
    gfx->cleanup_display();
    console_switch_cleanup();
    exit(code);
}

static void console_switch_suspend(void)
{
    libinput_suspend(kbd);
}

static void console_switch_resume(void)
{
    gfx->restore_display();
    libinput_resume(kbd);
    clear++;
    dirty++;
}

/* ---------------------------------------------------------------------- */

const char *ansiseq[KEY_MAX] = {
    [ KEY_UP       ] = "\x1b[A",
    [ KEY_DOWN     ] = "\x1b[B",
    [ KEY_RIGHT    ] = "\x1b[C",
    [ KEY_LEFT     ] = "\x1b[D",
    [ KEY_END      ] = "\x1b[F",
    [ KEY_HOME     ] = "\x1b[H",

    [ KEY_INSERT   ] = "\x1b[2~",
    [ KEY_DELETE   ] = "\x1b[3~",
    [ KEY_PAGEUP   ] = "\x1b[5~",
    [ KEY_PAGEDOWN ] = "\x1b[6~",

    [ KEY_F1       ] = "\x1b[OP",
    [ KEY_F2       ] = "\x1b[OQ",
    [ KEY_F3       ] = "\x1b[OR",
    [ KEY_F4       ] = "\x1b[OS",

    [ KEY_F5       ] = "\x1b[15~",
    [ KEY_F6       ] = "\x1b[17~",
    [ KEY_F7       ] = "\x1b[18~",
    [ KEY_F8       ] = "\x1b[19~",
    [ KEY_F9       ] = "\x1b[20~",
    [ KEY_F10      ] = "\x1b[21~",
    [ KEY_F11      ] = "\x1b[23~",
    [ KEY_F12      ] = "\x1b[24~",
};

static void xkb_configure(void)
{
    char line[128], *m, *v, *h;
    FILE *fp;

    fp = fopen("/etc/vconsole.conf", "r");
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "KEYMAP=", 7) != 0)
            continue;
        m = line + 7;
        if (*m == '"')
            m++;
        if ((h = strchr(m, '\n')) != NULL)
            *h = 0;
        if ((h = strchr(m, '"')) != NULL)
            *h = 0;
        v = strchr(m, '-');
        if (v) {
            *(v++) = 0;
            layout.variant = strdup(v);
        }
        layout.layout = strdup(m);
    }
    fclose(fp);
}

/* ---------------------------------------------------------------------- */

struct color {
    float r;
    float g;
    float b;
};

static struct color tmt_colors_normal[] = {
    [ TMT_COLOR_BLACK   ] = { .r = 0.0, .g = 0.0, .b = 0.0 },
    [ TMT_COLOR_RED     ] = { .r = 0.7, .g = 0.0, .b = 0.0 },
    [ TMT_COLOR_GREEN   ] = { .r = 0.0, .g = 0.7, .b = 0.0 },
    [ TMT_COLOR_YELLOW  ] = { .r = 0.7, .g = 0.7, .b = 0.0 },
    [ TMT_COLOR_BLUE    ] = { .r = 0.0, .g = 0.0, .b = 0.7 },
    [ TMT_COLOR_MAGENTA ] = { .r = 0.7, .g = 0.0, .b = 0.7 },
    [ TMT_COLOR_CYAN    ] = { .r = 0.0, .g = 0.7, .b = 0.7 },
    [ TMT_COLOR_WHITE   ] = { .r = 0.7, .g = 0.7, .b = 0.7 },
};

static struct color tmt_colors_bold[] = {
    [ TMT_COLOR_BLACK   ] = { .r = 0.3, .g = 0.3, .b = 0.3 },
    [ TMT_COLOR_RED     ] = { .r = 1.0, .g = 0.3, .b = 0.3 },
    [ TMT_COLOR_GREEN   ] = { .r = 0.3, .g = 1.0, .b = 0.3 },
    [ TMT_COLOR_YELLOW  ] = { .r = 1.0, .g = 1.0, .b = 0.3 },
    [ TMT_COLOR_BLUE    ] = { .r = 0.3, .g = 0.3, .b = 1.0 },
    [ TMT_COLOR_MAGENTA ] = { .r = 1.0, .g = 0.3, .b = 1.0 },
    [ TMT_COLOR_CYAN    ] = { .r = 0.3, .g = 1.0, .b = 1.0 },
    [ TMT_COLOR_WHITE   ] = { .r = 1.0, .g = 1.0, .b = 1.0 },
};

struct color *tmt_foreground(struct TMTATTRS *a)
{
    struct color *tmt_colors = tmt_colors_normal;
    int fg = a->fg;

    if (a->bold)
        tmt_colors = tmt_colors_bold;
    if (fg == TMT_COLOR_DEFAULT)
        fg = TMT_COLOR_WHITE;
    return tmt_colors + fg;
}

struct color *tmt_background(struct TMTATTRS *a)
{
    int bg = a->bg;

    if (bg == TMT_COLOR_DEFAULT)
       bg = TMT_COLOR_BLACK;
    return tmt_colors_normal + bg;
}

/* ---------------------------------------------------------------------- */

static void render(void)
{
    static bool second;
    const TMTSCREEN *s = tmt_screen(vt);
    const TMTPOINT *cursor = tmt_cursor(vt);
    cairo_t *context;
    int line, col, tx, ty;
    wchar_t ws[2];
    char utf8[10];

    if (surface2)
        second = !second;
    context = second ? context2 : context1;

    tx = (gfx->hdisplay - (extents.max_x_advance * s->ncol)) / 2;
    ty = (gfx->vdisplay - (extents.height * s->nline)) / 2;

    if (clear) {
        cairo_set_source_rgb(context, 0, 0, 0);
        cairo_paint(context);
    }

    for (line = 0; line < s->nline; line++) {
        for (col = 0; col < s->ncol; col++) {
            TMTCHAR *c = &s->lines[line]->chars[col];
            struct color *bg = tmt_background(&c->a);
            struct color *fg = tmt_foreground(&c->a);

            if (cursor->r == line && cursor->c == col) {
                bg = tmt_colors_normal + TMT_COLOR_WHITE;
                fg = tmt_colors_normal + TMT_COLOR_BLACK;
            }

            /* background */
            cairo_rectangle(context,
                            tx + col * extents.max_x_advance,
                            ty + line * extents.height,
                            extents.max_x_advance,
                            extents.height);
            cairo_set_source_rgb(context, bg->r, bg->g, bg->b);
            cairo_fill(context);

            /* char */
            cairo_move_to(context,
                          tx + col * extents.max_x_advance,
                          ty + line * extents.height + extents.ascent);
            cairo_set_source_rgb(context, fg->r, fg->g, fg->b);
            ws[0] = c->c;
            ws[1] = 0;
            wcstombs(utf8, ws, sizeof(utf8));
            cairo_show_text(context, utf8);
        }
    }

    cairo_show_page(context);

    if (gfx->flush_display)
        gfx->flush_display(second);
}

static void tmt_callback(tmt_msg_t m, TMT *vt, const void *a, void *p)
{
    switch (m) {
    case TMT_MSG_UPDATE:
        dirty++;
        break;
    case TMT_MSG_ANSWER:
        write(pty, a, strlen(a));
    default:
        break;
    }
}

static void child_exec_shell(struct winsize *win)
{
    char lines[10], columns[10];

    /* reset terminal */
    fprintf(stderr, "\x1b[0m");

    /* check for errors */
    if (libinput_deverror != 0) {
        fprintf(stderr, "ERROR: failed to open input devices (%d ok, %d failed)\n",
                libinput_devcount, libinput_deverror);
        return;
    }

#if 1
    fprintf(stderr, "# \n");
    fprintf(stderr, "# This is fbcon @%s, using %s-%d.\n",
            seat_name, font_name, font_size);
    fprintf(stderr, "# \n");
#endif

    /* prepare environment, run shell */
    snprintf(lines, sizeof(lines), "%d", win->ws_row);
    snprintf(columns, sizeof(columns), "%d", win->ws_col);
    setenv("TERM", "ansi", true);
    setenv("LINES", lines, true);
    setenv("COLUMNS", columns, true);

    execl("/bin/sh", "-sh", NULL);
    fprintf(stderr, "failed to exec /bin/sh: %s\n", strerror(errno));
}

int main(int argc, char *argv[])
{
    struct udev_enumerate *uenum;
    struct udev_list_entry *ulist, *uentry;
    struct winsize win;
    const char *drm_node = NULL;
    const char *fb_node = NULL;
    int input;
    pid_t child;

    setlocale(LC_ALL,"");

    /* look for gfx devices */
    udev = udev_new();
    uenum = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(uenum, "drm");
    udev_enumerate_add_match_subsystem(uenum, "graphics");
    udev_enumerate_add_match_tag(uenum, "seat");
    udev_enumerate_scan_devices(uenum);
    ulist = udev_enumerate_get_list_entry(uenum);
    udev_list_entry_foreach(uentry, ulist) {
        const char *path = udev_list_entry_get_name(uentry);
        struct udev_device *udevice = udev_device_new_from_syspath(udev, path);
        const char *node = udev_device_get_devnode(udevice);
        const char *seat = udev_device_get_property_value(udevice, "ID_SEAT");
        const char *subsys = udev_device_get_subsystem(udevice);
        if (!seat)
            seat = "seat0";
        if (strcmp(seat, seat_name) != 0)
            continue;
        if (!node)
            continue;
        if (!drm_node && strcmp(subsys, "drm") == 0)
            drm_node = node;
        if (!fb_node && strcmp(subsys, "graphics") == 0)
            fb_node = node;
    }

    /* init graphics */
    if (drm_node) {
        gfx = drm_init(drm_node, NULL, NULL, true);
        if (!gfx)
            fprintf(stderr, "%s: init failed\n", drm_node);
    }
    if (!gfx && fb_node) {
        gfx = fb_init(fb_node, NULL);
        if (!gfx)
            fprintf(stderr, "%s: init failed\n", fb_node);
    }
    if (!gfx)
        exit(1);
    exit_signals_init();
    signal(SIGTSTP,SIG_IGN);
    if (console_switch_init(console_switch_suspend,
                            console_switch_resume) < 0) {
        fprintf(stderr, "NOTICE: No vt switching available on terminal.\n");
    }

    /* init cairo */
    surface1 = cairo_image_surface_create_for_data(gfx->mem,
                                                   CAIRO_FORMAT_ARGB32,
                                                   gfx->hdisplay,
                                                   gfx->vdisplay,
                                                   gfx->stride);
    context1 = cairo_create(surface1);
    cairo_select_font_face(context1, font_name,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context1, font_size);
    cairo_font_extents(context1, &extents);
    if (gfx->mem2) {
        surface2 = cairo_image_surface_create_for_data(gfx->mem2,
                                                       CAIRO_FORMAT_ARGB32,
                                                       gfx->hdisplay,
                                                       gfx->vdisplay,
                                                       gfx->stride);
        context2 = cairo_create(surface2);
        cairo_select_font_face(context2, font_name,
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(context2, font_size);
    }

    /* init libinput */
    kbd = libinput_udev_create_context(&libinput_interface, NULL, udev);
    libinput_udev_assign_seat(kbd, seat_name);
    input = libinput_get_fd(kbd);

    /* init udev + xkbcommon */
    xkb_configure();
    xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    map = xkb_keymap_new_from_names(xkb, &layout, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!map) {
        layout.variant = NULL;
        map = xkb_keymap_new_from_names(xkb, &layout, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!map) {
            layout.layout = "us";
            map = xkb_keymap_new_from_names(xkb, &layout, XKB_KEYMAP_COMPILE_NO_FLAGS);
        }
    }
    state = xkb_state_new(map);

    /* init terminal emulation */
    win.ws_col = gfx->hdisplay / extents.max_x_advance;
    win.ws_row = gfx->vdisplay / extents.height;
    win.ws_xpixel = extents.max_x_advance;
    win.ws_ypixel = extents.height;
    vt = tmt_open(win.ws_row, win.ws_col, tmt_callback, NULL, NULL);
    child = forkpty(&pty, NULL, NULL, &win);
    if (0 == child) {
        child_exec_shell(&win);
        /* only reached on errors ... */
        sleep(3);
        exit(1);
    }

    /* parent */
    clear++;
    dirty++;
    for (;;) {
        fd_set set;
        int rc, max;

        if (dirty)
            render();

        max = 0;
        FD_ZERO(&set);
        FD_SET(pty, &set);
        if (max < pty)
            max = pty;
        FD_SET(input, &set);
        if (max < input)
            max = input;

        rc = select(max+ 1, &set, NULL, NULL, NULL);
        if (rc < 0)
            break;

        if (FD_ISSET(pty, &set)) {
            char buf[1024];
            rc = read(pty, buf, sizeof(buf));
            if (rc < 0 && errno != EAGAIN && errno != EINTR)
                break; /* read error */
            if (rc == 0)
                break; /* no data -> EOF */
            if (rc > 0)
                tmt_write(vt, buf, rc);
        }

        if (FD_ISSET(input, &set)) {
            struct libinput_event *evt;
            struct libinput_event_keyboard *kevt;
            xkb_keycode_t key;
            bool down;
            char buf[32];

            rc = libinput_dispatch(kbd);
            if (rc < 0)
                break;
            while ((evt = libinput_get_event(kbd)) != NULL) {
                switch (libinput_event_get_type(evt)) {
                case LIBINPUT_EVENT_KEYBOARD_KEY:
                    kevt = libinput_event_get_keyboard_event(evt);
                    key = libinput_event_keyboard_get_key(kevt) + 8;
                    down = libinput_event_keyboard_get_key_state(kevt);
                    xkb_state_update_key(state, key, down);
                    if (down) {
                        if (ansiseq[key - 8]) {
                            write(pty, ansiseq[key - 8],
                                  strlen(ansiseq[key - 8]));
                        } else {
                            rc = xkb_state_key_get_utf8(state, key,
                                                        buf, sizeof(buf));
                            if (rc > 0)
                                write(pty, buf, rc);
                        }
                    }
                    break;
                default:
                    /* ignore event */
                    break;
                }
                libinput_event_destroy(evt);
            }
        }
    }

    cleanup_and_exit(0);
    return 0;
}
