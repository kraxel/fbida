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

#include "logind.h"
#include "kbd.h"

#ifdef HAVE_SYSTEMD

/* ---------------------------------------------------------------------- */

static int logind_debug = 0;
static sd_bus *logind_dbus = NULL;
static sd_bus_slot *logind_signals;
static const char *logind_session_path;

static void (*session_suspend)(void);
static void (*session_resume)(void);

/* ---------------------------------------------------------------------- */

static void logind_pause_device_complete(unsigned int maj,
                                         unsigned int min)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    if (logind_debug)
        fprintf(stderr, "call   : PauseDeviceComplete(%d,%d)\n", maj, min);
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "PauseDeviceComplete",
                           &error,
                           &m,
                           "uu",
                           maj,
                           min);
    if (r < 0) {
        fprintf(stderr, "error  : PauseDeviceComplete failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
        return;
    }

    sd_bus_message_unref(m);
}

static int logind_prop_cb(sd_bus_message *m, void *data, sd_bus_error *ret_err)
{
    const char *intf, *prop, *unused;
    bool active;
    char type;
    int err;

    err = sd_bus_message_read(m, "s", &intf);
    if (err < 0)
        return err;

    err = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    while ((err = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        err = sd_bus_message_read(m, "s", &prop);
        if (err < 0)
            return err;
        err = sd_bus_message_peek_type(m, &type, &unused);
        if (err < 0)
            return err;
        /* Need to enter the variant, regarless we want it or not */
        err = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, unused);
        if (err < 0)
            return err;
        if (strcmp(prop, "Active") == 0) {
            err = sd_bus_message_read(m, "b", &active);
            if (err < 0)
                return err;
            if (!active) {
                if (logind_debug)
                    fprintf(stderr, "prop   : Active off (callback %s)\n",
                            session_suspend ? "yes" : "no");
                if (session_suspend)
                    session_suspend();
            }
            if (active) {
                if (logind_debug)
                    fprintf(stderr, "prop   : Active on (callback %s)\n",
                            session_resume ? "yes" : "no");
                if (session_resume)
                    session_resume();
            }
        } else {
            err = sd_bus_message_skip(m, unused);
            if (err < 0)
                return err;
        }
        /* variant */
        err = sd_bus_message_exit_container(m);
        if (err < 0)
            return err;
        /* dict entry */
        err = sd_bus_message_exit_container(m);
        if (err < 0)
            return err;
    }
    /* array */
    err = sd_bus_message_exit_container(m);
    if (err < 0)
        return err;
    return 0;
}

static int logind_session_cb(sd_bus_message *m, void *data, sd_bus_error *ret_err)
{
    const char *member = sd_bus_message_get_member(m);
    const char *type;
    unsigned int maj, min;
    int r;

    if (strcmp(member, "PauseDevice") == 0) {
        r = sd_bus_message_read(m, "uus", &maj, &min, &type);
        if (r < 0) {
            fprintf(stderr, "error  : parsing PauseDevice msg failed\n");
            return r;
        }
        if (logind_debug)
            fprintf(stderr, "signal : %s(%d,%d,%s)\n", member, maj, min, type);
        if (strcmp(type, "pause") == 0)
            logind_pause_device_complete(maj, min);
    } else {
        fprintf(stderr, "signal : %s(...)\n", member);
    }

    return 0;
}

int logind_init(bool take_control,
                void (*suspend)(void),
                void (*resume)(void))
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    const char *session_id;
    int r;

    r = sd_bus_open_system(&logind_dbus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        return -1;
    }

    if (take_control) {
        r = logind_take_control();
        if (r < 0)
            goto err;
    }

    session_id = getenv("XDG_SESSION_ID");
    if (logind_debug)
        fprintf(stderr, "call   : GetSession(%s)\n", session_id);
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "GetSession",
                           &error,
                           &m,
                           "s",
                           session_id);
    if (r < 0) {
        fprintf(stderr, "error  : GetSession failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
        goto err;
    }
    r = sd_bus_message_read(m, "o", &logind_session_path);
    if (r < 0) {
        fprintf(stderr, "error  : Parsing GetSession reply failed: %s\n",
                strerror(-r));
        goto err;
    }
    sd_bus_message_unref(m);

    sd_bus_match_signal(logind_dbus, &logind_signals,
                        "org.freedesktop.login1",
                        logind_session_path,
                        NULL,
                        "PropertiesChanged",
                        logind_prop_cb,
                        NULL);

    sd_bus_match_signal(logind_dbus, &logind_signals,
                        "org.freedesktop.login1",
                        NULL,
                        "org.freedesktop.login1.Session",
                        NULL,
                        logind_session_cb,
                        NULL);

    session_suspend = suspend;
    session_resume = resume;

    fprintf(stderr, "Opening input devices via logind.\n");
    return 0;

err:
    sd_bus_unref(logind_dbus);
    logind_dbus = NULL;
    return -1;
}

int logind_dbus_fd(void)
{
    if (!logind_dbus)
        return -1;
    return sd_bus_get_fd(logind_dbus);
}

void logind_dbus_input(void)
{
    int ret;

    if (!logind_dbus)
        return;

    do {
        ret = sd_bus_process(logind_dbus, NULL);
    } while (ret > 0);
}

int logind_switch_vt(int vt)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    if (!logind_dbus)
        return -1;

    if (logind_debug)
        fprintf(stderr, "call   : SwitchTo(%d)\n", vt);
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/seat/self",
                           "org.freedesktop.login1.Seat",
                           "SwitchTo",
                           &error,
                           &m,
                           "u",
                           vt);
    if (r < 0) {
        fprintf(stderr, "error  : SwitchTo failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
    }
    sd_bus_message_unref(m);

    return r;
}

int logind_take_control(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    if (!logind_dbus)
        return -1;

    if (logind_debug)
        fprintf(stderr, "call   : TakeControl()\n");
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "TakeControl",
                           &error,
                           &m,
                           "b",
                           false);
    if (r < 0) {
        fprintf(stderr, "error  : TakeControl failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
    }
    sd_bus_message_unref(m);

    return r;
}

int logind_release_control(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    if (!logind_dbus)
        return -1;

    if (logind_debug)
        fprintf(stderr, "call   : ReleaseControl()\n");
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "ReleaseControl",
                           &error,
                           &m,
                           "");
    if (r < 0) {
        fprintf(stderr, "error  : ReleaseControl failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
    }
    sd_bus_message_unref(m);

    return r;
}

int logind_open(const char *path, int flags, void *user_data)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    struct stat st;
    unsigned int maj, min;
    int inactive;
    int handle, fd, r;

    if (!logind_dbus)
        return -1;

    r = stat(path, &st);
    if (r < 0) {
        fprintf(stderr, "stat %s failed: %s\n", path, strerror(errno));
        libinput_deverror++;
        return -1;
    }

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);
    if (logind_debug)
        fprintf(stderr, "call   : TakeDevice(%d,%d)\n", maj, min);
    r = sd_bus_call_method(logind_dbus,
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
        fprintf(stderr, "error  : TakeDevice failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
        libinput_deverror++;
        return -1;
    }

    handle = -1;
    inactive = -1;
    r = sd_bus_message_read(m, "hb", &handle, &inactive);
    if (r < 0) {
        fd = -1;
        fprintf(stderr, "error  : Parsing TakeDevice reply failed: %s\n",
                strerror(-r));
        libinput_deverror++;
    } else {
        fd = fcntl(handle, F_DUPFD_CLOEXEC, 0);
        fprintf(stderr, "open %s: got fd %d via logind.\n",
                path, fd);
        libinput_devcount++;
    }
    sd_bus_message_unref(m);

    return fd;
}

void logind_close(int fd, void *user_data)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    struct stat st;
    unsigned int maj, min;
    int r;

    if (!logind_dbus)
        return;

    r = fstat(fd, &st);
    if (r < 0) {
        fprintf(stderr, "fstat failed: %s\n", strerror(errno));
        return;
    }
    close(fd);

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);
    if (logind_debug)
        fprintf(stderr, "call   : ReleaseDevice(%d,%d)\n", maj, min);
    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "ReleaseDevice",
                           &error,
                           &m,
                           "uu",
                           maj,
                           min);
    if (r < 0) {
        fprintf(stderr, "error  : ReleaseDevice failed: %s\n",
                error.message);
        sd_bus_error_free(&error);
    }
    sd_bus_message_unref(m);
    libinput_devcount--;
    return;
}

#else

/* ---------------------------------------------------------------------- */

int logind_init(bool take_control,
                void (*suspend)(void),
                void (*resume)(void))
{
    fprintf(stderr, "warning: compiled without logind support.\n");
    return -1;
}

int logind_dbus_fd(void)
{
    return -1;
}

void logind_dbus_input(void)
{
}

int logind_switch_vt(int vt)
{
    return -1;
}

int logind_take_control(void)
{
    return -1;
}

int logind_release_control(void)
{
    return -1;
}

int logind_open(const char *path, int flags, void *user_data)
{
    fprintf(stderr, "error: compiled without logind support.\n");
    libinput_deverror++;
    return -1;
}

void logind_close(int fd, void *user_data)
{
}

#endif

/* ---------------------------------------------------------------------- */

const struct libinput_interface libinput_if_logind = {
    .open_restricted  = logind_open,
    .close_restricted = logind_close,
};

int device_open(const char *device)
{
    int saved_errno, fd;

    fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        saved_errno = errno;
        fd = logind_open(device, 0, NULL);
        if (fd < 0) {
            errno = saved_errno;
            return -1;
        } else {
            fprintf(stderr, "%s: got handle from logind\n", device);
        }
    }
    return fd;
}
