#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "vt.h"
#include "fbtools.h"
#include "fb-gui.h"

static int ys =  3;
static int xs = 10;

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- internals                                        */

static int32_t s_lut_transp[256], s_lut_red[256], s_lut_green[256], s_lut_blue[256];

static unsigned char **shadow;
static unsigned int  *sdirty,swidth,sheight;

static void shadow_lut_init_one(int32_t *lut, int bits, int shift)
{
    int i;
    
    if (bits > 8)
	for (i = 0; i < 256; i++)
	    lut[i] = (i << (bits + shift - 8));
    else
	for (i = 0; i < 256; i++)
	    lut[i] = (i >> (8 - bits)) << shift;
}

static void shadow_lut_init(gfxstate *gfx)
{
    shadow_lut_init_one(s_lut_transp, gfx->tlen, gfx->toff);
    shadow_lut_init_one(s_lut_red,    gfx->rlen, gfx->roff);
    shadow_lut_init_one(s_lut_green,  gfx->glen, gfx->goff);
    shadow_lut_init_one(s_lut_blue,   gfx->blen, gfx->boff);
}

static void shadow_render_line(gfxstate *gfx, int line,
                               unsigned char *dest, char unsigned *buffer)
{
    uint8_t  *ptr  = (void*)dest;
    uint16_t *ptr2 = (void*)dest;
    uint32_t *ptr4 = (void*)dest;
    int x;

    switch (gfx->bits_per_pixel) {
    case 15:
    case 16:
	for (x = 0; x < swidth; x++) {
	    ptr2[x] =
                s_lut_red[buffer[x*4+2]] |
		s_lut_green[buffer[x*4+1]] |
		s_lut_blue[buffer[x*4+0]];
	}
	break;
    case 24:
	for (x = 0; x < swidth; x++) {
	    ptr[3*x+2] = buffer[4*x+2];
	    ptr[3*x+1] = buffer[4*x+1];
	    ptr[3*x+0] = buffer[4*x+0];
	}
	break;
    case 32:
	for (x = 0; x < swidth; x++) {
	    ptr4[x] = s_lut_transp[255] |
		s_lut_red[buffer[x*4+2]] |
		s_lut_green[buffer[x*4+1]] |
		s_lut_blue[buffer[x*4+0]];
	}
	break;
    }
}

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- management interface                             */

void shadow_render(gfxstate *gfx)
{
    unsigned int offset = 0;
    int i;

    if (!console_visible)
	return;
    for (i = 0; i < sheight; i++, offset += gfx->stride) {
	if (0 == sdirty[i])
	    continue;
	shadow_render_line(gfx, i, gfx->mem + offset, shadow[i]);
	sdirty[i] = 0;
    }
    if (gfx->flush_display)
        gfx->flush_display(false);
}

void shadow_clear_lines(int first, int last)
{
    int i;

    for (i = first; i <= last; i++) {
	memset(shadow[i],0,4*swidth);
	sdirty[i]++;
    }
}

void shadow_clear(void)
{
    shadow_clear_lines(0, sheight-1);
}

void shadow_set_dirty(void)
{
    int i;

    for (i = 0; i < sheight; i++)
	sdirty[i]++;
}

void shadow_init(gfxstate *gfx)
{
    int i;

    /* init shadow fb */
    swidth  = gfx->hdisplay;
    sheight = gfx->vdisplay;
    shadow  = malloc(sizeof(unsigned char*) * sheight);
    sdirty  = malloc(sizeof(unsigned int)   * sheight);
    memset(sdirty,0, sizeof(unsigned int)   * sheight);
    for (i = 0; i < sheight; i++)
	shadow[i] = malloc(swidth*4);
    shadow_clear();

    /* init rendering */
    switch (gfx->bits_per_pixel) {
    case 15:
    case 16:
    case 24:
    case 32:
        shadow_lut_init(gfx);
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		gfx->bits_per_pixel);
	exit(1);
    }
}

void shadow_fini(void)
{
    int i;

    if (!shadow)
	return;
    for (i = 0; i < sheight; i++)
	free(shadow[i]);
    free(shadow);
    free(sdirty);
}

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- drawing interface                                */

static void shadow_setpixel(int x, int y)
{
    unsigned char *dest = shadow[y] + 4*x;

    if (x < 0)
	return;
    if (x >= swidth)
	return;
    if (y < 0)
	return;
    if (y >= sheight)
	return;
    *(dest++) = 255;
    *(dest++) = 255;
    *(dest++) = 255;
    sdirty[y]++;
}

void shadow_draw_line(int x1, int x2, int y1,int y2)
{
    int x,y,h;
    float inc;

    if (x2 < x1)
	h = x2, x2 = x1, x1 = h;
    if (y2 < y1)
	h = y2, y2 = y1, y1 = h;

    if (x2 - x1 < y2 - y1) {
	inc = (float)(x2-x1)/(float)(y2-y1);
	for (y = y1; y <= y2; y++) {
	    x = x1 + inc * (y - y1);
	    shadow_setpixel(x,y);
	}
    } else {
	inc = (float)(y2-y1)/(float)(x2-x1);
	for (x = x1; x <= x2; x++) {
	    y = y1 + inc * (x - x1);
	    shadow_setpixel(x,y);
	}
    }
}

void shadow_draw_rect(int x1, int x2, int y1,int y2)
{
    shadow_draw_line(x1, x2, y1, y1);
    shadow_draw_line(x1, x2, y2, y2);
    shadow_draw_line(x1, x1, y1, y2);
    shadow_draw_line(x2, x2, y1, y2);
}

void shadow_draw_rgbdata(int x, int y, int pixels, unsigned char *rgb)
{
    unsigned char *dest = shadow[y] + 4*x;
    int i;

    for (i = 0; i < pixels; i++) {
        dest[0] = rgb[2];
        dest[1] = rgb[1];
        dest[2] = rgb[0];
        dest[4] = 0;
        dest += 4;
        rgb += 3;
    }
    sdirty[y]++;
}

void shadow_merge_rgbdata(int x, int y, int pixels, int weight,
			  unsigned char *rgb)
{
    unsigned char *dest = shadow[y] + 4*x;
    int i;

    weight = weight * 256 / 100;

    for (i = 0; i < pixels; i++) {
        dest[0] += rgb[2] * weight >> 8;
        dest[1] += rgb[1] * weight >> 8;
        dest[2] += rgb[0] * weight >> 8;
        dest[4] = 0;
        dest += 4;
        rgb += 3;
    }
    sdirty[y]++;
}


void shadow_darkify(int x1, int x2, int y1,int y2, int percent)
{
    unsigned char *ptr;
    int x,y,h;

    if (x2 < x1)
	h = x2, x2 = x1, x1 = h;
    if (y2 < y1)
	h = y2, y2 = y1, y1 = h;
    
    if (x1 < 0)
	x1 = 0;
    if (x2 >= swidth)
	x2 = swidth;

    if (y1 < 0)
	y1 = 0;
    if (y2 >= sheight)
	y2 = sheight;

    percent = percent * 256 / 100;

    for (y = y1; y <= y2; y++) {
	sdirty[y]++;
	ptr = shadow[y];
	ptr += 4*x1;
	x = 4*(x2-x1+1);
	while (x-- > 0) {
	    *ptr = (*ptr * percent) >> 8;
	    ptr++;
	}
    }
}

void shadow_reverse(int x1, int x2, int y1,int y2)
{
    unsigned char *ptr;
    int x,y,h;

    if (x2 < x1)
	h = x2, x2 = x1, x1 = h;
    if (y2 < y1)
	h = y2, y2 = y1, y1 = h;

    if (x1 < 0)
	x1 = 0;
    if (x2 >= swidth)
	x2 = swidth;

    if (y1 < 0)
	y1 = 0;
    if (y2 >= sheight)
	y2 = sheight;

    for (y = y1; y <= y2; y++) {
	sdirty[y]++;
	ptr = shadow[y];
	for (x = x1; x <= x2; x++) {
	    ptr[4*x+0] = 255-ptr[4*x+0];
	    ptr[4*x+1] = 255-ptr[4*x+1];
	    ptr[4*x+2] = 255-ptr[4*x+2];
	}
    }
}

/* ---------------------------------------------------------------------- */
/* shadow framebuffer -- text rendering                                   */

static void shadow_draw_glyph(FT_Bitmap *bitmap, int sx, int sy)
{
    unsigned char *src,*dst;
    unsigned int bit;
    int x,y;

    src = bitmap->buffer;
    for (y = 0; y < bitmap->rows; y++, src += bitmap->pitch) {
	if (sy+y < 0)
	    continue;
	if (sy+y >= sheight)
	    continue;
	sdirty[sy+y]++;
	dst = shadow[sy+y] + sx*4;
	switch (bitmap->pixel_mode) {
	case FT_PIXEL_MODE_MONO:
	    for (x = 0; x < bitmap->width; x++, dst += 4) {
		if (sx+x < 0)
		    continue;
		if (sx+x >= swidth)
		    continue;
		bit = (1 << (7-(x&7)));
		if (bit & (src[x >> 3])) {
		    dst[0] = 255;
		    dst[1] = 255;
		    dst[2] = 255;
		}
	    }
	    break;
	case FT_PIXEL_MODE_GRAY:
	    for (x = 0; x < bitmap->width; x++, dst += 4) {
		if (sx+x < 0)
		    continue;
		if (sx+x >= swidth)
		    continue;
		if (src[x]) {
		    dst[0] += (255-dst[0]) * src[x] / 255;
		    dst[1] += (255-dst[1]) * src[x] / 255;
		    dst[2] += (255-dst[2]) * src[x] / 255;
		}
	    }
	    break;
	}
    }
}

struct glyph {
    FT_Glyph  glyph;
    int       pos;
};

int shadow_draw_string(FT_Face face, int x, int y, wchar_t *str, int align)
{
    struct glyph   *glyphs;
    FT_UInt        gi,pgi;
    FT_Vector      delta,pen;
    FT_Glyph       image;
    FT_BitmapGlyph bit;
    size_t         len;
    int            i,ng,pos;
    int            kerning;

    len = wcslen(str);
    glyphs = malloc(sizeof(*glyphs) * len);
    memset(glyphs,0,sizeof(*glyphs) * len);

    kerning  = FT_HAS_KERNING(face);
    pgi = 0;
    
    for (ng = 0, pos = 0, i = 0; str[i] != 0; i++) {
	gi = FT_Get_Char_Index(face, str[i]);
	if (kerning && pgi && gi) {
	    FT_Get_Kerning(face,pgi,gi,FT_KERNING_DEFAULT,&delta);
	    pos += delta.x;
	}
	glyphs[ng].pos = pos;
	if (0 != FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT))
	    continue;
	if (0 != FT_Get_Glyph(face->glyph, &glyphs[ng].glyph))
	    continue;
	pos += face->glyph->advance.x;
	pgi = gi;
	ng++;
    }

    switch(align) {
    case -1: /* left */
	break;
    case 0: /* center */
	x -= pos >> 7;
	break;
    case 1: /* right */
	x -= pos >> 6;
	break;
    }
    pen.x = 0;
    pen.y = 0;
    for (i = 0; i < ng; i++) {
	image = glyphs[i].glyph;
	if (0 != FT_Glyph_To_Bitmap(&image,FT_RENDER_MODE_NORMAL,&pen,0))
	    continue;
	bit = (FT_BitmapGlyph)image;
	shadow_draw_glyph(&bit->bitmap,
			  x + bit->left + (glyphs[i].pos >> 6),
			  y - bit->top);
	if (image != glyphs[i].glyph)
	    FT_Done_Glyph(image);
    }

    for (i = 0; i < ng; i++)
	FT_Done_Glyph(glyphs[i].glyph);
    free(glyphs);

    return pos >> 6;
}

void shadow_draw_string_cursor(FT_Face face, int x, int y, wchar_t *str, int pos)
{
    wchar_t save;
    int len, left, width, y1, y2;

    len = wcslen(str);
    if (pos >= len) {
	left  = shadow_draw_string(face, x, y, str, -1);
	width = shadow_draw_string(face, x+left, y, L" ", -1);
    } else {
	save = str[pos];
	str[pos] = 0;
	left = shadow_draw_string(face, x, y, str, -1);
	str[pos] = save;

	save = str[pos+1];
	str[pos+1] = 0;
	width = shadow_draw_string(face, x+left, y, str+pos, -1);
	str[pos+1] = save;

	shadow_draw_string(face, x+left+width, y, str+pos+1, -1);
    }

    y2 = y  - (face->size->metrics.descender >> 6) -1;
    y1 = y2 - (face->size->metrics.height    >> 6) +1;
    shadow_reverse(left,left+width,y1,y2);
}

void shadow_draw_text_box(FT_Face face, int x, int y, int percent, wchar_t *lines[], unsigned int count)
{
    unsigned int i,len,max, x1, x2, y1, y2;

    if (!console_visible)
	return;

    max = 0;
    for (i = 0; i < count; i++) {
	len = wcslen(lines[i]);
	if (max < len)
	    max = len;
    }

    FT_Load_Glyph(face, FT_Get_Char_Index(face, 'x'), FT_LOAD_DEFAULT);
    x1 = x;
    x2 = x + max * (face->glyph->advance.x >> 6);
    y1 = y;
    y2 = y + count * (face->size->metrics.height >> 6);

    x += xs; x2 += 2*xs;
    y += ys; y2 += 2*ys;
    y += (face->size->metrics.height    >> 6);
    y += (face->size->metrics.descender >> 6);
    
    shadow_darkify(x1, x2, y1, y2, percent);
    shadow_draw_rect(x1, x2, y1, y2);
    for (i = 0; i < count; i++) {
	shadow_draw_string(face, x, y, lines[i], -1);
	y += (face->size->metrics.height >> 6);
    }
}

/* ---------------------------------------------------------------------- */
/* fontconfig + freetype font rendering                                   */

static FT_Library freetype;

void font_init(void)
{
    int rc;
    
    FcInit();
    rc = FT_Init_FreeType(&freetype);
    if (rc) {
	fprintf(stderr,"FT_Init_FreeType() failed\n");
	exit(1);
    }
}

FT_Face font_open(char *fcname)
{
    FcResult    result = 0;
    FT_Face     face = NULL;
    FcPattern   *pattern,*match;
    char        *fontname,*h;
    FcChar8     *filename;
    double      pixelsize;
    int         rc;

    /* parse + match font name */
    pattern = FcNameParse(fcname);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    match = FcFontMatch (0, pattern, &result);
    FcPatternDestroy(pattern);
    if (FcResultMatch != result)
	return NULL;
    fontname = FcNameUnparse(match);
    h = strchr(fontname, ':');
    if (h)
	*h = 0;

    /* try get the face directly */
    result = FcPatternGetFTFace(match, FC_FT_FACE, 0, &face);
    if (FcResultMatch == result) {
	fprintf(stderr,"using \"%s\", face=%p\n",fontname,face);
	return face;
    }

    /* failing that use the filename */
    result = FcPatternGetString (match, FC_FILE, 0, &filename);
    if (FcResultMatch == result) {
	result = FcPatternGetDouble(match, FC_PIXEL_SIZE, 0, &pixelsize);
	if (FcResultMatch != result)
	    pixelsize = 16;
	fprintf(stderr,"using \"%s\", pixelsize=%.2lf file=%s\n",
		fontname,pixelsize,filename);
	rc = FT_New_Face (freetype, filename, 0, &face);
	if (rc)
	    return NULL;
	FT_Set_Pixel_Sizes(face, 0, (int)pixelsize);
	return face;
    }

    /* oops, didn't work */
    return NULL;
}
