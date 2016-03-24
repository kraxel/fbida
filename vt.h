extern int console_visible;

int console_switch_init(int fd, void (*redraw)(void));
int check_console_switch(void);

void console_set_vt(int vtno);
void console_restore_vt(void);
int console_activate_current(int tty);
