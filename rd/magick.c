#include <stdio.h>
#include <stdlib.h>
#include <magick/api.h>

#include "loader.h"
#include "viewer.h"

extern char *binary;

static int first = 1;
static ExceptionInfo exception;

struct magick_state {
    ImageInfo *image_info;
    Image *image;
};

static void*
magick_init(FILE *fp, char *filename, int *width, int *height)
{
    struct magick_state *h;

    /* libmagick wants a filename */
    fclose(fp);

    if (first) {
	/* init library first time */
	MagickIncarnate(binary);
	GetExceptionInfo(&exception);
	first = 0;
    }
    
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->image_info=CloneImageInfo(NULL);
    strcpy(h->image_info->filename,filename);
    h->image = ReadImage(h->image_info,&exception);
    if (NULL == h->image) {
	MagickError(exception.severity,exception.reason,exception.description);
	goto oops;
    }

    *width  = h->image->rows;
    *height = h->image->columns;
    return h;

 oops:
    if (h->image)
	DestroyImage(h->image);
    if (h->image_info)
	DestroyImageInfo(h->image_info);
    free(h);
    return NULL;
}

static void
magick_read(unsigned char *dst, int line, void *data)
{
    struct magick_state *h = data;
    DispatchImage (h->image,0,line,h->image->columns, 1,
		   "RGB", 0, data);
}

static void
magick_done(void *data)
{
    struct magick_state *h = data;

    DestroyImageInfo(h->image_info);
    DestroyImage(h->image);
    free(h);
}

static struct ida_loader magick_loader = {
    name:  "libmagick",
    init:  magick_init,
    read:  magick_read,
    done:  magick_done,
};

static void __init init_rd(void)
{
    load_register(&magick_loader);
}
