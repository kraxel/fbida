#define FB_ACTIVE    0
#define FB_REL_REQ   1
#define FB_INACTIVE  2
#define FB_ACQ_REQ   3

/* info about videomode - yes I know, quick & dirty... */
extern struct fb_fix_screeninfo   fb_fix;
extern struct fb_var_screeninfo   fb_var;
extern unsigned char             *fb_mem;
extern int			  fb_mem_offset;
extern int                        fb_switch_state;

/* init + cleanup */
int fb_probe(void);
int  fb_init(char *device, char *mode, int vt);
void fb_cleanup(void);
void fb_catch_exit_signals(void);
void fb_memset(void *addr, int c, size_t len);

/* console switching */
int  fb_switch_init(void);
void fb_switch_release(void);
void fb_switch_acquire(void);
