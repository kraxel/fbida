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

#include "logind.h"

/* ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *xdg_seat, *xdg_session_id;
    int dbus;

    xdg_seat = getenv("XDG_SEAT");
    xdg_session_id = getenv("XDG_SESSION_ID");
    if (!xdg_seat || !xdg_session_id) {
        fprintf(stderr, "session id or seat not found\n");
        exit(1);
    }

    fprintf(stderr, "session %s at %s\n",
            xdg_session_id, xdg_seat);
    if (logind_init(false, NULL, NULL) != 0) {
        fprintf(stderr, "logind init failed\n");
        exit(1);
    }
    dbus = logind_dbus_fd();

#if 0
    logind_open("/dev/dri/card0", 0, NULL);

    const char *xdg_vtnr = getenv("XDG_VTNR");
    if (xdg_vtnr) {
        int vt = atoi(xdg_vtnr);
        vt++;
        fprintf(stderr, "switch to vt %d\n", vt);
        logind_switch_vt(vt);
    }
#endif

    for (;;) {
        fd_set set;
        int rc, max;

        max = 0;
        FD_ZERO(&set);
        FD_SET(dbus, &set);
        if (max < dbus)
            max = dbus;

        rc = select(max+ 1, &set, NULL, NULL, NULL);
        if (rc < 0)
            break;

        if (FD_ISSET(dbus, &set)) {
            logind_dbus_input();
        }
    }
    return 0;
}
