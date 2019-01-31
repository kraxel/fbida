extern int console_visible;

int console_switch_init(void (*redraw)(void));
void console_switch_cleanup(void);
int check_console_switch(void);

int console_aquire_vt(void);
void console_restore_vt(void);
int console_activate_current(void);
