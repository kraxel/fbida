#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <webp/decode.h>

#include "readers.h"


struct webp_state {
	FILE *f;
	int width, height;
	uint8_t *data;
};

static void *
webp_init(FILE *fp, char *filename, unsigned int page,
          struct ida_image_info *i, int thumbnail)
{
	uint32_t img_size;
	void *img;
	struct webp_state *h;
	h = malloc(sizeof(*h));

	h->f = fp;
	if(fseek(fp, 0, SEEK_END) != 0)
	{
		free(h);
		return NULL;
	}
	img_size = ftell(fp);
	img = malloc(img_size);

	if(fseek(fp, 0, SEEK_SET) != 0)
	{
		free(img);
		free(h);
		return NULL;
	}
	if(fread(img, img_size, 1, fp) != 1)
	{
		free(img);
		free(h);
		return NULL;
	}

	h->data = WebPDecodeRGBA(img, img_size, &h->width, &h->height);

	i->width = h->width;
	i->height = h->height;
	i->dpi = 100;
	i->npages = 1;

	free(img);

	return h;
}


static void
webp_read(unsigned char *dst, unsigned int line, void *data)
{
	struct webp_state *h = data;

	load_rgba(dst, h->data + line * 4 * h->width, h->width);
}


static void
webp_done(void *data)
{
	struct webp_state *h = data;

	free(h->data);
	fclose(h->f);
	free(h);
}


static struct ida_loader webp_loader = {
    magic: "WEBPVP8",
    moff:  8,
    mlen:  7,
    name:  "libwebp",
    init:  webp_init,
    read:  webp_read,
    done:  webp_done,
};


static void __init init_rd(void)
{
    load_register(&webp_loader);
}
