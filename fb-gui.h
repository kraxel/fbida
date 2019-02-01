#include <cairo.h>

extern int visible;

void shadow_render(gfxstate *gfx);
void shadow_clear_lines(int first, int last);
void shadow_clear(void);
void shadow_set_dirty(void);
void shadow_set_palette(int fd);
void shadow_init(gfxstate *gfx);
void shadow_fini(void);

void shadow_draw_line(int x1, int x2, int y1,int y2);
void shadow_draw_rect(int x1, int x2, int y1,int y2);
void shadow_composite_image(struct ida_image *img,
                            int xoff, int yoff, int weight);
void shadow_darkify(int x1, int x2, int y1,int y2, int percent);

int  shadow_draw_string(int x, int y, char *str, int align);
void shadow_draw_text_box(int x, int y, int percent,
			  char *lines[], unsigned int count);

cairo_font_extents_t *shadow_font_init(char *find);
