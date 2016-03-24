#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>

#include <linux/kd.h>
#include <linux/vt.h>

#include "vt.h"

/* -------------------------------------------------------------------- */

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
static int orig_vt_no = 0;
static void (*console_redraw)(void);

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

int console_switch_init(void (*redraw)(void))
{
    struct sigaction act,old;

    console_redraw = redraw;

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
	console_switch_release();
    case CONSOLE_INACTIVE:
	console_visible = 0;
	break;
    case CONSOLE_ACQ_REQ:
	console_switch_acquire();
    case CONSOLE_ACTIVE:
	console_visible = 1;
        console_redraw();
	break;
    default:
	break;
    }
    switch_last = console_switch_state;
    return 1;
}

void console_set_vt(int vtno)
{
    struct vt_stat vts;
    char vtname[12];

    if (vtno < 0) {
	if (-1 == ioctl(STDIN_FILENO, VT_OPENQRY, &vtno) || vtno == -1) {
	    perror("ioctl VT_OPENQRY");
	    exit(1);
	}
    }

    vtno &= 0xff;
    sprintf(vtname, "/dev/tty%d", vtno);
    chown(vtname, getuid(), getgid());
    if (-1 == access(vtname, R_OK | W_OK)) {
	fprintf(stderr,"access %s: %s\n",vtname,strerror(errno));
	exit(1);
    }

    /* switch controlling tty */
    switch (fork()) {
    case 0:
	break;
    case -1:
	perror("fork");
	exit(1);
    default:
	exit(0);
    }
    close(0);
    close(1);
    close(2);
    setsid();
    open(vtname,O_RDWR);
    dup(0);
    dup(0);

    if (-1 == ioctl(STDIN_FILENO,VT_GETSTATE, &vts)) {
	perror("ioctl VT_GETSTATE");
	exit(1);
    }
    orig_vt_no = vts.v_active;
    if (-1 == ioctl(STDIN_FILENO,VT_ACTIVATE, vtno)) {
	perror("ioctl VT_ACTIVATE");
	exit(1);
    }
    if (-1 == ioctl(STDIN_FILENO,VT_WAITACTIVE, vtno)) {
	perror("ioctl VT_WAITACTIVE");
	exit(1);
    }
}

void console_restore_vt(void)
{
    if (!orig_vt_no)
        return;

    if (ioctl(STDIN_FILENO, VT_ACTIVATE, orig_vt_no) < 0)
	perror("ioctl VT_ACTIVATE");
    if (ioctl(STDIN_FILENO, VT_WAITACTIVE, orig_vt_no) < 0)
	perror("ioctl VT_WAITACTIVE");
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
