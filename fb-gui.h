extern int visible;

void fb_clear_mem(void);
void fb_clear_screen(void);

void fb_text_init1(char *font);
void fb_text_init2(void);
int  fb_font_width(void);
void fb_status_line(unsigned char *msg);
void fb_edit_line(unsigned char *str, int pos);
void fb_text_box(int x, int y, char *lines[], unsigned int count);
