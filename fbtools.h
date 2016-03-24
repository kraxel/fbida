#include "gfx.h"

#define FB_ACTIVE    0
#define FB_REL_REQ   1
#define FB_INACTIVE  2
#define FB_ACQ_REQ   3

/* info about videomode - yes I know, quick & dirty... */
extern int                        fb_switch_state;

/* init + cleanup */
gfxstate *fb_init(char *device, char *mode, int vt);

/* console switching */
int  fb_switch_init(void);
void fb_switch_release(void);
void fb_switch_acquire(void);
