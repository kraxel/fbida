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
    uint32_t code, mod;
    char key[32];
    int rc,i;

    kbd_init();

    for (;;) {
        kbd_wait(10);

        rc = kbd_read(key, sizeof(key), &code, &mod);
        if (rc < 0) {
            /* EOF */
            break;
        }

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

    kbd_fini();
    return 0;
}
