#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fb.h>

#include "fbtools.h"
#include "fs.h"
#include "fb-gui.h"

/* public */
int visible = 1;

/* private */
static struct fs_font *f;
static char *x11_font = "10x20";

static int ys =  3;
static int xs = 10;

/* ---------------------------------------------------------------------- */
/* clear screen (areas)                                                   */

void fb_clear_mem(void)
{
    if (visible)
	fb_memset(fb_mem,0,fb_fix.smem_len);
}

void fb_clear_screen(void)
{
    if (visible)
	fb_memset(fb_mem,0,fb_fix.line_length * fb_var.yres);
}

static void fb_clear_rect(int x1, int x2, int y1,int y2)
{
    unsigned char *ptr;
    int y,h;

    if (!visible)
	return;

    if (x2 < x1)
	h = x2, x2 = x1, x1 = h;
    if (y2 < y1)
	h = y2, y2 = y1, y1 = h;
    ptr  = fb_mem;
    ptr += y1 * fb_fix.line_length;
    ptr += x1 * fs_bpp;

    for (y = y1; y <= y2; y++) {
	fb_memset(ptr, 0, (x2 - x1 + 1) * fs_bpp);
	ptr += fb_fix.line_length;
    }
}

/* ---------------------------------------------------------------------- */
/* draw lines                                                             */

static void fb_setpixel(int x, int y, unsigned int color)
{
    unsigned char *ptr;

    ptr  = fb_mem;
    ptr += y * fb_fix.line_length;
    ptr += x * fs_bpp;
    fs_setpixel(ptr, color);
}

static void fb_line(int x1, int x2, int y1,int y2)
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
	    fb_setpixel(x,y,fs_white);
	}
    } else {
	inc = (float)(y2-y1)/(float)(x2-x1);
	for (x = x1; x <= x2; x++) {
	    y = y1 + inc * (x - x1);
	    fb_setpixel(x,y,fs_white);
	}
    }
}

static void fb_rect(int x1, int x2, int y1,int y2)
{
    fb_line(x1, x2, y1, y1);
    fb_line(x1, x2, y2, y2);
    fb_line(x1, x1, y1, y2);
    fb_line(x2, x2, y1, y2);
}

/* ---------------------------------------------------------------------- */
/* text stuff                                                             */

void fb_text_init1(char *font)
{
    char   *fonts[2] = { font, NULL };

    if (NULL == f)
	f = fs_consolefont(font ? fonts : NULL);
#ifndef X_DISPLAY_MISSING
    if (NULL == f && 0 == fs_connect(NULL))
	f = fs_open(font ? font : x11_font);
#endif
    if (NULL == f) {
	fprintf(stderr,"no font available\n");
	exit(1);
    }
}

void fb_text_init2(void)
{
    fs_init_fb(255);
}

int  fb_font_width(void)
{
    return f->width;
}

void fb_status_line(unsigned char *msg)
{
    int y;
    
    if (!visible)
	return;
    y = fb_var.yres - f->height - ys;
    fb_memset(fb_mem + fb_fix.line_length * y, 0,
	      fb_fix.line_length * (f->height+ys));
    fb_line(0, fb_var.xres, y, y);
    fs_puts(f, 0, y+ys, msg);
}

void fb_edit_line(unsigned char *str, int pos)
{
    int x,y;
    
    if (!visible)
	return;
    y = fb_var.yres - f->height - ys;
    x = pos * f->width;
    fb_memset(fb_mem + fb_fix.line_length * y, 0,
	      fb_fix.line_length * (f->height+ys));
    fb_line(0, fb_var.xres, y, y);
    fs_puts(f, 0, y+ys, str);
    fb_line(x, x + f->width, fb_var.yres-1, fb_var.yres-1);
    fb_line(x, x + f->width, fb_var.yres-2, fb_var.yres-2);
}

void fb_text_box(int x, int y, char *lines[], unsigned int count)
{
    unsigned int i,len,max, x1, x2, y1, y2;

    if (!visible)
	return;

    max = 0;
    for (i = 0; i < count; i++) {
	len = strlen(lines[i]);
	if (max < len)
	    max = len;
    }
    x1 = x;
    x2 = x + max * f->width;
    y1 = y;
    y2 = y + count * f->height;

    x += xs; x2 += 2*xs;
    y += ys; y2 += 2*ys;
    
    fb_clear_rect(x1, x2, y1, y2);
    fb_rect(x1, x2, y1, y2);
    for (i = 0; i < count; i++) {
	fs_puts(f,x,y,lines[i]);
	y += f->height;
    }
}

