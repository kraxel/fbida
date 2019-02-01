#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "readers.h"

/* ----------------------------------------------------------------------- */

void load_bits_lsb(unsigned char *dst, unsigned char *src, int width,
		   int on, int off)
{
    int i,mask,bit;
    
    for (i = 0; i < width; i++) {
	mask = 1 << (i & 0x07);
	bit  = src[i>>3] & mask;
	dst[0] = bit ? on : off;
	dst[1] = bit ? on : off;
	dst[2] = bit ? on : off;
	dst += 3;
    }
}

void load_bits_msb(unsigned char *dst, unsigned char *src, int width,
		   int on, int off)
{
    int i,mask,bit;
    
    for (i = 0; i < width; i++) {
	mask = 1 << (7 - (i & 0x07));
	bit  = src[i>>3] & mask;
	dst[0] = bit ? on : off;
	dst[1] = bit ? on : off;
	dst[2] = bit ? on : off;
	dst += 3;
    }
}

void load_gray(unsigned char *dst, unsigned char *src, int width)
{
    int i;

    for (i = 0; i < width; i++) {
	dst[0] = src[0];
	dst[1] = src[0];
	dst[2] = src[0];
	dst += 3;
	src += 1;
    }
}

void load_graya(unsigned char *dst, unsigned char *src, int width)
{
    int i;

    for (i = 0; i < width; i++) {
	dst[0] = src[0];
	dst[1] = src[0];
	dst[2] = src[0];
	dst += 3;
	src += 2;
    }
}

void load_rgba(unsigned char *dst, unsigned char *src, int width)
{
    int i;

    for (i = 0; i < width; i++) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst += 3;
	src += 4;
    }
}

/* ----------------------------------------------------------------------- */

int load_add_extra(struct ida_image_info *info, enum ida_extype type,
		   unsigned char *data, unsigned int size)
{
    struct ida_extra *extra;

    extra = malloc(sizeof(*extra));
    if (NULL == extra)
	return -1;
    memset(extra,0,sizeof(*extra));
    extra->data = malloc(size);
    if (NULL == extra->data) {
	free(extra);
	return -1;
    }
    extra->type = type;
    extra->size = size;
    memcpy(extra->data,data,size);
    extra->next = info->extra;
    info->extra = extra;
    return 0;
};

struct ida_extra* load_find_extra(struct ida_image_info *info,
				  enum ida_extype type)
{
    struct ida_extra *extra;

    for (extra = info->extra; NULL != extra; extra = extra->next)
	if (type == extra->type)
	    return extra;
    return NULL;
}

int load_free_extras(struct ida_image_info *info)
{
    struct ida_extra *next;

    while (NULL != info->extra) {
	next = info->extra->next;
	free(info->extra->data);
	free(info->extra);
	info->extra = next;
    }
    return 0;
}

/* ----------------------------------------------------------------------- */

void ida_image_alloc(struct ida_image *img)
{
    assert(img->p == NULL);
    img->p = pixman_image_create_bits(/* PIXMAN_r8g8b8 */ PIXMAN_b8g8r8,
                                      img->i.width, img->i.height, NULL, 0);
}

uint8_t *ida_image_scanline(struct ida_image *img, int y)
{
    uint8_t *scanline;

    assert(img->p != NULL);
    scanline  = (void*)pixman_image_get_data(img->p);
    scanline += pixman_image_get_stride(img->p) * y;
    return scanline;
}

uint32_t ida_image_stride(struct ida_image *img)
{
    return pixman_image_get_stride(img->p);
}

uint32_t ida_image_bpp(struct ida_image *img)
{
    uint32_t bits = PIXMAN_FORMAT_BPP(pixman_image_get_format(img->p));
    uint32_t bytes = bits / 8;
    assert(bytes * 8 == bits);
    return bytes;
}

void ida_image_free(struct ida_image *img)
{
    assert(img->p != NULL);
    pixman_image_unref(img->p);
    img->p = NULL;
}

/* ----------------------------------------------------------------------- */

LIST_HEAD(loaders);

void load_register(struct ida_loader *loader)
{
    list_add_tail(&loader->list, &loaders);
}
