#include <stdbool.h>
#include <linux/input.h>

#define KEY_MOD_SHIFT (1 << 0)
#define KEY_MOD_CTRL  (1 << 1)

void kbd_init(bool use_libinput, bool use_grab, dev_t gfx);
int kbd_wait(int timeout);
int kbd_read(char *buf, uint32_t len,
             uint32_t *keycode, uint32_t *modifier);
void kbd_fini(void);
