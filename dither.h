
extern void (*dither_line)(unsigned char *, unsigned char *, int, int);

void init_dither(int, int, int, int);
void dither_line_color(unsigned char *, unsigned char *, int, int);
void dither_line_gray(unsigned char *, unsigned char *, int, int);
