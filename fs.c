/*
 * text rendering for the framebuffer console
 * pick fonts from X11 font server or
 * use linux consolefont psf files.
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/fb.h>

#include "fbtools.h"
#include "fs.h"

/* ------------------------------------------------------------------ */

#define BIT_ORDER       BitmapFormatBitOrderMSB
#ifdef BYTE_ORDER
#undef BYTE_ORDER
#endif
#define BYTE_ORDER      BitmapFormatByteOrderMSB
#define SCANLINE_UNIT   BitmapFormatScanlineUnit8
#define SCANLINE_PAD    BitmapFormatScanlinePad8
#define EXTENTS         BitmapFormatImageRectMin

#define SCANLINE_PAD_BYTES 1
#define GLWIDTHBYTESPADDED(bits, nBytes)                                    \
        ((nBytes) == 1 ? (((bits)  +  7) >> 3)          /* pad to 1 byte  */\
        :(nBytes) == 2 ? ((((bits) + 15) >> 3) & ~1)    /* pad to 2 bytes */\
        :(nBytes) == 4 ? ((((bits) + 31) >> 3) & ~3)    /* pad to 4 bytes */\
        :(nBytes) == 8 ? ((((bits) + 63) >> 3) & ~7)    /* pad to 8 bytes */\
        : 0)

static const unsigned fs_masktab[] = {
    (1 << 7), (1 << 6), (1 << 5), (1 << 4),
    (1 << 3), (1 << 2), (1 << 1), (1 << 0),
};

/* ------------------------------------------------------------------ */

#ifndef X_DISPLAY_MISSING
static FSServer           *svr;
#endif
unsigned int       fs_bpp, fs_black, fs_white;

void (*fs_setpixel)(void *ptr, unsigned int color);

static void setpixel1(void *ptr, unsigned int color)
{
    unsigned char *p = ptr;
    *p = color;
}
static void setpixel2(void *ptr, unsigned int color)
{
    unsigned short *p = ptr;
    *p = color;
}
static void setpixel3(void *ptr, unsigned int color)
{
    unsigned char *p = ptr;
    *(p++) = (color >> 16) & 0xff;
    *(p++) = (color >>  8) & 0xff;
    *(p++) =  color        & 0xff;
}
static void setpixel4(void *ptr, unsigned int color)
{
    unsigned long *p = ptr;
    *p = color;
}

int fs_init_fb(int white8)
{
    switch (fb_var.bits_per_pixel) {
    case 8:
	fs_white = white8; fs_black = 0; fs_bpp = 1;
	fs_setpixel = setpixel1;
	break;
    case 15:
    case 16:
	if (fb_var.green.length == 6)
	    fs_white = 0xffff;
	else
	    fs_white = 0x7fff;
	fs_black = 0; fs_bpp = 2;
	fs_setpixel = setpixel2;
	break;
    case 24:
	fs_white = 0xffffff; fs_black = 0; fs_bpp = fb_var.bits_per_pixel/8;
	fs_setpixel = setpixel3;
	break;
    case 32:
	fs_white = 0xffffff; fs_black = 0; fs_bpp = fb_var.bits_per_pixel/8;
	fs_setpixel = setpixel4;
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		fb_var.bits_per_pixel);
	return -1;
    }
    return 0;
}

void fs_render_fb(unsigned char *ptr, int pitch,
		  FSXCharInfo *charInfo, unsigned char *data)
{
    int row,bit,bpr,x;

    bpr = GLWIDTHBYTESPADDED((charInfo->right - charInfo->left),
			     SCANLINE_PAD_BYTES);
    for (row = 0; row < (charInfo->ascent + charInfo->descent); row++) {
	for (x = 0, bit = 0; bit < (charInfo->right - charInfo->left); bit++) {
	    if (data[bit>>3] & fs_masktab[bit&7])
		fs_setpixel(ptr+x,fs_white);
	    x += fs_bpp;
	}
	data += bpr;
	ptr += pitch;
    }
}

int fs_puts(struct fs_font *f, unsigned int x, unsigned int y,
	    unsigned char *str)
{
    unsigned char *pos,*start;
    int i,c,j,w;

    pos  = fb_mem+fb_mem_offset;
    pos += fb_fix.line_length * y;
    for (i = 0; str[i] != '\0'; i++) {
	c = str[i];
	if (NULL == f->eindex[c])
	    continue;
	/* clear with bg color */
	start = pos + x*fs_bpp + f->fontHeader.max_bounds.descent * fb_fix.line_length;
	w = (f->eindex[c]->width+1)*fs_bpp;
	for (j = 0; j < f->height; j++) {
	    memset(start,0,w);
	    start += fb_fix.line_length;
	}
	/* draw char */
	start = pos + x*fs_bpp + fb_fix.line_length * (f->height-f->eindex[c]->ascent);
	fs_render_fb(start,fb_fix.line_length,f->eindex[c],f->gindex[c]);
	x += f->eindex[c]->width;
	if (x > fb_var.xres - f->width)
	    return -1;
    }
    return x;
}

int fs_textwidth(struct fs_font *f, unsigned char *str)
{
    int width = 0;
    int i,c;
    
    for (i = 0; str[i] != '\0'; i++) {
	c = str[i];
	if (NULL == f->eindex[c])
	    continue;
	width += f->eindex[c]->width;
    }
    return width;
}

void fs_render_tty(FSXCharInfo *charInfo, unsigned char *data)
{
    int bpr,row,bit,on;

    bpr = GLWIDTHBYTESPADDED((charInfo->right - charInfo->left),
			     SCANLINE_PAD_BYTES);
    for (row = 0; row < (charInfo->ascent + charInfo->descent); row++) {
	fprintf(stdout,"|");
	for (bit = 0; bit < (charInfo->right - charInfo->left); bit++) {
	    on = data[bit>>3] & fs_masktab[bit&7];
	    fprintf(stdout,"%s",on ? "##" : "  ");
	}
	fprintf(stdout,"|\n");
	data += bpr;
    }
    fprintf(stdout,"--\n");
}

/* ------------------------------------------------------------------ */

#ifndef X_DISPLAY_MISSING
/* connect to font server */
int fs_connect(char *servername)
{
    if (NULL == servername)
	servername = getenv("FONTSERVER");
    if (NULL == servername)
	servername = "unix/:7100";
    svr = FSOpenServer(servername);
    if (NULL == svr) {
	if (NULL == FSServerName(servername)) {
	    fprintf(stderr, "no font server defined\n");
	} else {
	    fprintf(stderr, "unable to open server \"%s\"\n",
		    FSServerName(servername));
	}
	return -1;
    }
    return 0;
}

/* load font from font server */
struct fs_font* fs_open(char *pattern)
{
    int              nnames = 1;
    int              available,high,low,encoding,bpr;
    char             **fonts;
    unsigned char    *glyph;
    Font             dummy;
    FSBitmapFormat   format;
    FSXCharInfo      *charInfo;
    struct fs_font   *f = NULL;
    
    if (NULL == svr) {
	fprintf(stderr,"fs: not connected\n");
	return NULL;
    }

    fonts = FSListFonts(svr, pattern, nnames, &available);
    if (0 == available) {
	fprintf(stderr,"fs: font not available [%s]\n",pattern);
	goto out;
    }
    fprintf(stderr,"using x11 font \"%s\"\n",fonts[0]);

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));
    f->font = FSOpenBitmapFont(svr, 0, 0, fonts[0], &dummy);
    FSFreeFontNames(fonts);
    if (0 == f->font)
	goto out;
    
    FSQueryXInfo(svr,f->font,&f->fontHeader, &f->propInfo,
		 &f->propOffsets, &f->propData);
    format = BYTE_ORDER | BIT_ORDER | SCANLINE_UNIT | SCANLINE_PAD | EXTENTS;
    FSQueryXExtents16(svr, f->font, True, (FSChar2b *) 0, 0, &f->extents);
    FSQueryXBitmaps16(svr, f->font, format, True, (FSChar2b *) 0, 0,
		      &f->offsets, &f->glyphs);

    f->maxenc = (f->fontHeader.char_range.max_char.high+1) << 8;
    f->width  = f->fontHeader.max_bounds.right - f->fontHeader.min_bounds.left;
    f->height = f->fontHeader.max_bounds.ascent + f->fontHeader.max_bounds.descent;
    f->eindex = malloc(f->maxenc * sizeof(FSXCharInfo*));
    f->gindex = malloc(f->maxenc * sizeof(unsigned char*));
    memset(f->eindex,0,f->maxenc * sizeof(FSXCharInfo*));
    memset(f->gindex,0,f->maxenc * sizeof(unsigned char*));

    glyph    = f->glyphs;
    charInfo = f->extents;
    for (high  = f->fontHeader.char_range.min_char.high;
	 high <= f->fontHeader.char_range.max_char.high;
	 high++) {
	for (low  = f->fontHeader.char_range.min_char.low;
	     low <= f->fontHeader.char_range.max_char.low;
	     low++) {
	    bpr = GLWIDTHBYTESPADDED((charInfo->right - charInfo->left),
				     SCANLINE_PAD_BYTES);
	    encoding = (high<<8) + low;
#ifdef TTY
	    fprintf(stdout,"e=0x%x | w=%d  l=%d r=%d  |  a=%d d=%d\n",
		    encoding,charInfo->width,charInfo->left,
		    charInfo->right,charInfo->ascent,charInfo->descent);
#endif
	    if ((charInfo->width != 0) || (charInfo->right != charInfo->left)) {
		f->gindex[encoding] = glyph;
		f->eindex[encoding] = charInfo;
#ifdef TTY
		fs_render_tty(f->eindex[encoding],
			      f->gindex[encoding]);
#endif
	    }
	    glyph += (charInfo->descent + charInfo->ascent) * bpr;
	    charInfo++;
	}
    }
    return f;

 out:
    if (f) 
	fs_free(f);
    return NULL;
}
#endif

void fs_free(struct fs_font *f)
{
    if (f->gindex)
	free(f->gindex);
#if 0
    if (f->extents)
	FSFree((char *) f->extents);
    if (f->offsets)
	FSFree((char *) f->offsets);
    if (f->propOffsets)
	FSFree((char *) (f->propOffsets));
    if (f->propData)
	FSFree((char *) (f->propData));
#endif
#if 0 /* FIXME */
    if (f->glyphs)
	FSFree((char *) f->glyphs);
#endif
    free(f);
}

/* ------------------------------------------------------------------ */
/* load console font file                                             */

static char *default_font[] = {
    /* why the heck every f*cking distribution picks another
       location for these fonts ??? */
    "/usr/share/consolefonts/lat1-16.psf",
    "/usr/share/consolefonts/lat1-16.psf.gz",
    "/usr/share/consolefonts/lat1-16.psfu.gz",
    "/usr/share/kbd/consolefonts/lat1-16.psf",
    "/usr/share/kbd/consolefonts/lat1-16.psf.gz",
    "/usr/share/kbd/consolefonts/lat1-16.psfu.gz",
    "/usr/lib/kbd/consolefonts/lat1-16.psf",
    "/usr/lib/kbd/consolefonts/lat1-16.psf.gz",
    "/usr/lib/kbd/consolefonts/lat1-16.psfu.gz",
    "/lib/kbd/consolefonts/lat1-16.psf",
    "/lib/kbd/consolefonts/lat1-16.psf.gz",
    "/lib/kbd/consolefonts/lat1-16.psfu.gz",
    NULL
};

struct fs_font* fs_consolefont(char **filename)
{
    int  i;
    char *h,command[256];
    struct fs_font *f = NULL;
    FILE *fp;

    if (NULL == filename)
	filename = default_font;

    for(i = 0; filename[i] != NULL; i++) {
	if (-1 == access(filename[i],R_OK))
	    continue;
	break;
    }
    if (NULL == filename[i]) {
	fprintf(stderr,"can't find console font file\n");
	return NULL;
    }

    h = filename[i]+strlen(filename[i])-3;
    if (0 == strcmp(h,".gz")) {
	sprintf(command,"zcat %s",filename[i]);
	fp = popen(command,"r");
    } else {
	fp = fopen(filename[i], "r");
    }
    if (NULL == fp) {
	fprintf(stderr,"can't open %s: %s\n",filename[i],strerror(errno));
	return NULL;
    }

    if (fgetc(fp) != 0x36 ||
	fgetc(fp) != 0x04) {
	fprintf(stderr,"can't use font %s\n",filename[i]);
	return NULL;
    }
    fprintf(stderr,"using linux console font \"%s\"\n",filename[i]);

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));

    fgetc(fp);
    f->maxenc = 256;
    f->width  = 8;
    f->height = fgetc(fp);
    f->fontHeader.min_bounds.left    = 0;
    f->fontHeader.max_bounds.right   = f->width;
    f->fontHeader.max_bounds.descent = 0;
    f->fontHeader.max_bounds.ascent  = f->height;

    f->glyphs  = malloc(f->height * 256);
    f->extents = malloc(sizeof(FSXCharInfo)*256);
    fread(f->glyphs, 256, f->height, fp);
    fclose(fp);

    f->eindex  = malloc(sizeof(FSXCharInfo*)   * 256);
    f->gindex  = malloc(sizeof(unsigned char*) * 256);
    for (i = 0; i < 256; i++) {
	f->eindex[i] = f->extents +i;
	f->gindex[i] = f->glyphs  +i * f->height;
	f->eindex[i]->left    = 0;
	f->eindex[i]->right   = 7;
	f->eindex[i]->width   = 8;
	f->eindex[i]->descent = 0;
	f->eindex[i]->ascent  = f->height;
    }
    return f;
}


#ifdef TESTING
/* ------------------------------------------------------------------ */
/* for testing                                                        */

int debug;

/* list fonts */
int fs_ls(char *pattern)
{
    int    nnames = 16;
    int    available,i;
    char   **fonts;

    if (NULL == svr) {
	fprintf(stderr,"fs: not connected\n");
	return -1;
    }

    fonts = FSListFonts(svr, pattern, nnames, &available);
    while (nnames <= available) {
	nnames *= 2;
	FSFreeFontNames(fonts);
	fonts = FSListFonts(svr, pattern, nnames, &available);
    }
    for (i = 0; i < available; i++) {
	fprintf(stderr,"%s\n",fonts[i]);
    }
    FSFreeFontNames(fonts);
    return 0;
}

void dump_charset(struct fs_font *f)
{
    unsigned char *pos;
    int c,x,y;

    x = 0, y = 0;
    for (c = 0; c < f->maxenc; c++) {
	if (NULL == f->eindex[c])
	    continue;
	pos  = fb_mem+fb_mem_offset;
	pos += fb_fix.line_length * (y+f->height-f->eindex[c]->ascent);
	pos += x*bpp;
	fs_render_fb(pos,fb_fix.line_length,f->eindex[c],f->gindex[c]);
	x += f->eindex[c]->right-f->eindex[c]->left+1;
	if (x > fb_var.xres - f->width) {
	    x = 0;
	    y += f->height+1;
	}
	if (y > fb_var.yres - f->height)
	    break;
    }
}

int main(int argc, char *argv[])
{
    struct fs_font *f = NULL;
    unsigned char dummy[42];
    int fd;

    if (argc < 2) {
	fprintf(stderr,"missing arg\n");
	exit(1);
    }

    /* try font server */
    if (-1 != fs_connect(NULL)) {
	fs_ls(argv[1]);
	f = fs_open(argv[1]);
	if (NULL == f)
	    fprintf(stderr,"no such font\n");
    }

    /* try console font */
    if (NULL == f)
	f = fs_consolefont(NULL);
    if (NULL == f)
	exit(1);
	
#ifdef TTY
    exit(1);
#endif

    fd = fb_init(NULL, NULL, 0);
    fb_cleanup_fork();
    fb_switch_init();
    fs_init_fb();

    if (argc < 3) {
	dump_charset(f);
    } else {
	fs_puts(f,0,0,argv[2]);
    }
    fgets(dummy,42,stdin);
    
    return 0;
}
#endif
