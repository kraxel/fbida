#ifndef X_DISPLAY_MISSING
# include <FSlib.h>

struct fs_font {
    Font               font;
    FSXFontInfoHeader  fontHeader;
    FSPropInfo         propInfo;
    FSPropOffset       *propOffsets;
    unsigned char      *propData;

    FSXCharInfo        *extents;
    FSOffset           *offsets;
    unsigned char      *glyphs;

    int                maxenc,width,height;
    FSXCharInfo        **eindex;
    unsigned char      **gindex;
};

#else

typedef struct _FSXCharInfo {
    short       left;
    short       right;
    short       width;
    short       ascent;
    short       descent;
    //unsigned short      attributes;
} FSXCharInfo;

typedef struct _FSXFontInfoHeader {
    //int         flags;
    //FSRange     char_range;
    //unsigned    draw_direction;
    //FSChar2b    default_char;
    FSXCharInfo min_bounds;
    FSXCharInfo max_bounds;
    short       font_ascent;
    short       font_descent;
} FSXFontInfoHeader;

struct fs_font {
    FSXFontInfoHeader  fontHeader;
    //unsigned char      *propData;
    FSXCharInfo        *extents;
    unsigned char      *glyphs;
    int                maxenc,width,height;
    FSXCharInfo        **eindex;
    unsigned char      **gindex;
};

#endif

/* ------------------------------------------------------------------ */

extern unsigned int fs_bpp, fs_black, fs_white;
void (*fs_setpixel)(void *ptr, unsigned int color);

int fs_init_fb(int white8);
void fs_render_fb(unsigned char *ptr, int pitch,
		  FSXCharInfo *charInfo, unsigned char *data);
int fs_puts(struct fs_font *f, unsigned int x, unsigned int y,
	    unsigned char *str);
int fs_textwidth(struct fs_font *f, unsigned char *str);
void fs_render_tty(FSXCharInfo *charInfo, unsigned char *data);

#ifndef X_DISPLAY_MISSING
int fs_connect(char *servername);
struct fs_font* fs_open(char *pattern);
#endif
struct fs_font* fs_consolefont(char **filename);
void fs_free(struct fs_font *f);
