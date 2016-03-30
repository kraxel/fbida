#include <linux/input.h>

#define KEY_MOD_SHIFT (1 << 0)
#define KEY_MOD_CTRL  (1 << 1)

uint32_t kbd_parse(const char *key, uint32_t *mod);
int kbd_wait(int timeout);

void tty_raw(void);
void tty_restore(void);
