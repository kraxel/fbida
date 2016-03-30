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

#include "kbd.h"

/* ---------------------------------------------------------------------- */

static const char *keyname[KEY_CNT] = {
#include "KEY.h"
};

int main(int argc, char *argv[])
{
    struct timeval limit;
    uint32_t code, mod;
    char key[32];
    fd_set set;
    int rc,i;

    tty_raw();

    for (;;) {
        FD_ZERO(&set);
        FD_SET(0, &set);
        limit.tv_sec = 10;
        limit.tv_usec = 0;
	rc = select(1, &set, NULL, NULL, &limit);
	if (0 == rc || !FD_ISSET(0,&set))
            break;

        memset(key, 0, sizeof(key));
        rc = read(0, key, sizeof(key)-1);
        if (rc < 1) {
            /* EOF */
            break;
        }

        code = kbd_parse(key, &mod);
        fprintf(stderr, "key: \"");
        for (i = 0; key[i] != 0; i++) {
            fprintf(stderr, "%c", isprint(key[i]) ? key[i] : '.');
        }
        fprintf(stderr, "\" -> %s", keyname[code]);
        if (mod & KEY_MOD_SHIFT)
            fprintf(stderr, " +shift");
        if (mod & KEY_MOD_CTRL)
            fprintf(stderr, " +ctrl");
        fprintf(stderr, "\n");
    }
    tty_restore();
    return 0;
}
