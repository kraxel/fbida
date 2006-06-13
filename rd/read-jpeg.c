#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <jpeglib.h>
#include <setjmp.h>

#include <libexif/exif-data.h>

#include "readers.h"
#include "misc.h"

/* ---------------------------------------------------------------------- */
/* load                                                                   */

struct jpeg_state {
    FILE * infile;                /* source file */
    
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    jmp_buf errjump;
    JSAMPARRAY buffer;            /* Output row buffer */
    int row_stride,linelength;    /* physical row width in output buffer */
    unsigned char *image,*ptr;

    /* thumbnail */
    unsigned char  *thumbnail;
    unsigned int   tpos, tsize;
};

/* ---------------------------------------------------------------------- */
/* data source manager for thumbnail images                               */

static void thumbnail_src_init(struct jpeg_decompress_struct *cinfo)
{
    struct jpeg_state *h  = container_of(cinfo, struct jpeg_state, cinfo);
    cinfo->src->next_input_byte = h->thumbnail;
    cinfo->src->bytes_in_buffer = h->tsize;
}

static int thumbnail_src_fill(struct jpeg_decompress_struct *cinfo)
{
    fprintf(stderr,"jpeg: panic: no more thumbnail input data\n");
    exit(1);
}

static void thumbnail_src_skip(struct jpeg_decompress_struct *cinfo,
			       long num_bytes)
{
    cinfo->src->next_input_byte += num_bytes;
}

static void thumbnail_src_term(struct jpeg_decompress_struct *cinfo)
{
    /* nothing */
}

static struct jpeg_source_mgr thumbnail_mgr = {
    .init_source         = thumbnail_src_init,
    .fill_input_buffer   = thumbnail_src_fill,
    .skip_input_data     = thumbnail_src_skip,
    .resync_to_restart   = jpeg_resync_to_restart,
    .term_source         = thumbnail_src_term,
};

/* ---------------------------------------------------------------------- */
/* jpeg loader                                                            */

static void jerror_exit(j_common_ptr info)
{
    struct jpeg_decompress_struct *cinfo = (struct jpeg_decompress_struct *)info;
    struct jpeg_state *h  = container_of(cinfo, struct jpeg_state, cinfo);
    cinfo->err->output_message(info);
    longjmp(h->errjump, 1);
    jpeg_destroy_decompress(cinfo);
    exit(1);
}

static void*
jpeg_init(FILE *fp, char *filename, unsigned int page,
	  struct ida_image_info *i, int thumbnail)
{
    struct jpeg_state *h;
    jpeg_saved_marker_ptr mark;
    
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->infile = fp;

    h->cinfo.err = jpeg_std_error(&h->jerr);
    h->cinfo.err->error_exit = jerror_exit;
    if(setjmp(h->errjump))
	return 0;

    jpeg_create_decompress(&h->cinfo);
    jpeg_save_markers(&h->cinfo, JPEG_COM,    0xffff); /* comment */
    jpeg_save_markers(&h->cinfo, JPEG_APP0+1, 0xffff); /* EXIF */
    jpeg_stdio_src(&h->cinfo, h->infile);
    jpeg_read_header(&h->cinfo, TRUE);

    for (mark = h->cinfo.marker_list; NULL != mark; mark = mark->next) {
	switch (mark->marker) {
	case JPEG_COM:
	    if (debug)
		fprintf(stderr,"jpeg: comment found (COM marker) [%.*s]\n",
			(int)mark->data_length, mark->data);
	    load_add_extra(i,EXTRA_COMMENT,mark->data,mark->data_length);
	    break;
	case JPEG_APP0 +1:
	    if (debug)
		fprintf(stderr,"jpeg: exif data found (APP1 marker)\n");
	    load_add_extra(i,EXTRA_COMMENT,mark->data,mark->data_length);

	    if (thumbnail) {
		ExifData *ed;
		
		ed = exif_data_new_from_data(mark->data,mark->data_length);
		if (ed->data &&
		    ed->data[0] == 0xff &&
		    ed->data[1] == 0xd8) {
		    if (debug)
			fprintf(stderr,"jpeg: exif thumbnail found\n");

		    /* save away thumbnail data */
		    h->thumbnail = malloc(ed->size);
		    h->tsize = ed->size;
		    memcpy(h->thumbnail,ed->data,ed->size);
		}
		exif_data_unref(ed);
	    }
	    break;
	}
    }

    if (h->thumbnail) {
	/* save image size */
	i->thumbnail   = 1;
	i->real_width  = h->cinfo.image_width;
	i->real_height = h->cinfo.image_height;

	/* re-setup jpeg */
	jpeg_destroy_decompress(&h->cinfo);
	fclose(h->infile);
	h->infile = NULL;
	jpeg_create_decompress(&h->cinfo);
	h->cinfo.src = &thumbnail_mgr;
	jpeg_read_header(&h->cinfo, TRUE);
    }

    h->cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&h->cinfo);
    i->width  = h->cinfo.image_width;
    i->height = h->cinfo.image_height;
    i->npages = 1;
    switch (h->cinfo.density_unit) {
    case 0: /* unknown */
	break;
    case 1: /* dot per inch */
	i->dpi = h->cinfo.X_density;
	break;
    case 2: /* dot per cm */
	i->dpi = res_cm_to_inch(h->cinfo.X_density);
	break;
    }

    return h;
}

static void
jpeg_read(unsigned char *dst, unsigned int line, void *data)
{
    struct jpeg_state *h = data;
    JSAMPROW row = dst;

    if(setjmp(h->errjump))
	return;
    jpeg_read_scanlines(&h->cinfo, &row, 1);
}

static void
jpeg_done(void *data)
{
    struct jpeg_state *h = data;

    if (setjmp(h->errjump))
	return;
    jpeg_destroy_decompress(&h->cinfo);
    if (h->infile)
	fclose(h->infile);
    if (h->thumbnail)
	free(h->thumbnail);
    free(h);
}

struct ida_loader jpeg_loader = {
    magic: "\xff\xd8",
    moff:  0,
    mlen:  2,
    name:  "libjpeg",
    init:  jpeg_init,
    read:  jpeg_read,
    done:  jpeg_done,
};

static void __init init_rd(void)
{
    load_register(&jpeg_loader);
}
