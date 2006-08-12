#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

extern int visible;

void shadow_render(void);
void shadow_clear_lines(int first, int last);
void shadow_clear(void);
void shadow_set_dirty(void);
void shadow_set_palette(int fd);
void shadow_init(void);
void shadow_fini(void);

void shadow_draw_line(int x1, int x2, int y1,int y2);
void shadow_draw_rect(int x1, int x2, int y1,int y2);
void shadow_draw_rgbdata(int x, int y, int pixels,
			 unsigned char *rgb);
void shadow_merge_rgbdata(int x, int y, int pixels, int weight,
			  unsigned char *rgb);
void shadow_darkify(int x1, int x2, int y1,int y2, int percent);
void shadow_reverse(int x1, int x2, int y1,int y2);

int  shadow_draw_string(FT_Face face, int x, int y, wchar_t *str, int align);
void shadow_draw_string_cursor(FT_Face face, int x, int y, wchar_t *str, int pos);
void shadow_draw_text_box(FT_Face face, int x, int y, int percent,
			  wchar_t *lines[], unsigned int count);

void font_init(void);
FT_Face font_open(char *fcname);

void fb_clear_mem(void);
void fb_clear_screen(void);
