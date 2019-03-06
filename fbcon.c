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
#include <pwd.h>

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <linux/input.h>

#include <glib.h>
#include <cairo.h>
#include <libudev.h>
#include <libinput.h>
#include <libtsm.h>
#include <xkbcommon/xkbcommon.h>

#include "fbtools.h"
#include "drmtools.h"
#include "vt.h"
#include "kbd.h"
#include "logind.h"

/* ---------------------------------------------------------------------- */

static const char *seat_name = "seat0";

/* config */
static const char *font_name = "monospace";
static int font_size = 16;
static bool verbose;

static gfxstate *gfx;
static cairo_font_extents_t extents;
static struct cairo_state {
    cairo_surface_t *surface;
    cairo_t *context;
    tsm_age_t age;
    uint32_t tx, ty, clear;
} state1, state2;

static int dirty, pty;
static struct udev *udev;
static struct libinput *kbd;
static bool logind = false;

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

static struct tsm_screen *vts;
static struct tsm_vte    *vte;
static bool tsm_sb;

int debug = 0;

enum fbcon_mode {
    FBCON_MODE_SHELL,
    FBCON_MODE_EXEC,
};

/* ---------------------------------------------------------------------- */

#define FBCON_CFG_FILE       ".config/fbcon.conf"
#define FBCON_CFG_GROUP      "fbcon"
#define FBCON_CFG_FONT_FACE  "font-face"
#define FBCON_CFG_FONT_SIZE  "font-size"
#define FBCON_CFG_VERBOSE    "verbose"

static void fbcon_read_config(void)
{
    char *filename;
    GKeyFile *cfg;
    char *string;
    int integer;

    cfg = g_key_file_new();
    filename = g_strdup_printf("%s/%s", getenv("HOME"), FBCON_CFG_FILE);
    g_key_file_load_from_file(cfg, filename, G_KEY_FILE_NONE, NULL);

    string = g_key_file_get_string(cfg, FBCON_CFG_GROUP, FBCON_CFG_FONT_FACE, NULL);
    if (string)
        font_name = string;

    integer = g_key_file_get_integer(cfg, FBCON_CFG_GROUP, FBCON_CFG_FONT_SIZE, NULL);
    if (integer)
        font_size =integer;

    verbose = g_key_file_get_boolean(cfg, FBCON_CFG_GROUP, FBCON_CFG_VERBOSE, NULL);

    g_free(filename);
}

static void fbcon_write_config(void)
{
    char *filename;
    GKeyFile *cfg;

    cfg = g_key_file_new();
    filename = g_strdup_printf("%s/%s", getenv("HOME"), FBCON_CFG_FILE);
    g_key_file_load_from_file(cfg, filename, G_KEY_FILE_KEEP_COMMENTS, NULL);

    g_key_file_set_string(cfg, FBCON_CFG_GROUP, FBCON_CFG_FONT_FACE, font_name);
    g_key_file_set_integer(cfg, FBCON_CFG_GROUP, FBCON_CFG_FONT_SIZE, font_size);
    g_key_file_set_boolean(cfg, FBCON_CFG_GROUP, FBCON_CFG_VERBOSE, verbose);

    g_key_file_save_to_file(cfg, filename, NULL);
    g_free(filename);
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

static void cleanup_and_exit(int code)
{
    gfx->cleanup_display();
    console_switch_cleanup();
    exit(code);
}

static void console_switch_suspend(void)
{
    libinput_suspend(kbd);
    logind_release_control();
}

static void console_switch_resume(void)
{
    gfx->restore_display();
    logind_take_control();
    libinput_resume(kbd);
    state1.clear++;
    state2.clear++;
    dirty++;
}

/* ---------------------------------------------------------------------- */

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

static const struct color black = { 0, 0, 0 };
static const struct color white = { 1, 1, 1 };

void fbcon_tsm_log_cb(void *data, const char *file, int line,
                      const char *func, const char *subs, unsigned int sev,
                      const char *format, va_list args)
{
    if (sev == 7 /* debug */ && !debug)
        return;

    fprintf(stderr, "<%d> ", sev);
    if (file)
        fprintf(stderr, "[%s:%d] ", file, line);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

void fbcon_tsm_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data)
{
    if (debug) {
        for (int i = 0; i < len; i++) {
            fprintf(stderr, "%s: 0x%02x %c\n", __func__,
                    u8[i], isprint(u8[i]) ? u8[i] : '.');
        }
    }
    write(pty, u8, len);
    dirty++;
}

struct color fbcon_tsm_color(const struct tsm_screen_attr *attr,
                             bool fg)
{
    struct color c;

    if (attr->inverse)
        fg = !fg;

    if (fg) {
        c.r = attr->fr / 255.0;
        c.g = attr->fg / 255.0;
        c.b = attr->fb / 255.0;
    } else {
        c.r = attr->br / 255.0;
        c.g = attr->bg / 255.0;
        c.b = attr->bb / 255.0;
    }
    return c;
}

int fbcon_tsm_draw_cb(struct tsm_screen *con, uint32_t id,
                      const uint32_t *ch, size_t len,
                      unsigned int width, unsigned int posx, unsigned int posy,
                      const struct tsm_screen_attr *attr,
                      tsm_age_t age, void *data)
{
    struct cairo_state *s = data;
    struct color fg, bg;
    wchar_t ws[8];
    char utf8[32];
    int i;

    if (s->age && age && age < s->age)
        return 0;

    fg = fbcon_tsm_color(attr, true);
    bg = fbcon_tsm_color(attr, false);
    if (posx == tsm_screen_get_cursor_x(con) &&
        posy == tsm_screen_get_cursor_y(con) &&
        !(tsm_screen_get_flags(con) & TSM_SCREEN_HIDE_CURSOR) &&
        !tsm_sb) {
        bg = white;
        fg = black;
    }

    /* background */
    cairo_rectangle(s->context,
                    s->tx + posx * extents.max_x_advance,
                    s->ty + posy * extents.height,
                    extents.max_x_advance * width,
                    extents.height);
    cairo_set_source_rgb(s->context, bg.r, bg.g, bg.b);
    cairo_fill(s->context);

    /* char */
    cairo_move_to(s->context,
                  s->tx + posx * extents.max_x_advance,
                  s->ty + posy * extents.height + extents.ascent);
    cairo_set_source_rgb(s->context, fg.r, fg.g, fg.b);
    for (i = 0; i < len && i < ARRAY_SIZE(ws)-1; i++)
        ws[i] = ch[i];
    ws[i] = 0;
    wcstombs(utf8, ws, sizeof(utf8));
    cairo_show_text(s->context, utf8);

    return 0;
}

static void fbcon_tsm_render(void)
{
    static bool second;
    struct cairo_state *s;
    int sw = tsm_screen_get_width(vts) * extents.max_x_advance;
    int sh = tsm_screen_get_height(vts) * extents.height;

    if (state2.surface)
        second = !second;
    s = second ? &state2 : &state1;

    if (s->clear) {
        s->clear = 0;
        cairo_set_source_rgb(s->context, 0, 0, 0);
        cairo_rectangle(s->context, 0, 0, gfx->hdisplay, gfx->vdisplay);
        cairo_fill(s->context);
        s->age = 0;
    }

    s->tx = (gfx->hdisplay - sw) / 2;
    s->ty = (gfx->vdisplay - sh) / 2;
    s->age = tsm_screen_draw(vts, fbcon_tsm_draw_cb, s);
    cairo_show_page(s->context);

    if (gfx->flush_display)
        gfx->flush_display(second);
}

static void fbcon_cairo_update_one(struct cairo_state *s,
                                   const char *font_name, int font_size)
{
    if (!s->surface)
        return;
    if (!s->context)
        s->context = cairo_create(s->surface);
    cairo_select_font_face(s->context, font_name,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(s->context, font_size);
}

static void fbcon_cairo_update(const char *font_name, int font_size)
{
    fbcon_cairo_update_one(&state1, font_name, font_size);
    fbcon_cairo_update_one(&state2, font_name, font_size);
    cairo_font_extents(state1.context, &extents);

    /* underline quirk */
    extents.height++;
}

static void fbcon_winsize(struct winsize *win)
{
    win->ws_col = gfx->hdisplay / extents.max_x_advance;
    win->ws_row = gfx->vdisplay / extents.height;
    win->ws_xpixel = extents.max_x_advance;
    win->ws_ypixel = extents.height;
}

static void fbcon_resize()
{
    struct winsize win;

    fbcon_cairo_update(font_name, font_size);
    fbcon_winsize(&win);
    tsm_screen_resize(vts, win.ws_col, win.ws_row);
    tsm_screen_clear_sb(vts);
    ioctl(pty, TIOCSWINSZ, &win);
    state1.clear++;
    state2.clear++;
    dirty++;
}

static uint32_t xkb_to_tsm_mods(struct xkb_state *state)
{
    static const struct {
        const char *xkb;
        uint32_t tsm;
    } map[] = {
        { XKB_MOD_NAME_SHIFT, TSM_SHIFT_MASK   },
        { XKB_MOD_NAME_CAPS,  TSM_LOCK_MASK    },
        { XKB_MOD_NAME_CTRL,  TSM_CONTROL_MASK },
        { XKB_MOD_NAME_ALT,   TSM_ALT_MASK     },
        { XKB_MOD_NAME_LOGO,  TSM_LOGO_MASK    },
    };
    uint32_t i, mods = 0;

    for (i = 0; i < ARRAY_SIZE(map); i++) {
        if (!xkb_state_mod_name_is_active(state, map[i].xkb,
                                          XKB_STATE_MODS_EFFECTIVE))
            continue;
        mods |= map[i].tsm;
    }
    return mods;
}

static void fbcon_handle_keydown(struct xkb_state *state,
                                 xkb_keycode_t key)
{
    xkb_keysym_t sym = xkb_state_key_get_one_sym(state, key);
    uint32_t utf32 = xkb_state_key_get_utf32(state, key);
    uint32_t mods = xkb_to_tsm_mods(state);
    bool ctrlalt = (mods == (TSM_CONTROL_MASK | TSM_ALT_MASK));
    bool shift = (mods == TSM_SHIFT_MASK);

    /* change font size */
    if (ctrlalt && sym == XKB_KEY_plus) {
        font_size += 2;
        fbcon_resize();
        fbcon_write_config();
        return;
    }
    if (ctrlalt && sym == XKB_KEY_minus && font_size > 8) {
        font_size -= 2;
        fbcon_resize();
        fbcon_write_config();
        return;
    }

#if 0
    /* WIP & broken */
    if (logind && ctrlalt) {
        if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
            int vt = sym - XKB_KEY_F1 + 1;
            fprintf(stderr, "console switch to vt %d\n", vt);
            console_switch_suspend();
            logind_switch_vt(vt);
            return;
        }
    }
#endif

    /* scrollback */
    if (shift && sym == XKB_KEY_Up) {
        tsm_screen_sb_up(vts, 1);
        tsm_sb = true; dirty++;
        return;
    }
    if (shift && sym == XKB_KEY_Down) {
        tsm_screen_sb_down(vts, 1);
        tsm_sb = true; dirty++;
        return;
    }
    if (shift && sym == XKB_KEY_Page_Up) {
        tsm_screen_sb_page_up(vts, 1);
        tsm_sb = true; dirty++;
        return;
    }
    if (shift && sym == XKB_KEY_Page_Down) {
        tsm_screen_sb_page_down(vts, 1);
        tsm_sb = true; dirty++;
        return;
    }
    if (tsm_sb) {
        tsm_screen_sb_reset(vts);
        tsm_sb = false; dirty++;
    }

    /* send key to terminal */
    if (!utf32)
        utf32 = TSM_VTE_INVALID;
    tsm_vte_handle_keyboard(vte, sym, 0, mods, utf32);
    dirty++;
}

static void fbcon_child_exec(struct winsize *win,
                             enum fbcon_mode mode,
                             int argc, char **argv)
{
    struct passwd *pwent;
    char *shell;

    /* reset terminal */
    fprintf(stderr, "\x1b[0m");

    /* check for errors */
    if (libinput_deverror != 0 || libinput_devcount == 0) {
        fprintf(stderr, "ERROR: failed to open input devices (%d ok, %d failed)\n",
                libinput_devcount, libinput_deverror);
        return;
    }

    if (verbose) {
        fprintf(stderr, "#\n");
        fprintf(stderr, "# This is fbcon @%s\n", seat_name);
        fprintf(stderr, "#   device: %s\n", gfx->devpath);
        fprintf(stderr, "#   format: %c%c%c%c\n",
                (gfx->fmt->fourcc >>  0) & 0xff,
                (gfx->fmt->fourcc >>  8) & 0xff,
                (gfx->fmt->fourcc >> 16) & 0xff,
                (gfx->fmt->fourcc >> 24) & 0xff);
        fprintf(stderr, "#   font:   %s-%d\n", font_name, font_size);
        fprintf(stderr, "#   size:   %dx%d\n", win->ws_col, win->ws_row);
        fprintf(stderr, "#\n");
    }

    /* prepare environment, run shell */
    setenv("TERM", "xterm-256color", true);

    switch (mode) {
    case FBCON_MODE_EXEC:
        execvp(argv[0], argv);
        fprintf(stderr, "failed to exec %s: %s\n",
                argv[0], strerror(errno));
        break;
    case FBCON_MODE_SHELL:
    default:
        pwent = getpwent();
        shell = strdup(pwent->pw_shell);
        shell = strrchr(shell, '/');
        *shell = '-';
        execl(pwent->pw_shell, shell, NULL);
        fprintf(stderr, "failed to exec %s: %s\n",
                pwent->pw_shell, strerror(errno));
        break;
    }
}

/* ---------------------------------------------------------------------- */

static void usage(FILE *fp)
{
    fprintf(fp,
            "\n"
            "usage: fbcon [ options ]\n"
            "\n"
            "options:\n"
            "  -h         print this text\n"
            "  -s         run shell (default)\n"
            "  -e <cmd>   run command\n"
            "\n");
}

int main(int argc, char *argv[])
{
    struct udev_enumerate *uenum;
    struct udev_list_entry *ulist, *uentry;
    struct winsize win;
    const char *drm_node = NULL;
    const char *fb_node = NULL;
    const char *xdg_seat, *xdg_session_id;
    enum fbcon_mode mode = FBCON_MODE_SHELL;
    int c, status, input, dbus = 0;
    pid_t child;

    setlocale(LC_ALL,"");
    fbcon_read_config();

    for (;;) {
        c = getopt(argc, argv, "hse");
        if (c == -1)
            break;
        switch (c) {
        case 's':
            mode = FBCON_MODE_SHELL;
            break;
        case 'e':
            mode = FBCON_MODE_EXEC;
            break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    xdg_seat = getenv("XDG_SEAT");
    xdg_session_id = getenv("XDG_SESSION_ID");
    if (xdg_seat)
        seat_name = xdg_seat;
    if (xdg_seat && xdg_session_id) {
        if (logind_init() == 0) {
            dbus = logind_dbus_fd();
            logind = true;
        }
    }

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
    state1.surface = cairo_image_surface_create_for_data(gfx->mem,
                                                         gfx->fmt->cairo,
                                                         gfx->hdisplay,
                                                         gfx->vdisplay,
                                                         gfx->stride);
    if (gfx->mem2) {
        state2.surface = cairo_image_surface_create_for_data(gfx->mem2,
                                                             gfx->fmt->cairo,
                                                             gfx->hdisplay,
                                                             gfx->vdisplay,
                                                             gfx->stride);
    }
    fbcon_cairo_update(font_name, font_size);

    /* init libinput */
    kbd = libinput_udev_create_context(logind
                                       ? &libinput_if_logind
                                       : &libinput_if_default,
                                       NULL, udev);
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
    fbcon_winsize(&win);

    tsm_screen_new(&vts, fbcon_tsm_log_cb, NULL);
    tsm_screen_resize(vts, win.ws_col, win.ws_row);
    tsm_screen_set_max_sb(vts, 10000);
    tsm_vte_new(&vte, vts, fbcon_tsm_write_cb, NULL, fbcon_tsm_log_cb, NULL);

    /* run shell */
    child = forkpty(&pty, NULL, NULL, &win);
    if (0 == child) {
        fbcon_child_exec(&win, mode, argc - optind, argv + optind);
        /* only reached on errors ... */
        sleep(3);
        exit(1);
    }

    /* parent */
    state1.clear++;
    state2.clear++;
    dirty++;
    for (;;) {
        fd_set set;
        int rc, max;

        if (dirty) {
            fbcon_tsm_render();
            dirty = 0;
        }

        max = 0;
        FD_ZERO(&set);
        FD_SET(pty, &set);
        if (max < pty)
            max = pty;
        FD_SET(input, &set);
        if (max < input)
            max = input;
        if (logind) {
            FD_SET(dbus, &set);
            if (max < dbus)
                max = dbus;
        }

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
            if (rc > 0) {
                tsm_vte_input(vte, buf, rc);
                dirty++;
            }
        }

        if (FD_ISSET(input, &set)) {
            struct libinput_event *evt;
            struct libinput_event_keyboard *kevt;
            xkb_keycode_t key;
            bool down;

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
                        fbcon_handle_keydown(state, key);
                    }
                    break;
                default:
                    /* ignore event */
                    break;
                }
                libinput_event_destroy(evt);
            }
        }

        if (logind && FD_ISSET(dbus, &set)) {
            logind_dbus_input();
        }
    }

    if (child == waitpid(child, &status, WNOHANG)) {
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            /* keep error message on the screen for a moment */
            sleep(3);
        }
    }
    cleanup_and_exit(0);
    return 0;
}
