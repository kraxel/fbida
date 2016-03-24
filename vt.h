extern int console_visible;

int console_switch_init(void (*redraw)(void));
void console_switch_cleanup(void);
int check_console_switch(void);

void console_set_vt(int vtno);
void console_restore_vt(void);
int console_activate_current(void);
