/*
 * ordered dither rotines
 *
 * stolen from The GIMP and trimmed for speed
 *
 */

#include <stdlib.h>
#include "dither.h"

#define DITHER_LEVEL 8

void (*dither_line)(unsigned char *, unsigned char *, int, int);

static long     red_mult, green_mult;
static long     red_dither[256];
static long     green_dither[256];
static long     blue_dither[256];
static long     gray_dither[256];

typedef unsigned long vector[DITHER_LEVEL];
typedef vector  matrix[DITHER_LEVEL];

#if DITHER_LEVEL == 8
#define DITHER_MASK 7
static matrix   DM =
{
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};

#endif

#if DITHER_LEVEL == 4
#define DITHER_MASK 3
static matrix   DM =
{
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5}
};

#endif

void
init_dither(int shades_r, int shades_g, int shades_b, int shades_gray)
{
    int             i, j;
    unsigned char   low_shade, high_shade;
    unsigned short  index;
    float           red_colors_per_shade;
    float           green_colors_per_shade;
    float           blue_colors_per_shade;
    float           gray_colors_per_shade;

    red_mult = shades_g * shades_b;
    green_mult = shades_b;

    red_colors_per_shade = 256.0 / (shades_r - 1);
    green_colors_per_shade = 256.0 / (shades_g - 1);
    blue_colors_per_shade = 256.0 / (shades_b - 1);
    gray_colors_per_shade = 256.0 / (shades_gray - 1);

    /* this avoids a shift when checking these values */
    for (i = 0; i < DITHER_LEVEL; i++)
	for (j = 0; j < DITHER_LEVEL; j++)
	    DM[i][j] *= 0x10000;

    /*  setup arrays containing three bytes of information for red, green, & blue  */
    /*  the arrays contain :
     *    1st byte:    low end shade value
     *    2nd byte:    high end shade value
     *    3rd & 4th bytes:    ordered dither matrix index
     */

    for (i = 0; i < 256; i++) {

	/*  setup the red information  */
	{
	    low_shade = (unsigned char) (i / red_colors_per_shade);
	    high_shade = low_shade + 1;

	    index = (unsigned short)
		(((i - low_shade * red_colors_per_shade) / red_colors_per_shade) *
		 (DITHER_LEVEL * DITHER_LEVEL + 1));

	    low_shade *= red_mult;
	    high_shade *= red_mult;

	    red_dither[i] = (index << 16) + (high_shade << 8) + (low_shade);
	}

	/*  setup the green information  */
	{
	    low_shade = (unsigned char) (i / green_colors_per_shade);
	    high_shade = low_shade + 1;

	    index = (unsigned short)
		(((i - low_shade * green_colors_per_shade) / green_colors_per_shade) *
		 (DITHER_LEVEL * DITHER_LEVEL + 1));

	    low_shade *= green_mult;
	    high_shade *= green_mult;

	    green_dither[i] = (index << 16) + (high_shade << 8) + (low_shade);
	}

	/*  setup the blue information  */
	{
	    low_shade = (unsigned char) (i / blue_colors_per_shade);
	    high_shade = low_shade + 1;

	    index = (unsigned short)
		(((i - low_shade * blue_colors_per_shade) / blue_colors_per_shade) *
		 (DITHER_LEVEL * DITHER_LEVEL + 1));

	    blue_dither[i] = (index << 16) + (high_shade << 8) + (low_shade);
	}

	/*  setup the gray information  */
	{
	    low_shade = (unsigned char) (i / gray_colors_per_shade);
	    high_shade = low_shade + 1;

	    index = (unsigned short)
		(((i - low_shade * gray_colors_per_shade) / gray_colors_per_shade) *
		 (DITHER_LEVEL * DITHER_LEVEL + 1));

	    gray_dither[i] = (index << 16) + (high_shade << 8) + (low_shade);
	}
    }
}

void
dither_line_color(unsigned char *src, unsigned char *dest, int y, int width)
{
    register long   a, b;
    long           *ymod, xmod;

    ymod = DM[y & DITHER_MASK];

    while (width--) {
	xmod = width & DITHER_MASK;

	b = red_dither[*(src++)];
	if (ymod[xmod] < b)
	    b >>= 8;

	a = green_dither[*(src++)];
	if (ymod[xmod] < a)
	    a >>= 8;
	b += a;

	a = blue_dither[*(src++)];
	if (ymod[xmod] < a)
	    a >>= 8;
	b += a;

	*(dest++) = b & 0xff;
    }
}

void
dither_line_gray(unsigned char *src, unsigned char *dest, int y, int width)
{
    long           *ymod, xmod;
    register long   a,g;

    ymod = DM[y & DITHER_MASK];

    while (width--) {
	xmod = width & DITHER_MASK;

#if 1
	g = (src[0]*3 + src[1]*6 + src[2]) / 10;
	a = gray_dither[g];
	src += 3;
#else
	a = gray_dither[*(src++)];
#endif
	if (ymod[xmod] < a)
	    a >>= 8;

	*(dest++) = a & 0xff;
    }
}
