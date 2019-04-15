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

static struct {
    uint32_t code;
    const char *name;
} xkbname_list[] = {
#include "xkbname.h"
};

const char *xkbname(uint32_t code)
{
    int keycount = sizeof(xkbname_list)/sizeof(xkbname_list[0]);
    int i;

    for (i = 0; i < keycount; i++)
        if (xkbname_list[i].code == code)
            return xkbname_list[i].name;
    return "unknown";
}

static void usage(FILE *fp)
{
    fprintf(fp,
            "\n"
            "usage: kbdtest [ options ]\n"
            "\n"
            "options:\n"
            "  -h         print this text\n"
            "  -i         use libinput\n"
            "\n");
}

int main(int argc, char *argv[])
{
    uint32_t code, mod;
    char key[32];
    bool use_libinput = false;
    int rc, i, c;

    for (;;) {
        c = getopt(argc, argv, "hi");
        if (c == -1)
            break;
        switch (c) {
        case 'i':
            use_libinput = true;
            break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    kbd_init(use_libinput, 0);

    for (;;) {
        kbd_wait(10);

        rc = kbd_read(key, sizeof(key), &code, &mod);
        if (rc < 0) {
            /* EOF */
            break;
        }
        if (code == XKB_KEY_VoidSymbol)
            continue;

        fprintf(stderr, "key: \"");
        for (i = 0; key[i] != 0; i++) {
            fprintf(stderr, "%c", isprint(key[i]) ? key[i] : '.');
        }
        fprintf(stderr, "\" -> %s", xkbname(code));
        if (mod & KEY_MOD_SHIFT)
            fprintf(stderr, " +shift");
        if (mod & KEY_MOD_CTRL)
            fprintf(stderr, " +ctrl");
        fprintf(stderr, "\n");
    }

    kbd_fini();
    return 0;
}
