#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <png.h>

#include "readers.h"

static const char *ct[] = {
    "gray",  "X1", "rgb",  "palette",
    "graya", "X5", "rgba", "X7",
};

struct png_state {
    FILE         *infile;
    png_structp  png;
    png_infop    info;
    png_bytep    image;
    png_uint_32  w,h;
    int          color_type;
};

static void*
png_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *i, int thumbnail)
{
    struct png_state *h;
    int bit_depth, interlace_type;
    int pass, number_passes;
    unsigned int y;
    png_uint_32 resx, resy;
    png_color_16 *file_bg, my_bg = {
	.red   = 192,
	.green = 192,
	.blue  = 192,
	.gray  = 192,
    };
    int unit;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->infile = fp;

    h->png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
				    NULL, NULL, NULL);
    if (NULL == h->png)
	goto oops;
    h->info = png_create_info_struct(h->png);
    if (NULL == h->info)
	goto oops;

    png_init_io(h->png, h->infile);
    png_read_info(h->png, h->info);
    png_get_IHDR(h->png, h->info, &h->w, &h->h,
		 &bit_depth,&h->color_type,&interlace_type, NULL,NULL);
    png_get_pHYs(h->png, h->info, &resx, &resy, &unit);
    i->width  = h->w;
    i->height = h->h;
    if (PNG_RESOLUTION_METER == unit)
	i->dpi = res_m_to_inch(resx);
    if (debug)
	fprintf(stderr,"png: color_type=%s #1\n",ct[h->color_type]);
    i->npages = 1;

    png_set_packing(h->png);
    if (bit_depth == 16)
	png_set_strip_16(h->png);
    if (h->color_type == PNG_COLOR_TYPE_PALETTE)
	png_set_palette_to_rgb(h->png);
    if (h->color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
	png_set_expand_gray_1_2_4_to_8(h->png);

    if (png_get_bKGD(h->png, h->info, &file_bg)) {
	png_set_background(h->png,file_bg,PNG_BACKGROUND_GAMMA_FILE,1,1.0);
    } else {
	png_set_background(h->png,&my_bg,PNG_BACKGROUND_GAMMA_SCREEN,0,1.0);
    }

    number_passes = png_set_interlace_handling(h->png);
    png_read_update_info(h->png, h->info);

    h->color_type = png_get_color_type(h->png, h->info);
    if (debug)
	fprintf(stderr,"png: color_type=%s #2\n",ct[h->color_type]);

    h->image = malloc(i->width * i->height * 4);

    for (pass = 0; pass < number_passes-1; pass++) {
	if (debug)
	    fprintf(stderr,"png: pass #%d\n",pass);
	for (y = 0; y < i->height; y++) {
	    png_bytep row = h->image + y * i->width * 4;
	    png_read_rows(h->png, &row, NULL, 1);
	}
    }

    return h;

 oops:
    if (h->image)
	free(h->image);
    if (h->png)
	png_destroy_read_struct(&h->png, NULL, NULL);
    fclose(h->infile);
    free(h);
    return NULL;
}

static void
png_read(unsigned char *dst, unsigned int line, void *data)
{
    struct png_state *h = data;

    png_bytep row = h->image + line * h->w * 4;
    switch (h->color_type) {
    case PNG_COLOR_TYPE_GRAY:
	png_read_rows(h->png, &row, NULL, 1);
	load_gray(dst,row,h->w);
	break;
    case PNG_COLOR_TYPE_RGB:
	png_read_rows(h->png, &row, NULL, 1);
	memcpy(dst,row,3*h->w);
	break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
	png_read_rows(h->png, &row, NULL, 1);
	load_rgba(dst,row,h->w);
	break;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
	png_read_rows(h->png, &row, NULL, 1);
	load_graya(dst,row,h->w);
	break;
    default:
	/* shouldn't happen */
	fprintf(stderr,"Oops: %s:%d\n",__FILE__,__LINE__);
	exit(1);
    }
}

static void
png_done(void *data)
{
    struct png_state *h = data;

    free(h->image);
    png_destroy_read_struct(&h->png, &h->info, NULL);
    fclose(h->infile);
    free(h);
}

static struct ida_loader png_loader = {
    magic: "\x89PNG",
    moff:  0,
    mlen:  4,
    name:  "libpng",
    init:  png_init,
    read:  png_read,
    done:  png_done,
};

static void __init init_rd(void)
{
    load_register(&png_loader);
}
