/*
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <libexif/exif-data.h>

#include <jpeglib.h>
#include "jpeg/transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"
#include "genthumbnail.h"

/* ---------------------------------------------------------------------- */

static void dump_exif(FILE *out, ExifData *ed)
{
    const char *title, *value;
#ifdef HAVE_NEW_EXIF
    char buffer[256];
#endif
    ExifEntry  *ee;
    int tag,i;

    for (i = 0; i < EXIF_IFD_COUNT; i++) {
	fprintf(out,"   ifd %s\n", exif_ifd_get_name (i));
	for (tag = 0; tag < 0xffff; tag++) {
	    title = exif_tag_get_title(tag);
	    if (!title)
		continue;
	    ee = exif_content_get_entry (ed->ifd[i], tag);
	    if (NULL == ee)
		continue;
#ifdef HAVE_NEW_EXIF
	    value = exif_entry_get_value(ee, buffer, sizeof(buffer));
#else
	    value = exif_entry_get_value(ee);
#endif
	    fprintf(out,"      0x%04x  %-30s %s\n", tag, title, value);
	}
    }
    if (ed->data && ed->size)
	fprintf(out,"   thumbnail\n      %d bytes data\n", ed->size);
}

static int dump_file(FILE *out, char *filename)
{
    ExifData   *ed;

    ed = exif_data_new_from_file (filename);
    if (NULL == ed) {
	fprintf(stderr,"%s: no EXIF data\n",filename);
	return -1;
    }

    fprintf(out,"%s\n",filename);
    dump_exif(out,ed);
    fprintf(out,"--\n");
    
    exif_data_unref (ed);
    return 0;
}

/* ---------------------------------------------------------------------- */

#define THUMB_MAX 65536

static void
usage(FILE *fp, char *name)
{
    char *h;

    if (NULL != (h = strrchr(name, '/')))
	name = h+1;
    fprintf(fp,
	    "usage: %s [ options ] file\n"
	    "\n"
	    "transform options:\n"
	    "  -a         automatic (using exif orientation tag)\n"
	    "  -9         rotate by 90 degrees\n"
	    "  -1         rotate by 180 degrees\n"
	    "  -2         rotate by 270 degrees\n"
	    "  -f         flip vertical\n"
	    "  -F         flip horizontal\n"
	    "  -t         transpose\n"
	    "  -T         transverse\n"
	    "\n"
	    "  -nt        don't rotate exif thumbnail\n"
	    "  -ni        don't rotate jpeg image\n"
	    "  -no        don't update the orientation tag\n"
	    "\n"
	    "other options:\n"
	    "  -h         print this text\n"
	    "  -d         dump exif data\n"
	    "  -c <text>  create/update comment\n"
	    "  -g         (re)generate thumbnail\n"
	    "  -o <file>  output file\n"
	    "  -i         change files inplace\n"
	    "    -b       create a backup file (with -i)\n"
	    "    -p       preserve timestamps  (with -i)\n"
	    "\n"
	    "-- \n"
	    "Gerd Hoffmann <kraxel@bytesex.org> [SUSE Labs]\n",
	    name);
}

int main(int argc, char *argv[])
{
    JXFORM_CODE transform = JXFORM_NONE;
    unsigned char *comment = NULL;
    unsigned char *outfile = NULL;
    unsigned char *thumbnail = NULL;
    int tsize = 0;
    int inplace = 0;
    unsigned int flags =
	JFLAG_TRANSFORM_IMAGE     |
	JFLAG_TRANSFORM_THUMBNAIL |
	JFLAG_UPDATE_ORIENTATION;
    int dump = 0;
    int i, c, rc;

    for (;;) {
	c = getopt(argc, argv, "hbpid912fFtTagc:o:n:");
	if (c == -1)
	    break;
	switch (c) {
	case '9':
	    transform = JXFORM_ROT_90;
	    break;
	case '1':
	    transform = JXFORM_ROT_180;
	    break;
	case '2':
	    transform = JXFORM_ROT_270;
	    break;
	case 'f':
	    transform = JXFORM_FLIP_V;
	    break;
	case 'F':
	    transform = JXFORM_FLIP_H;
	    break;
	case 't':
	    transform = JXFORM_TRANSPOSE;
	    break;
	case 'T':
	    transform = JXFORM_TRANSVERSE;
	    break;
	case 'a':
	    transform = -1; /* automagic */
	    break;

	case 'n':
	    /* don't ... */
	    switch (optarg[0]) {
	    case 't':
		flags &= ~JFLAG_TRANSFORM_THUMBNAIL;
		break;
	    case 'i':
		flags &= ~JFLAG_TRANSFORM_IMAGE;
		break;
	    case 'o':
		flags &= ~JFLAG_UPDATE_ORIENTATION;
		break;
	    default:
		fprintf(stderr,"unknown option -n%c\n",optarg[0]);
		exit(1);
	    }
	    break;
	    
	case 'c':
	    flags |= JFLAG_UPDATE_COMMENT;
	    comment = optarg;
	    break;
	case 'g':
	    flags |= JFLAG_UPDATE_THUMBNAIL;
	    break;
	case 'o':
	    outfile = optarg;
	    break;
	case 'd':
	    dump = 1;
	    break;

	case 'b':
	    flags |= JFLAG_FILE_BACKUP;
	    break;
	case 'p':
	    flags |= JFLAG_FILE_KEEP_TIME;
	    break;
	case 'i':
	    inplace = 1;
	    break;

	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    /* sanity checks on the arguments */
    if (optind == argc) {
	fprintf(stderr,
		"no image file specified (try -h for more info)\n");
	exit(1);
    }

    /* read-only stuff */
    if (dump) {
	rc = 0;
	for (i = optind; i < argc; i++) {
	    if (0 != dump_file(stdout,argv[i]))
		rc = 1;
	}
	return rc;
    }

    /* r/w sanity checks */
    if (NULL != outfile && optind+1 > argc) {
	fprintf(stderr,
		"when specifying a output file you can process\n"
		"one file at a time only (try -h for more info).\n");
	exit(1);
    }
    if (NULL == outfile && 0 == inplace) {
	fprintf(stderr,
		"you have to either specify a output file (-o <file>)\n"
		"or enable inplace editing (-i). Try -h for more info.\n");
	exit(1);
    }
    if (JXFORM_NONE == transform &&
	!(flags & JFLAG_UPDATE_COMMENT) &&
	!(flags & JFLAG_UPDATE_THUMBNAIL)) {
	fprintf(stderr,
		"What do you want to do today?  Neither a new comment nor a\n"
		"tranformation operation was specified (try -h for more info).\n");
	exit(1);
    }

    /* do actual update work */
    if (outfile) {
	if (flags & JFLAG_UPDATE_THUMBNAIL) {
	    thumbnail = malloc(THUMB_MAX);
	    tsize = create_thumbnail(argv[optind],thumbnail,THUMB_MAX);
	}
	return jpeg_transform_files(argv[optind], outfile, transform,
				    comment, thumbnail, tsize, flags);
    } else {
	rc = 0;
	for (i = optind; i < argc; i++) {
	    fprintf(stderr,"processing %s\n",argv[i]);
	    if (flags & JFLAG_UPDATE_THUMBNAIL) {
		thumbnail = malloc(THUMB_MAX);
		tsize = create_thumbnail(argv[i],thumbnail,THUMB_MAX);
	    }
	    if (0 != jpeg_transform_inplace(argv[i], transform, comment,
					    thumbnail, tsize, flags))
		rc = 1;
	}
	return rc;
    }
}
