#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <termios.h>

#include <sys/stat.h>

#include "kbd.h"
#include "logind.h"

#ifdef SYSTEM_LINUX
# include <linux/input.h>
#endif

/* ---------------------------------------------------------------------- */

struct xkb_context *xkb_ctx;
struct xkb_keymap *xkb_map;
struct xkb_state *xkb_state;
struct xkb_rule_names xkb_layout = {
    .rules   = NULL,
    .model   = "pc105",
    .layout  = "us",
    .variant = NULL,
    .options = NULL,
};

void xkb_configure(void)
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
            xkb_layout.variant = strdup(v);
        }
        xkb_layout.layout = strdup(m);
    }
    fclose(fp);
}

void xkb_init(void)
{
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_map = xkb_keymap_new_from_names(xkb_ctx, &xkb_layout,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkb_map) {
        xkb_layout.variant = NULL;
        xkb_map = xkb_keymap_new_from_names(xkb_ctx, &xkb_layout,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!xkb_map) {
            xkb_layout.layout = "us";
            xkb_map = xkb_keymap_new_from_names(xkb_ctx, &xkb_layout,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
        }
    }
    xkb_state = xkb_state_new(xkb_map);
}

/* ---------------------------------------------------------------------- */

struct termctrl {
    const char *seq;
    uint32_t code;
    uint32_t mod;
};

static struct termctrl termctrl[] = {
    { .seq = "a", .code = XKB_KEY_A },
    { .seq = "b", .code = XKB_KEY_B },
    { .seq = "c", .code = XKB_KEY_C },
    { .seq = "d", .code = XKB_KEY_D },
    { .seq = "e", .code = XKB_KEY_E },
    { .seq = "f", .code = XKB_KEY_F },
    { .seq = "g", .code = XKB_KEY_G },
    { .seq = "h", .code = XKB_KEY_H },
    { .seq = "i", .code = XKB_KEY_I },
    { .seq = "j", .code = XKB_KEY_J },
    { .seq = "k", .code = XKB_KEY_K },
    { .seq = "l", .code = XKB_KEY_L },
    { .seq = "m", .code = XKB_KEY_M },
    { .seq = "n", .code = XKB_KEY_N },
    { .seq = "o", .code = XKB_KEY_O },
    { .seq = "p", .code = XKB_KEY_P },
    { .seq = "q", .code = XKB_KEY_Q },
    { .seq = "r", .code = XKB_KEY_R },
    { .seq = "s", .code = XKB_KEY_S },
    { .seq = "t", .code = XKB_KEY_T },
    { .seq = "u", .code = XKB_KEY_U },
    { .seq = "v", .code = XKB_KEY_V },
    { .seq = "w", .code = XKB_KEY_W },
    { .seq = "x", .code = XKB_KEY_X },
    { .seq = "y", .code = XKB_KEY_Y },
    { .seq = "z", .code = XKB_KEY_Z },

    { .seq = "A", .code = XKB_KEY_A, .mod = KEY_MOD_SHIFT },
    { .seq = "B", .code = XKB_KEY_B, .mod = KEY_MOD_SHIFT },
    { .seq = "C", .code = XKB_KEY_C, .mod = KEY_MOD_SHIFT },
    { .seq = "D", .code = XKB_KEY_D, .mod = KEY_MOD_SHIFT },
    { .seq = "E", .code = XKB_KEY_E, .mod = KEY_MOD_SHIFT },
    { .seq = "F", .code = XKB_KEY_F, .mod = KEY_MOD_SHIFT },
    { .seq = "G", .code = XKB_KEY_G, .mod = KEY_MOD_SHIFT },
    { .seq = "H", .code = XKB_KEY_H, .mod = KEY_MOD_SHIFT },
    { .seq = "I", .code = XKB_KEY_I, .mod = KEY_MOD_SHIFT },
    { .seq = "J", .code = XKB_KEY_J, .mod = KEY_MOD_SHIFT },
    { .seq = "K", .code = XKB_KEY_K, .mod = KEY_MOD_SHIFT },
    { .seq = "L", .code = XKB_KEY_L, .mod = KEY_MOD_SHIFT },
    { .seq = "M", .code = XKB_KEY_M, .mod = KEY_MOD_SHIFT },
    { .seq = "N", .code = XKB_KEY_N, .mod = KEY_MOD_SHIFT },
    { .seq = "O", .code = XKB_KEY_O, .mod = KEY_MOD_SHIFT },
    { .seq = "P", .code = XKB_KEY_P, .mod = KEY_MOD_SHIFT },
    { .seq = "Q", .code = XKB_KEY_Q, .mod = KEY_MOD_SHIFT },
    { .seq = "R", .code = XKB_KEY_R, .mod = KEY_MOD_SHIFT },
    { .seq = "S", .code = XKB_KEY_S, .mod = KEY_MOD_SHIFT },
    { .seq = "T", .code = XKB_KEY_T, .mod = KEY_MOD_SHIFT },
    { .seq = "U", .code = XKB_KEY_U, .mod = KEY_MOD_SHIFT },
    { .seq = "V", .code = XKB_KEY_V, .mod = KEY_MOD_SHIFT },
    { .seq = "W", .code = XKB_KEY_W, .mod = KEY_MOD_SHIFT },
    { .seq = "X", .code = XKB_KEY_X, .mod = KEY_MOD_SHIFT },
    { .seq = "Y", .code = XKB_KEY_Y, .mod = KEY_MOD_SHIFT },
    { .seq = "Z", .code = XKB_KEY_Z, .mod = KEY_MOD_SHIFT },

    { .seq = "\x01", .code = XKB_KEY_A, .mod = KEY_MOD_CTRL },
    { .seq = "\x02", .code = XKB_KEY_B, .mod = KEY_MOD_CTRL },
    { .seq = "\x03", .code = XKB_KEY_C, .mod = KEY_MOD_CTRL },
    { .seq = "\x04", .code = XKB_KEY_D, .mod = KEY_MOD_CTRL },
    { .seq = "\x05", .code = XKB_KEY_E, .mod = KEY_MOD_CTRL },
    { .seq = "\x06", .code = XKB_KEY_F, .mod = KEY_MOD_CTRL },
    { .seq = "\x07", .code = XKB_KEY_G, .mod = KEY_MOD_CTRL },
    { .seq = "\x08", .code = XKB_KEY_H, .mod = KEY_MOD_CTRL },
    { .seq = "\x09", .code = XKB_KEY_I, .mod = KEY_MOD_CTRL },
    { .seq = "\x0a", .code = XKB_KEY_Return },
    { .seq = "\x0b", .code = XKB_KEY_K, .mod = KEY_MOD_CTRL },
    { .seq = "\x0c", .code = XKB_KEY_L, .mod = KEY_MOD_CTRL },
    { .seq = "\x0d", .code = XKB_KEY_M, .mod = KEY_MOD_CTRL },
    { .seq = "\x0e", .code = XKB_KEY_N, .mod = KEY_MOD_CTRL },
    { .seq = "\x0f", .code = XKB_KEY_O, .mod = KEY_MOD_CTRL },
    { .seq = "\x10", .code = XKB_KEY_P, .mod = KEY_MOD_CTRL },
    { .seq = "\x11", .code = XKB_KEY_Q, .mod = KEY_MOD_CTRL },
    { .seq = "\x12", .code = XKB_KEY_R, .mod = KEY_MOD_CTRL },
    { .seq = "\x13", .code = XKB_KEY_S, .mod = KEY_MOD_CTRL },
    { .seq = "\x14", .code = XKB_KEY_T, .mod = KEY_MOD_CTRL },
    { .seq = "\x15", .code = XKB_KEY_U, .mod = KEY_MOD_CTRL },
    { .seq = "\x16", .code = XKB_KEY_V, .mod = KEY_MOD_CTRL },
    { .seq = "\x17", .code = XKB_KEY_W, .mod = KEY_MOD_CTRL },
    { .seq = "\x18", .code = XKB_KEY_X, .mod = KEY_MOD_CTRL },
    { .seq = "\x19", .code = XKB_KEY_Y, .mod = KEY_MOD_CTRL },
    { .seq = "\x1a", .code = XKB_KEY_Z, .mod = KEY_MOD_CTRL },

    { .seq = "0", .code = XKB_KEY_0 },
    { .seq = "1", .code = XKB_KEY_1 },
    { .seq = "2", .code = XKB_KEY_2 },
    { .seq = "3", .code = XKB_KEY_3 },
    { .seq = "4", .code = XKB_KEY_4 },
    { .seq = "5", .code = XKB_KEY_5 },
    { .seq = "6", .code = XKB_KEY_6 },
    { .seq = "7", .code = XKB_KEY_7 },
    { .seq = "8", .code = XKB_KEY_8 },
    { .seq = "9", .code = XKB_KEY_9 },

    { .seq = " ",         .code = XKB_KEY_space        },
    { .seq = "\x1b",      .code = XKB_KEY_Escape       },
    { .seq = "+",         .code = XKB_KEY_KP_Add       },
    { .seq = "-",         .code = XKB_KEY_KP_Subtract  },
    { .seq = "\x7f",      .code = XKB_KEY_BackSpace    },

    { .seq = "\x1b[A",    .code = XKB_KEY_Up           },
    { .seq = "\x1b[B",    .code = XKB_KEY_Down         },
    { .seq = "\x1b[C",    .code = XKB_KEY_Right        },
    { .seq = "\x1b[D",    .code = XKB_KEY_Left         },
    { .seq = "\x1b[F",    .code = XKB_KEY_End          },
    { .seq = "\x1b[H",    .code = XKB_KEY_Home         },

    { .seq = "\x1b[1~",   .code = XKB_KEY_Home         },
    { .seq = "\x1b[2~",   .code = XKB_KEY_Insert       },
    { .seq = "\x1b[3~",   .code = XKB_KEY_Delete       },
    { .seq = "\x1b[4~",   .code = XKB_KEY_End          },
    { .seq = "\x1b[5~",   .code = XKB_KEY_Page_Up      },
    { .seq = "\x1b[6~",   .code = XKB_KEY_Page_Down    },

    { /* EOF */ }
};

static uint32_t tty_parse(const char *key, uint32_t *mod)
{
    int i;

    for (i = 0; termctrl[i].seq != NULL; i++) {
        if (strcmp(key, termctrl[i].seq) == 0) {
            *mod = termctrl[i].mod;
            return termctrl[i].code;
        }
    }
    *mod = 0;
    return XKB_KEY_VoidSymbol;
}

/* ---------------------------------------------------------------------- */

static struct termios  saved_attributes;
static int             saved_fl;

static void tty_raw(void)
{
    struct termios tattr;

    fcntl(STDIN_FILENO, F_GETFL, &saved_fl);
    tcgetattr (0, &saved_attributes);

    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ICANON|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);
}

static void tty_restore(void)
{
    fcntl(STDIN_FILENO, F_SETFL, saved_fl);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_attributes);
}

static int file_wait(int fd, int timeout)
{
    struct timeval limit;
    fd_set set;
    int rc;

    FD_ZERO(&set);
    FD_SET(fd, &set);
    limit.tv_sec = timeout;
    limit.tv_usec = 0;
    rc = select(fd + 1, &set, NULL, NULL,
                timeout ? &limit : NULL);
    return rc;
}

/* ---------------------------------------------------------------------- */

int libinput_devcount;
int libinput_deverror;

static int open_restricted(const char *path, int flags, void *user_data)
{
    int fd;

    fd = open(path, flags | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "kbd: open %s: %s\n", path, strerror(errno));
        libinput_deverror++;
        return fd;
    }

    fprintf(stderr, "kbd: using %s\n", path);
#ifdef EVIOCGRAB
    ioctl(fd, EVIOCGRAB, 1);
#endif
    libinput_devcount++;
    return fd;
}

static void close_restricted(int fd, void *user_data)
{
#ifdef EVIOCGRAB
    ioctl(fd, EVIOCGRAB, 0);
#endif
    close(fd);
    libinput_devcount--;
}

const struct libinput_interface libinput_if_default = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

static struct libinput *ctx;

void kbd_init(bool use_libinput, bool use_logind, dev_t gfx)
{
    struct udev        *udev;
    struct udev_device *ugfx;
    const char *seat = NULL;

    if (use_libinput) {
        udev = udev_new();
        ugfx = udev_device_new_from_devnum(udev, 'c', gfx);
        if (ugfx)
            seat = udev_device_get_property_value(ugfx, "ID_SEAT");
        if (!seat)
            seat = "seat0";
        ctx = libinput_udev_create_context(use_logind
                                           ? &libinput_if_logind
                                           : &libinput_if_default,
                                           NULL, udev);
        libinput_udev_assign_seat(ctx, seat);
        fprintf(stderr, "kbd: using libinput (%d devices, %s)\n",
                libinput_devcount, seat);
        xkb_configure();
        xkb_init();
    } else {
        fprintf(stderr, "kbd: using stdin from terminal\n");
        tty_raw();
    }
}

void kbd_fini(void)
{
    if (ctx) {
        libinput_unref(ctx);
    } else {
        tty_restore();
    }
}

int kbd_wait(int timeout)
{
    if (ctx) {
        return file_wait(libinput_get_fd(ctx), timeout);
    } else {
        return file_wait(STDIN_FILENO, timeout);
    }
}

int kbd_read(char *buf, uint32_t len,
             uint32_t *keycode, uint32_t *modifier)
{
    struct libinput_event *evt;
    struct libinput_event_keyboard *kbd;
#if 0
    struct libinput_event_pointer *ptr;
#endif
    xkb_keycode_t key;
    bool down;
    int rc, events = 0;

    memset(buf, 0, len);
    *keycode = XKB_KEY_VoidSymbol;
    *modifier = 0;

    if (ctx) {
        rc = libinput_dispatch(ctx);
        if (rc < 0)
            return -1;
        while ((evt = libinput_get_event(ctx)) != NULL) {
            switch (libinput_event_get_type(evt)) {
            case LIBINPUT_EVENT_KEYBOARD_KEY:
                kbd = libinput_event_get_keyboard_event(evt);
                key = libinput_event_keyboard_get_key(kbd) + 8;
                down = libinput_event_keyboard_get_key_state(kbd);
                xkb_state_update_key(xkb_state, key, down);
                if (down) {
                    *keycode = xkb_state_key_get_one_sym(xkb_state, key);
                    if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_SHIFT,
                                                     XKB_STATE_MODS_EFFECTIVE))
                        *modifier |= KEY_MOD_SHIFT;
                    if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL,
                                                     XKB_STATE_MODS_EFFECTIVE))
                        *modifier |= KEY_MOD_CTRL;
                }
                break;
#if 0
            case LIBINPUT_EVENT_POINTER_BUTTON:
                ptr = libinput_event_get_pointer_event(evt);
                if (libinput_event_pointer_get_button_state(ptr))
                    *keycode = libinput_event_pointer_get_button(ptr);
                break;
#endif
            default:
                /* ignore event */
                break;
            }
            libinput_event_destroy(evt);
            events++;
        }
        if (!events)
            return -1;
        return 0;
    } else {
        rc = read(STDIN_FILENO, buf, len-1);
        if (rc < 1)
            return -1;

        *keycode = tty_parse(buf, modifier);
        return rc;
    }
}

void kbd_suspend(void)
{
    if (ctx)
        libinput_suspend(ctx);
}

void kbd_resume(void)
{
    if (ctx)
        libinput_resume(ctx);
}
