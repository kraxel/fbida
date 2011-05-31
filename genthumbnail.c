#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <jpeglib.h>
#include "transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"

#include "misc.h"

#include "readers.h"
#include "filter.h"
#include "genthumbnail.h"

/* ---------------------------------------------------------------------- */

static struct ida_image*
read_jpeg(char *filename)
{
    struct ida_image *img;
    FILE *fp;
    unsigned int y;
    void *data;

    /* open file */
    if (NULL == (fp = fopen(filename, "r"))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return NULL;
    }

    /* load image */
    img = malloc(sizeof(*img));
    memset(img,0,sizeof(*img));
    data = jpeg_loader.init(fp,filename,0,&img->i,0);
    if (NULL == data) {
	fprintf(stderr,"loading %s [%s] FAILED\n",filename,jpeg_loader.name);
	free(img);
	return NULL;
    }
    img->data = malloc(img->i.width * img->i.height * 3);
    for (y = 0; y < img->i.height; y++)
  	jpeg_loader.read(img->data + img->i.width * 3 * y, y, data);
    jpeg_loader.done(data);
    return img;
}

/* ---------------------------------------------------------------------- */

static struct ida_image*
scale_thumbnail(struct ida_image *src, int max)
{
    struct op_resize_parm p;
    struct ida_rect  rect;
    struct ida_image *dest;
    void *data;
    unsigned int y;
    float xs,ys,scale;
    
    xs = (float)max / src->i.width;
    ys = (float)max / src->i.height;
    scale = (xs < ys) ? xs : ys;

    dest = malloc(sizeof(*dest));
    memset(dest,0,sizeof(*dest));
    memset(&rect,0,sizeof(rect));
    memset(&p,0,sizeof(p));
    
    p.width  = src->i.width  * scale;
    p.height = src->i.height * scale;
    p.dpi    = src->i.dpi;
    if (0 == p.width)
	p.width = 1;
    if (0 == p.height)
	p.height = 1;
    
    data = desc_resize.init(src,&rect,&dest->i,&p);
    dest->data = malloc(dest->i.width * dest->i.height * 3);
    for (y = 0; y < dest->i.height; y++)
	desc_resize.work(src,&rect,
			 dest->data + 3 * dest->i.width * y,
			 y, data);
    desc_resize.done(data);
    return dest;
}

/* ---------------------------------------------------------------------- */

struct thc {
    struct jpeg_compress_struct dst;
    struct jpeg_error_mgr err;
    unsigned char *out;
    int osize;
};

static void thc_dest_init(struct jpeg_compress_struct *cinfo)
{
    struct thc *h  = container_of(cinfo, struct thc, dst);
    cinfo->dest->next_output_byte = h->out;
    cinfo->dest->free_in_buffer   = h->osize;
}

static boolean thc_dest_flush(struct jpeg_compress_struct *cinfo)
{
    fprintf(stderr,"jpeg: panic: output buffer full\n");
    exit(1);
}

static void thc_dest_term(struct jpeg_compress_struct *cinfo)
{
    struct thc *h  = container_of(cinfo, struct thc, dst);
    h->osize -= cinfo->dest->free_in_buffer;
}

static struct jpeg_destination_mgr thumbnail_dst = {
    .init_destination    = thc_dest_init,
    .empty_output_buffer = thc_dest_flush,
    .term_destination    = thc_dest_term,
};

static int
compress_thumbnail(struct ida_image *img, char *dest, int max)
{
    struct thc thc;
    unsigned char *line;
    unsigned int i;

    memset(&thc,0,sizeof(thc));
    thc.dst.err = jpeg_std_error(&thc.err);
    jpeg_create_compress(&thc.dst);
    thc.dst.dest = &thumbnail_dst;
    thc.out = dest;
    thc.osize = max;

    thc.dst.image_width  = img->i.width;
    thc.dst.image_height = img->i.height;
    thc.dst.input_components = 3;
    thc.dst.in_color_space = JCS_RGB;
    jpeg_set_defaults(&thc.dst);
    jpeg_start_compress(&thc.dst, TRUE);

    for (i = 0, line = img->data; i < img->i.height; i++, line += img->i.width*3)
        jpeg_write_scanlines(&thc.dst, &line, 1);
    
    jpeg_finish_compress(&(thc.dst));
    jpeg_destroy_compress(&(thc.dst));

    return thc.osize;
}

/* ---------------------------------------------------------------------- */

int create_thumbnail(char *filename, unsigned char *dest, int max)
{
    struct ida_image *img,*thumb;
    int size;

    //fprintf(stderr,"%s: read ",filename);
    img = read_jpeg(filename);
    if (!img) {
	fprintf(stderr,"FAILED\n");
	return -1;
    }
    
    //fprintf(stderr,"scale ");
    thumb = scale_thumbnail(img,160);
    if (!thumb) {
	free(img->data);
	free(img);
	fprintf(stderr,"FAILED\n");
	return -1;
    }

    //fprintf(stderr,"compress ");
    size = compress_thumbnail(thumb,dest,max);

    /* cleanup */
    free(img->data);
    free(img);
    free(thumb->data);
    free(thumb);
    return size;
}

/* ---------------------------------------------------------------------- */

#if 0

#define THUMB_MAX 65536

static int handle_image(char *filename)
{
    char *dest;
    int size;

    dest = malloc(THUMB_MAX);
    size = create_thumbnail(filename,dest,THUMB_MAX);

    fprintf(stderr,"transform ");
    jpeg_transform_inplace(filename, JXFORM_NONE, NULL,
			   dest, size, JFLAG_UPDATE_THUMBNAIL);

    fprintf(stderr,"done\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++)
	handle_image(argv[i]);
    return 0;
}

#endif

