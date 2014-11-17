/*
 * jpegtran.c
 *
 * Copyright (C) 1995-1997, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * plenty of changes by Gerd Hoffmann <gerd@kraxel.org>, with focus on
 * digital image processing and sane exif handling:
 *
 *   - does transformations only (flip/rotate/transpose/transverse).
 *   - also transforms the exif thumbnail if present.
 *   - can automatically figure transformation from the
 *     exif orientation tag.
 *   - updates the exif orientation tag.
 *   - updates the exif pixel dimension tags.
 *
 * This file contains a command-line user interface for JPEG transcoding.
 * It is very similar to cjpeg.c, but provides lossless transcoding between
 * different JPEG file formats.  It also provides some lossless and sort-of-
 * lossless transformations of JPEG data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <utime.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <jpeglib.h>
#include "transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"

#include "misc.h"

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-tag.h>

static int do_transform(struct jpeg_decompress_struct *src,
			struct jpeg_compress_struct   *dst,
			JXFORM_CODE transform,
			unsigned char *comment,
			char *thumbnail, int tsize,
			unsigned int flags);

static JXFORM_CODE transmagic[] = {
    [ 1 ] = JXFORM_NONE,
    [ 2 ] = JXFORM_FLIP_H,
    [ 3 ] = JXFORM_ROT_180,
    [ 4 ] = JXFORM_FLIP_V,
    [ 5 ] = JXFORM_TRANSPOSE,
    [ 6 ] = JXFORM_ROT_90,
    [ 7 ] = JXFORM_TRANSVERSE,
    [ 8 ] = JXFORM_ROT_270,
};

#if 0
static char *transname[] = {
    [ JXFORM_NONE ]       = "none",
    [ JXFORM_FLIP_H ]     = "flip h",
    [ JXFORM_FLIP_V ]     = "flip v",
    [ JXFORM_TRANSPOSE ]  = "transpose",
    [ JXFORM_TRANSVERSE ] = "transverse",
    [ JXFORM_ROT_90 ]     = "rot 90",
    [ JXFORM_ROT_180 ]    = "rot 190",
    [ JXFORM_ROT_270 ]    = "rot 270",
};
#endif

/* ---------------------------------------------------------------------- */

/* libjpeg error handler -- exit via longjump */
struct longjmp_error_mgr {
    struct jpeg_error_mgr jpeg;
    jmp_buf setjmp_buffer;
};

static void longjmp_error_exit(j_common_ptr cinfo)
{
    struct longjmp_error_mgr *h = (struct longjmp_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(h->setjmp_buffer, 1);
}

/* ---------------------------------------------------------------------- */

static long get_int(ExifData *ed, ExifEntry *ee)
{
    ExifByteOrder o = exif_data_get_byte_order(ed);
    long value;

    switch (ee->format) {
    case EXIF_FORMAT_SHORT:
	value = exif_get_short (ee->data, o);
	break;
    case EXIF_FORMAT_LONG:
	value = exif_get_long (ee->data, o);
	break;
    case EXIF_FORMAT_SLONG:
	value = exif_get_slong (ee->data, o);
	break;
    default:
	fprintf(stderr,"get_int oops\n");
	exit(1);
    }
    return value;
}

static void set_int(ExifData *ed, ExifEntry *ee, long value)
{
    ExifByteOrder o = exif_data_get_byte_order(ed);

    switch (ee->format) {
    case EXIF_FORMAT_SHORT:
	exif_set_short (ee->data, o, value);
	break;
    case EXIF_FORMAT_LONG:
	exif_set_long (ee->data, o, value);
	break;
    case EXIF_FORMAT_SLONG:
	exif_set_slong (ee->data, o, value);
	break;
    default:
	fprintf(stderr,"set_int oops\n");
	exit(1);
    }
}

static void update_orientation(ExifData *ed, int ifd, int orientation)
{
    ExifEntry *ee;

    ee = exif_content_get_entry(ed->ifd[ifd], 0x0112);
    if (NULL == ee)
	return;
    set_int(ed,ee,orientation);
}

static void update_dimension(ExifData *ed, JXFORM_CODE transform,
			     int src_x, int src_y)
{
    static struct {
	int idf;
	int tag;
	int x;
    } fields[] = {
	{
	    .idf = EXIF_IFD_EXIF,
	    .tag = EXIF_TAG_PIXEL_X_DIMENSION,
	    .x   = 1,
	},{
	    .idf = EXIF_IFD_EXIF,
	    .tag = EXIF_TAG_PIXEL_Y_DIMENSION,
	    .x   = 0,
	},{
	    .idf = EXIF_IFD_INTEROPERABILITY,
	    .tag = EXIF_TAG_RELATED_IMAGE_WIDTH,
	    .x   = 1,
	},{
	    .idf = EXIF_IFD_INTEROPERABILITY,
	    .tag = EXIF_TAG_RELATED_IMAGE_LENGTH,
	    .x   = 0,
	}
    };
    ExifEntry *ee;
    int i;

    for (i = 0; i < sizeof(fields)/sizeof(fields[0]); i++) {
	ee = exif_content_get_entry(ed->ifd[fields[i].idf], fields[i].tag);
	if (!ee)
	    continue;
	switch (transform) {
	case JXFORM_ROT_90:
	case JXFORM_ROT_270:
	case JXFORM_TRANSPOSE:
	case JXFORM_TRANSVERSE:
	    /* x/y reversed */
	    set_int(ed, ee, fields[i].x ? src_y : src_x);
	    break;
	default:
	    /* normal */
	    set_int(ed, ee, fields[i].x ? src_x : src_y);
	    break;
	}
    }
}

static int get_orientation(ExifData *ed)
{
    ExifEntry *ee;

    ee = exif_content_get_entry(ed->ifd[EXIF_IFD_0], 0x0112);
    if (NULL == ee)
	return 1; /* top - left */
    return get_int(ed,ee);
}

static int get_file_orientation(const char *file)
{
    ExifData *ed;
    int ret;

    ed = exif_data_new_from_file(file);
    ret = ed ? get_orientation(ed) : 1 /* top - left */;
    exif_data_unref(ed);
    return ret;
}

/* ---------------------------------------------------------------------- */

struct th {
    struct jpeg_decompress_struct src;
    struct jpeg_compress_struct   dst;
    struct jpeg_error_mgr jsrcerr, jdsterr;
    unsigned char *in;
    unsigned char *out;
    int isize, osize;
};

static void thumbnail_src_init(struct jpeg_decompress_struct *cinfo)
{
    struct th *h  = container_of(cinfo, struct th, src);
    cinfo->src->next_input_byte = h->in;
    cinfo->src->bytes_in_buffer = h->isize;
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

static void thumbnail_dest_init(struct jpeg_compress_struct *cinfo)
{
    struct th *h  = container_of(cinfo, struct th, dst);
    h->osize = h->isize * 2;
    h->out   = malloc(h->osize);
    cinfo->dest->next_output_byte = h->out;
    cinfo->dest->free_in_buffer   = h->osize;
}

static boolean thumbnail_dest_flush(struct jpeg_compress_struct *cinfo)
{
    fprintf(stderr,"jpeg: panic: output buffer full\n");
    exit(1);
}

static void thumbnail_dest_term(struct jpeg_compress_struct *cinfo)
{
    struct th *h  = container_of(cinfo, struct th, dst);
    h->osize -= cinfo->dest->free_in_buffer;
}

static struct jpeg_source_mgr thumbnail_src = {
    .init_source         = thumbnail_src_init,
    .fill_input_buffer   = thumbnail_src_fill,
    .skip_input_data     = thumbnail_src_skip,
    .resync_to_restart   = jpeg_resync_to_restart,
    .term_source         = thumbnail_src_term,
};

static struct jpeg_destination_mgr thumbnail_dst = {
    .init_destination    = thumbnail_dest_init,
    .empty_output_buffer = thumbnail_dest_flush,
    .term_destination    = thumbnail_dest_term,
};

static void do_thumbnail(ExifData *ed, JXFORM_CODE transform)
{
    struct th th;

    if (JXFORM_NONE == transform)
	return;

    memset(&th,0,sizeof(th));
    th.in    = ed->data;
    th.isize = ed->size;

    /* setup src */
    th.src.err = jpeg_std_error(&th.jsrcerr);
    jpeg_create_decompress(&th.src);
    th.src.src = &thumbnail_src;

    /* setup dst */
    th.dst.err = jpeg_std_error(&th.jdsterr);
    jpeg_create_compress(&th.dst);
    th.dst.dest = &thumbnail_dst;

    /* transform image */
    do_transform(&th.src,&th.dst,transform,NULL,NULL,0,JFLAG_TRANSFORM_IMAGE);

    /* cleanup */
    jpeg_destroy_decompress(&th.src);
    jpeg_destroy_compress(&th.dst);

    /* replace thumbnail */
    free(ed->data);
    ed->data = th.out;
    ed->size = th.osize;
}

static void do_exif(struct jpeg_decompress_struct *src,
		    JXFORM_CODE *transform,
		    char *thumbnail, int tsize,
		    unsigned int flags)
{
    jpeg_saved_marker_ptr mark;
    ExifData *ed = NULL;
    unsigned char *data;
    unsigned int  size;

    for (mark = src->marker_list; NULL != mark; mark = mark->next) {
	if (mark->marker != JPEG_APP0 +1)
	    continue;
	ed = exif_data_new_from_data(mark->data,mark->data_length);
	break;
    }
    if (flags & JFLAG_UPDATE_THUMBNAIL) {
	if (NULL == ed)
	    ed = exif_data_new();
	if (NULL == mark) {
	    mark = src->mem->alloc_large((j_common_ptr)src,JPOOL_IMAGE,sizeof(*mark));
	    memset(mark,0,sizeof(*mark));
	    mark->marker = JPEG_APP0 +1;
	    mark->next = src->marker_list;
	    src->marker_list = mark;
	}
	if (ed->data)
	    free(ed->data);
	ed->data = thumbnail;
	ed->size = tsize;
    }
    if (NULL == ed)
	return;

    if (-1 == *transform) {
	/* automagic image transformation */
	int orientation = get_orientation(ed);
	*transform = JXFORM_NONE;
	if (orientation >= 1 && orientation <= 8)
	    *transform = transmagic[orientation];
#if 0
	if (debug)
	    fprintf(stderr,"autotrans: %s\n",transname[*transform]);
#endif
    }

    /* update exif data */
    if (flags & JFLAG_UPDATE_ORIENTATION) {
	update_orientation(ed,EXIF_IFD_0,1);
	update_orientation(ed,EXIF_IFD_1,1);
    }
    if (ed->data && ed->data[0] == 0xff && ed->data[1] == 0xd8 &&
	(flags & JFLAG_TRANSFORM_THUMBNAIL))
	do_thumbnail(ed,*transform);
    update_dimension(ed, (flags & JFLAG_TRANSFORM_IMAGE) ? *transform : JXFORM_NONE,
		     src->image_width, src->image_height);

    /* build new exif data block */
    exif_data_save_data(ed,&data,&size);
    exif_data_unref(ed);

    /* update jpeg APP1 (EXIF) marker */
    mark->data = src->mem->alloc_large((j_common_ptr)src,JPOOL_IMAGE,size);
    mark->original_length = size;
    mark->data_length = size;
    memcpy(mark->data,data,size);
    free(data);
}

/* ---------------------------------------------------------------------- */

static void do_comment(struct jpeg_decompress_struct *src,
		       unsigned char *comment)
{
    jpeg_saved_marker_ptr mark;
    int size;

    /* find or create comment marker */
    for (mark = src->marker_list;; mark = mark->next) {
	if (mark->marker == JPEG_COM)
	    break;
	if (NULL == mark->next) {
	    mark->next = src->mem->alloc_large((j_common_ptr)src,JPOOL_IMAGE,
					       sizeof(*mark));
	    mark = mark->next;
	    memset(mark,0,sizeof(*mark));
	    mark->marker = JPEG_COM;
	    break;
	}
    }

    /* update comment marker */
    size = strlen(comment) +1;
    mark->data = src->mem->alloc_large((j_common_ptr)src,JPOOL_IMAGE,size);
    mark->original_length = size;
    mark->data_length = size;
    memcpy(mark->data,comment,size);
}

static int do_transform(struct jpeg_decompress_struct *src,
			struct jpeg_compress_struct *dst,
			JXFORM_CODE transform,
			unsigned char *comment,
			char *thumbnail, int tsize,
			unsigned int flags)
{
    jvirt_barray_ptr * src_coef_arrays;
    jvirt_barray_ptr * dst_coef_arrays;
    jpeg_transform_info transformoption;

    jcopy_markers_setup(src, JCOPYOPT_ALL);
    if (JPEG_HEADER_OK != jpeg_read_header(src, TRUE))
	return -1;

    do_exif(src,&transform,thumbnail,tsize,flags);
    if (-1 == transform)
	transform = JXFORM_NONE;
    if (!(flags & JFLAG_TRANSFORM_IMAGE))
	transform = JXFORM_NONE;
    if ((flags & JFLAG_UPDATE_COMMENT) && NULL != comment)
	do_comment(src,comment);

    memset(&transformoption,0,sizeof(transformoption));
    transformoption.transform = transform;
    if (!(flags & JFLAG_TRANSFORM_TRIM))
        transformoption.trim       = TRUE;
    else
        transformoption.trim       = FALSE;
    transformoption.force_grayscale = FALSE;

    /* Any space needed by a transform option must be requested before
     * jpeg_read_coefficients so that memory allocation will be done right.
     */
    jtransform_request_workspace(src, &transformoption);
    src_coef_arrays = jpeg_read_coefficients(src);
    jpeg_copy_critical_parameters(src, dst);
    dst_coef_arrays = jtransform_adjust_parameters
	(src, dst, src_coef_arrays, &transformoption);

    /* Start compressor (note no image data is actually written here) */
    jpeg_write_coefficients(dst, dst_coef_arrays);

    /* Copy to the output file any extra markers that we want to preserve */
    jcopy_markers_execute(src, dst, JCOPYOPT_ALL);

    /* Execute image transformation, if any */
    jtransform_execute_transformation(src, dst,
				      src_coef_arrays,
				      &transformoption);

    /* Finish compression and release memory */
    jpeg_finish_compress(dst);
    jpeg_finish_decompress(src);

    return 0;
}

/* ---------------------------------------------------------------------- */

int jpeg_transform_fp(FILE *in, FILE *out,
		      JXFORM_CODE transform,
		      unsigned char *comment,
		      char *thumbnail, int tsize,
		      unsigned int flags)
{
    struct jpeg_decompress_struct src;
    struct jpeg_compress_struct   dst;
    struct jpeg_error_mgr jdsterr;
    struct longjmp_error_mgr jsrcerr;

    /* setup src */
    src.err = jpeg_std_error(&jsrcerr.jpeg);
    jsrcerr.jpeg.error_exit = longjmp_error_exit;
    if (setjmp(jsrcerr.setjmp_buffer))
	/* something went wrong within the jpeg library ... */
	goto oops;
    jpeg_create_decompress(&src);
    jpeg_stdio_src(&src, in);

    /* setup dst */
    dst.err = jpeg_std_error(&jdsterr);
    jpeg_create_compress(&dst);
    jpeg_stdio_dest(&dst, out);

    /* transform image */
    do_transform(&src,&dst,transform,comment,thumbnail,tsize,flags);

    /* cleanup */
    jpeg_destroy_decompress(&src);
    jpeg_destroy_compress(&dst);
    return 0;

 oops:
    jpeg_destroy_decompress(&src);
    jpeg_destroy_compress(&dst);
    return -1;
}

int jpeg_transform_files(char *infile, char *outfile,
			 JXFORM_CODE transform,
			 unsigned char *comment,
			 char *thumbnail, int tsize,
			 unsigned int flags)
{
    int rc;
    FILE *in;
    FILE *out;

    /* open infile */
    in = fopen(infile,"r");
    if (NULL == in) {
	fprintf(stderr,"open %s: %s\n",infile,strerror(errno));
	return -1;
    }

    /* open outfile */
    out = fopen(outfile,"w");
    if (NULL == out) {
	fprintf(stderr,"open %s: %s\n",outfile,strerror(errno));
	fclose(in);
	return -1;
    }

    /* go! */
    rc = jpeg_transform_fp(in,out,transform,comment,thumbnail,tsize,flags);
    fclose(in);
    fclose(out);

    return rc;
}

int jpeg_transform_inplace(char *file,
			   JXFORM_CODE transform,
			   unsigned char *comment,
			   char *thumbnail, int tsize,
			   unsigned int flags)
{
    char *tmpfile;
    char *bakfile;
    struct stat st;
    int fd;
    FILE *in  = NULL;
    FILE *out = NULL;

    /* are we allowed to write to the file? */
    if (0 != access(file, R_OK | W_OK)) {
	fprintf(stderr,"access %s: %s\n",file,strerror(errno));
	return -1;
    }

    if (!(flags & JFLAG_UPDATE_COMMENT) &&
        !(flags & JFLAG_UPDATE_THUMBNAIL)) {
        /* no forced updates, maybe we can shortcut here? */
        if (transform == JXFORM_NONE)
            return 0;
        if (transform == -1 && get_file_orientation(file) == 1)
            return 0;
    }

    /* open infile */
    in = fopen(file,"r");
    if (NULL == in) {
	fprintf(stderr,"open %s: %s\n",file,strerror(errno));
	return -1;
    }

    /* open tmpfile */
    tmpfile = malloc(strlen(file)+10);
    sprintf(tmpfile,"%s.XXXXXX",file);
    fd = mkstemp(tmpfile);
    if (-1 == fd) {
	fprintf(stderr,"mkstemp(%s): %s\n",tmpfile,strerror(errno));
	goto oops;
    }
    out = fdopen(fd,"w");

    /* copy owner and permissions */
    if (-1 == fstat(fileno(in),&st)) {
	fprintf(stderr,"fstat(%s): %s\n",file,strerror(errno));
	goto oops;
    }
    if (-1 == fchown(fileno(out),st.st_uid,st.st_gid)) {
	fprintf(stderr,"fchown(%s): %s\n",tmpfile,strerror(errno));
	goto oops;
    }
    if (-1 == fchmod(fileno(out),st.st_mode)) {
	fprintf(stderr,"fchmod(%s): %s\n",tmpfile,strerror(errno));
	goto oops;
    }

    /* transform */
    if (0 != jpeg_transform_fp(in,out,transform,comment,thumbnail,tsize,flags))
	goto oops;

    /* worked ok -- commit */
    fclose(in);
    fclose(out);
    if (flags & JFLAG_FILE_BACKUP) {
	bakfile = malloc(strlen(file)+2);
	sprintf(bakfile,"%s~",file);
	rename(file,bakfile);
	free(bakfile);
    }
    rename(tmpfile,file);
    if (flags & JFLAG_FILE_KEEP_TIME) {
	struct utimbuf u;
	u.actime = st.st_atime;
	u.modtime = st.st_mtime;
	utime(file,&u);
    }

    /* cleanup & return */
    free(tmpfile);
    return 0;

 oops:
    /* something went wrong -- rollback */
    if (in)
	fclose(in);
    if (out) {
	fclose(out);
	unlink(tmpfile);
    }
    return -1;
}
