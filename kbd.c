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

#include "config.h"
#ifdef HAVE_SYSTEMD
# include <systemd/sd-bus.h>
#endif

#include "kbd.h"

/* ---------------------------------------------------------------------- */

struct termctrl {
    const char *seq;
    uint32_t code;
    uint32_t mod;
};

static struct termctrl termctrl[] = {
    { .seq = "a", .code = KEY_A },
    { .seq = "b", .code = KEY_B },
    { .seq = "c", .code = KEY_C },
    { .seq = "d", .code = KEY_D },
    { .seq = "e", .code = KEY_E },
    { .seq = "f", .code = KEY_F },
    { .seq = "g", .code = KEY_G },
    { .seq = "h", .code = KEY_H },
    { .seq = "i", .code = KEY_I },
    { .seq = "j", .code = KEY_J },
    { .seq = "k", .code = KEY_K },
    { .seq = "l", .code = KEY_L },
    { .seq = "m", .code = KEY_M },
    { .seq = "n", .code = KEY_N },
    { .seq = "o", .code = KEY_O },
    { .seq = "p", .code = KEY_P },
    { .seq = "q", .code = KEY_Q },
    { .seq = "r", .code = KEY_R },
    { .seq = "s", .code = KEY_S },
    { .seq = "t", .code = KEY_T },
    { .seq = "u", .code = KEY_U },
    { .seq = "v", .code = KEY_V },
    { .seq = "w", .code = KEY_W },
    { .seq = "x", .code = KEY_X },
    { .seq = "y", .code = KEY_Y },
    { .seq = "z", .code = KEY_Z },

    { .seq = "A", .code = KEY_A, .mod = KEY_MOD_SHIFT },
    { .seq = "B", .code = KEY_B, .mod = KEY_MOD_SHIFT },
    { .seq = "C", .code = KEY_C, .mod = KEY_MOD_SHIFT },
    { .seq = "D", .code = KEY_D, .mod = KEY_MOD_SHIFT },
    { .seq = "E", .code = KEY_E, .mod = KEY_MOD_SHIFT },
    { .seq = "F", .code = KEY_F, .mod = KEY_MOD_SHIFT },
    { .seq = "G", .code = KEY_G, .mod = KEY_MOD_SHIFT },
    { .seq = "H", .code = KEY_H, .mod = KEY_MOD_SHIFT },
    { .seq = "I", .code = KEY_I, .mod = KEY_MOD_SHIFT },
    { .seq = "J", .code = KEY_J, .mod = KEY_MOD_SHIFT },
    { .seq = "K", .code = KEY_K, .mod = KEY_MOD_SHIFT },
    { .seq = "L", .code = KEY_L, .mod = KEY_MOD_SHIFT },
    { .seq = "M", .code = KEY_M, .mod = KEY_MOD_SHIFT },
    { .seq = "N", .code = KEY_N, .mod = KEY_MOD_SHIFT },
    { .seq = "O", .code = KEY_O, .mod = KEY_MOD_SHIFT },
    { .seq = "P", .code = KEY_P, .mod = KEY_MOD_SHIFT },
    { .seq = "Q", .code = KEY_Q, .mod = KEY_MOD_SHIFT },
    { .seq = "R", .code = KEY_R, .mod = KEY_MOD_SHIFT },
    { .seq = "S", .code = KEY_S, .mod = KEY_MOD_SHIFT },
    { .seq = "T", .code = KEY_T, .mod = KEY_MOD_SHIFT },
    { .seq = "U", .code = KEY_U, .mod = KEY_MOD_SHIFT },
    { .seq = "V", .code = KEY_V, .mod = KEY_MOD_SHIFT },
    { .seq = "W", .code = KEY_W, .mod = KEY_MOD_SHIFT },
    { .seq = "X", .code = KEY_X, .mod = KEY_MOD_SHIFT },
    { .seq = "Y", .code = KEY_Y, .mod = KEY_MOD_SHIFT },
    { .seq = "Z", .code = KEY_Z, .mod = KEY_MOD_SHIFT },

    { .seq = "\x01", .code = KEY_A, .mod = KEY_MOD_CTRL },
    { .seq = "\x02", .code = KEY_B, .mod = KEY_MOD_CTRL },
    { .seq = "\x03", .code = KEY_C, .mod = KEY_MOD_CTRL },
    { .seq = "\x04", .code = KEY_D, .mod = KEY_MOD_CTRL },
    { .seq = "\x05", .code = KEY_E, .mod = KEY_MOD_CTRL },
    { .seq = "\x06", .code = KEY_F, .mod = KEY_MOD_CTRL },
    { .seq = "\x07", .code = KEY_G, .mod = KEY_MOD_CTRL },
    { .seq = "\x08", .code = KEY_H, .mod = KEY_MOD_CTRL },
    { .seq = "\x09", .code = KEY_I, .mod = KEY_MOD_CTRL },
    { .seq = "\x0a", .code = KEY_ENTER },
    { .seq = "\x0b", .code = KEY_K, .mod = KEY_MOD_CTRL },
    { .seq = "\x0c", .code = KEY_L, .mod = KEY_MOD_CTRL },
    { .seq = "\x0d", .code = KEY_M, .mod = KEY_MOD_CTRL },
    { .seq = "\x0e", .code = KEY_N, .mod = KEY_MOD_CTRL },
    { .seq = "\x0f", .code = KEY_O, .mod = KEY_MOD_CTRL },
    { .seq = "\x10", .code = KEY_P, .mod = KEY_MOD_CTRL },
    { .seq = "\x11", .code = KEY_Q, .mod = KEY_MOD_CTRL },
    { .seq = "\x12", .code = KEY_R, .mod = KEY_MOD_CTRL },
    { .seq = "\x13", .code = KEY_S, .mod = KEY_MOD_CTRL },
    { .seq = "\x14", .code = KEY_T, .mod = KEY_MOD_CTRL },
    { .seq = "\x15", .code = KEY_U, .mod = KEY_MOD_CTRL },
    { .seq = "\x16", .code = KEY_V, .mod = KEY_MOD_CTRL },
    { .seq = "\x17", .code = KEY_W, .mod = KEY_MOD_CTRL },
    { .seq = "\x18", .code = KEY_X, .mod = KEY_MOD_CTRL },
    { .seq = "\x19", .code = KEY_Y, .mod = KEY_MOD_CTRL },
    { .seq = "\x1a", .code = KEY_Z, .mod = KEY_MOD_CTRL },

    { .seq = "0", .code = KEY_0 },
    { .seq = "1", .code = KEY_1 },
    { .seq = "2", .code = KEY_2 },
    { .seq = "3", .code = KEY_3 },
    { .seq = "4", .code = KEY_4 },
    { .seq = "5", .code = KEY_5 },
    { .seq = "6", .code = KEY_6 },
    { .seq = "7", .code = KEY_7 },
    { .seq = "8", .code = KEY_8 },
    { .seq = "9", .code = KEY_9 },

    { .seq = " ",         .code = KEY_SPACE    },
    { .seq = "\x1b",      .code = KEY_ESC      },
    { .seq = "+",         .code = KEY_KPPLUS   },
    { .seq = "-",         .code = KEY_KPMINUS  },
    { .seq = "\x7f",      .code = KEY_BACKSPACE},

    { .seq = "\x1b[A",    .code = KEY_UP       },
    { .seq = "\x1b[B",    .code = KEY_DOWN     },
    { .seq = "\x1b[C",    .code = KEY_RIGHT    },
    { .seq = "\x1b[D",    .code = KEY_LEFT     },
    { .seq = "\x1b[F",    .code = KEY_END      },
    { .seq = "\x1b[H",    .code = KEY_HOME     },

    { .seq = "\x1b[1~",   .code = KEY_HOME     },
    { .seq = "\x1b[2~",   .code = KEY_INSERT   },
    { .seq = "\x1b[3~",   .code = KEY_DELETE   },
    { .seq = "\x1b[4~",   .code = KEY_END      },
    { .seq = "\x1b[5~",   .code = KEY_PAGEUP   },
    { .seq = "\x1b[6~",   .code = KEY_PAGEDOWN },

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
    return KEY_RESERVED;
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

#ifdef HAVE_SYSTEMD

static sd_bus *dbus = NULL;

void logind_init(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    r = sd_bus_open_system(&dbus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        return;
    }

    r = sd_bus_call_method(dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "TakeControl",
                           &error,
                           &m,
                           "b",
                           false);
    if (r < 0) {
        fprintf(stderr, "TakeControl failed: %s\n", error.message);
        sd_bus_error_free(&error);
        return;
    }
}

bool use_logind(void)
{
    return dbus != NULL;
}

int logind_open(const char *path)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    struct stat st;
    unsigned int maj, min;
    bool unused;
    int handle, fd, r;

    r = stat(path, &st);
    if (r < 0) {
        fprintf(stderr, "stat %s failed: %s\n", path, strerror(errno));
        return -1;
    }

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);
    r = sd_bus_call_method(dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "TakeDevice",
                           &error,
                           &m,
                           "uu",
                           maj,
                           min);
    if (r < 0) {
        fprintf(stderr, "TakeDevice failed: %s\n", error.message);
        sd_bus_error_free(&error);
        return -1;
    }

    r = sd_bus_message_read(m, "hb", &handle, &unused);
    if (r < 0) {
        fprintf(stderr, "Parse TakeDevice reply failed: %s\n", strerror(-r));
        fd = -1;
    }
    fd = dup(handle);
    sd_bus_message_unref(m);

    return fd;
}

void logind_close(int fd)
{
    /* FIXME */
}

#else

void logind_init(void)
{
    fprintf(stderr, "warning: compiled without logind support.\n");
}

bool use_logind(void)
{
    return false;
}

int logind_open(const char *path)
{
    errno = ENOSYS;
    return -1;
}

void logind_close(int fd)
{
}

#endif

/* ---------------------------------------------------------------------- */

int libinput_devcount;
int libinput_deverror;

static int open_restricted(const char *path, int flags, void *user_data)
{
    int fd;

    if (use_logind()) {
        fd = logind_open(path);
        if (fd < 0)
            libinput_deverror++;
        else
            libinput_devcount++;
        return fd;
    }

    fd = open(path, flags | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "kbd: open %s: %s\n", path, strerror(errno));
        libinput_deverror++;
        return fd;
    }

    fprintf(stderr, "kbd: using %s\n", path);
    ioctl(fd, EVIOCGRAB, 1);
    libinput_devcount++;
    return fd;
}

static void close_restricted(int fd, void *user_data)
{
    if (use_logind()) {
        logind_close(fd);
        libinput_devcount--;
        return;
    }

    ioctl(fd, EVIOCGRAB, 0);
    close(fd);
    libinput_devcount--;
}

const struct libinput_interface libinput_interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

static struct libinput *ctx;

void kbd_init(int use_libinput, dev_t gfx)
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
        ctx = libinput_udev_create_context(&libinput_interface, NULL, udev);
        libinput_udev_assign_seat(ctx, seat);
        fprintf(stderr, "kbd: using libinput (%d devices, %s)\n",
                libinput_devcount, seat);
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
    struct libinput_event_pointer *ptr;
    int rc;

    memset(buf, 0, len);
    *keycode = KEY_RESERVED;
    *modifier = 0;

    if (ctx) {
        rc = libinput_dispatch(ctx);
        if (rc < 0)
            return -1;
        while ((evt = libinput_get_event(ctx)) != NULL) {
            switch (libinput_event_get_type(evt)) {
            case LIBINPUT_EVENT_KEYBOARD_KEY:
                kbd = libinput_event_get_keyboard_event(evt);
                if (libinput_event_keyboard_get_key_state(kbd))
                    *keycode = libinput_event_keyboard_get_key(kbd);
                /* TODO: track modifier state */
                /* TODO: fill buf with typed chars */
                break;
            case LIBINPUT_EVENT_POINTER_BUTTON:
                ptr = libinput_event_get_pointer_event(evt);
                if (libinput_event_pointer_get_button_state(ptr))
                    *keycode = libinput_event_pointer_get_button(ptr);
                break;
            default:
                /* ignore event */
                break;
            }
            libinput_event_destroy(evt);
        }
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
