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

static sd_bus *logind_dbus = NULL;

int logind_init(void)
{
    int r;

    r = sd_bus_open_system(&logind_dbus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        return -1;
    }

    r = logind_take_control();
    if (r < 0) {
        sd_bus_unref(logind_dbus);
        logind_dbus = NULL;
        return -1;
    }

    fprintf(stderr, "Opening input devices via logind.\n");
    return 0;
}

int logind_dbus_fd(void)
{
    if (!logind_dbus)
        return -1;
    return sd_bus_get_fd(logind_dbus);
}

void logind_dbus_input(void)
{
    sd_bus_message *m = NULL;
    int ret;

    if (!logind_dbus)
        return;

    do {
        ret = sd_bus_process(logind_dbus, &m);
        fprintf(stderr, "%s: path %s\n", __func__,
                sd_bus_message_get_path(m));
        sd_bus_message_unref(m);
    } while (ret > 0);
}

int logind_take_control(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r;

    if (!logind_dbus)
        return -1;

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
        fprintf(stderr, "TakeControl failed: %s\n", error.message);
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

    r = sd_bus_call_method(logind_dbus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "ReleaseControl",
                           &error,
                           &m,
                           "");
    if (r < 0) {
        fprintf(stderr, "ReleaseControl failed: %s\n", error.message);
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

    r = stat(path, &st);
    if (r < 0) {
        fprintf(stderr, "stat %s failed: %s\n", path, strerror(errno));
        libinput_deverror++;
        return -1;
    }

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);
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
        fprintf(stderr, "TakeDevice failed: %s\n", error.message);
        sd_bus_error_free(&error);
        libinput_deverror++;
        return -1;
    }

    handle = -1;
    inactive = -1;
    r = sd_bus_message_read(m, "hb", &handle, &inactive);
    if (r < 0) {
        fd = -1;
        fprintf(stderr, "Parsing TakeDevice reply failed: %s\n", strerror(-r));
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

    r = fstat(fd, &st);
    if (r < 0) {
        fprintf(stderr, "fstat failed: %s\n", strerror(errno));
        return;
    }
    close(fd);

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);
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
        fprintf(stderr, "ReleaseDevice failed: %s\n", error.message);
        sd_bus_error_free(&error);
    }
    sd_bus_message_unref(m);
    libinput_devcount--;
    return;
}

#else

/* ---------------------------------------------------------------------- */

int logind_init(void)
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

int logind_take_control(void)
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
