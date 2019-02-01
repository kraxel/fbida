#include <stdbool.h>
#include <inttypes.h>
#include <linux/input.h>

#include <libudev.h>
#include <libinput.h>

#define KEY_MOD_SHIFT (1 << 0)
#define KEY_MOD_CTRL  (1 << 1)

extern int libinput_devcount;
extern int libinput_deverror;
extern const struct libinput_interface libinput_interface;

void kbd_init(int use_libinput, dev_t gfx);
int kbd_wait(int timeout);
int kbd_read(char *buf, uint32_t len,
             uint32_t *keycode, uint32_t *modifier);
void kbd_suspend(void);
void kbd_resume(void);
void kbd_fini(void);
