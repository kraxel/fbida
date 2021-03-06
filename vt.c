#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "vt.h"

/* -------------------------------------------------------------------- */

#ifdef SYSTEM_LINUX

#include <linux/kd.h>
#include <linux/vt.h>

#define CONSOLE_ACTIVE    0
#define CONSOLE_REL_REQ   1
#define CONSOLE_INACTIVE  2
#define CONSOLE_ACQ_REQ   3

int console_visible = 1;

extern int debug;

static int switch_last;
static int console_switch_state = CONSOLE_ACTIVE;
static bool console_switching_active;
static int kd_mode;
static struct vt_mode vt_mode;
static struct vt_mode vt_omode;
static void (*console_suspend)(void);
static void (*console_resume)(void);

static void console_switch_signal(int signal)
{
    if (signal == SIGUSR1) {
	/* release */
	console_switch_state = CONSOLE_REL_REQ;
	if (debug)
	    write(2,"vt: SIGUSR1\n",12);
    }
    if (signal == SIGUSR2) {
	/* acquisition */
	console_switch_state = CONSOLE_ACQ_REQ;
	if (debug)
	    write(2,"vt: SIGUSR2\n",12);
    }
}

static void console_switch_release(void)
{
    ioctl(STDIN_FILENO, VT_RELDISP, 1);
    console_switch_state = CONSOLE_INACTIVE;
    if (debug)
	write(2,"vt: release\n",12);
}

static void console_switch_acquire(void)
{
    ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
    console_switch_state = CONSOLE_ACTIVE;
    if (debug)
	write(2,"vt: acquire\n",12);
}

int console_switch_init(void (*suspend)(void),
                        void (*resume)(void))
{
    struct sigaction act,old;

    console_suspend = suspend;
    console_resume = resume;

    memset(&act,0,sizeof(act));
    act.sa_handler  = console_switch_signal;
    sigemptyset(&act.sa_mask);
    sigaction(SIGUSR1,&act,&old);
    sigaction(SIGUSR2,&act,&old);

    if (-1 == ioctl(STDIN_FILENO, VT_GETMODE, &vt_omode)) {
	perror("ioctl VT_GETMODE");
        return -1;
    }
    if (-1 == ioctl(STDIN_FILENO, KDGETMODE, &kd_mode)) {
	perror("ioctl KDGETMODE");
        return -1;
    }

    if (-1 == ioctl(STDIN_FILENO, VT_GETMODE, &vt_mode)) {
	perror("ioctl VT_GETMODE");
        return -1;
    }
    vt_mode.mode   = VT_PROCESS;
    vt_mode.waitv  = 0;
    vt_mode.relsig = SIGUSR1;
    vt_mode.acqsig = SIGUSR2;
    if (-1 == ioctl(STDIN_FILENO, VT_SETMODE, &vt_mode)) {
	perror("ioctl VT_SETMODE");
        return -1;
    }
    if (-1 == ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS)) {
        ioctl(STDIN_FILENO, VT_SETMODE, &vt_omode);
	perror("ioctl KDSETMODE");
        return -1;
    }
    console_switching_active = true;
    return 0;
}

void console_switch_cleanup(void)
{
    if (!console_switching_active)
        return;

    if (-1 == ioctl(STDIN_FILENO, KDSETMODE, kd_mode))
	perror("ioctl KDSETMODE");
    if (-1 == ioctl(STDIN_FILENO, VT_SETMODE, &vt_omode))
	perror("ioctl VT_SETMODE");
    console_switching_active = false;
}

int check_console_switch(void)
{
    if (!console_switching_active)
        return 0;
    if (switch_last == console_switch_state)
        return 0;

    switch (console_switch_state) {
    case CONSOLE_REL_REQ:
        console_visible = 0;
        console_suspend();
        console_switch_release();
    case CONSOLE_INACTIVE:
	break;
    case CONSOLE_ACQ_REQ:
	console_switch_acquire();
    case CONSOLE_ACTIVE:
	console_visible = 1;
        console_resume();
	break;
    default:
	break;
    }
    switch_last = console_switch_state;
    return 1;
}

/* Hmm. radeonfb needs this. matroxfb doesn't. */
int console_activate_current(void)
{
    struct vt_stat vts;

    if (-1 == ioctl(STDIN_FILENO, VT_GETSTATE, &vts)) {
	perror("ioctl VT_GETSTATE");
	return -1;
    }
    if (-1 == ioctl(STDIN_FILENO, VT_ACTIVATE, vts.v_active)) {
	perror("ioctl VT_ACTIVATE");
	return -1;
    }
    if (-1 == ioctl(STDIN_FILENO, VT_WAITACTIVE, vts.v_active)) {
	perror("ioctl VT_WAITACTIVE");
	return -1;
    }
    return 0;
}

/* -------------------------------------------------------------------- */

#else /* SYSTEM_LINUX */

int console_visible = 1;

int console_switch_init(void (*suspend)(void),
                        void (*resume)(void))
{
    return -1;
}

void console_switch_cleanup(void)
{
}

int check_console_switch(void)
{
    return 0;
}

#endif /* SYSTEM_LINUX */
