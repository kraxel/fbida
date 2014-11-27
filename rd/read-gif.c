#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <gif_lib.h>

#include "readers.h"

#if defined(GIFLIB_MAJOR) && (GIFLIB_MAJOR >= 5)
#define GIF5DATA(e)		e
static void PrintGifError(int err)
{
    fprintf(stderr, "GIF library error: %s\n", GifErrorString(err));
}
#else
#define GIF5DATA(x)
#define PrintGifError(e)	PrintGifError()
#define DGifOpenFileHandle(x,e)	DGifOpenFileHandle(x)
#define DGifCloseFile(x,e)	DGifCloseFile(x)
#endif

struct gif_state {
    FILE         *infile;
    GifFileType  *gif;
    GifPixelType *row;
    GifPixelType *il;
    int w,h;
};

static GifRecordType
gif_fileread(struct gif_state *h)
{
    GifRecordType RecordType;
    GifByteType *Extension;
    int ExtCode, rc;
    char *type;

    for (;;) {
	if (GIF_ERROR == DGifGetRecordType(h->gif,&RecordType)) {
	    if (debug)
		fprintf(stderr,"gif: DGifGetRecordType failed\n");
	    PrintGifError(h->gif->Error);
	    return -1;
	}
	switch (RecordType) {
	case IMAGE_DESC_RECORD_TYPE:
	    if (debug)
		fprintf(stderr,"gif: IMAGE_DESC_RECORD_TYPE found\n");
	    return RecordType;
	case EXTENSION_RECORD_TYPE:
	    if (debug)
		fprintf(stderr,"gif: EXTENSION_RECORD_TYPE found\n");
	    for (rc = DGifGetExtension(h->gif,&ExtCode,&Extension);
		 NULL != Extension;
		 rc = DGifGetExtensionNext(h->gif,&Extension)) {
		if (rc == GIF_ERROR) {
		    if (debug)
			fprintf(stderr,"gif: DGifGetExtension failed\n");
		    PrintGifError(h->gif->Error);
		    return -1;
		}
		if (debug) {
		    switch (ExtCode) {
		    case COMMENT_EXT_FUNC_CODE:     type="comment";   break;
		    case GRAPHICS_EXT_FUNC_CODE:    type="graphics";  break;
		    case PLAINTEXT_EXT_FUNC_CODE:   type="plaintext"; break;
		    case APPLICATION_EXT_FUNC_CODE: type="appl";      break;
		    default:                        type="???";       break;
		    }
		    fprintf(stderr,"gif: extcode=0x%x [%s]\n",ExtCode,type);
		}
	    }
	    break;
	case TERMINATE_RECORD_TYPE:
	    if (debug)
		fprintf(stderr,"gif: TERMINATE_RECORD_TYPE found\n");
	    return RecordType;
	default:
	    if (debug)
		fprintf(stderr,"gif: unknown record type [%d]\n",RecordType);
	    return -1;
	}
    }
}

#if 0
static void
gif_skipimage(struct gif_state *h)
{
    unsigned char *line;
    int i;

    if (debug)
	fprintf(stderr,"gif: skipping image record ...\n");
    DGifGetImageDesc(h->gif);
    line = malloc(h->gif->SWidth);
    for (i = 0; i < h->gif->SHeight; i++)
	DGifGetLine(h->gif, line, h->gif->SWidth);
    free(line);
}
#endif

static void*
gif_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *info, int thumbnail)
{
    struct gif_state *h;
    GifRecordType RecordType;
    int i, image = 0;
    GIF5DATA(int giferror = 0;)
    
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->infile = fp;
    h->gif = DGifOpenFileHandle(fileno(fp), &giferror);
    h->row = malloc(h->gif->SWidth * sizeof(GifPixelType));

    while (0 == image) {
	RecordType = gif_fileread(h);
	switch (RecordType) {
	case IMAGE_DESC_RECORD_TYPE:
	    if (GIF_ERROR == DGifGetImageDesc(h->gif)) {
		if (debug)
		    fprintf(stderr,"gif: DGifGetImageDesc failed\n");
		PrintGifError(giferror);
	    }
	    if (NULL == h->gif->SColorMap &&
		NULL == h->gif->Image.ColorMap) {
		if (debug)
		    fprintf(stderr,"gif: oops: no colormap found\n");
		goto oops;
	    }
#if 0
	    info->width  = h->w = h->gif->SWidth;
	    info->height = h->h = h->gif->SHeight;
#else
	    info->width  = h->w = h->gif->Image.Width;
	    info->height = h->h = h->gif->Image.Height;
#endif
            info->npages = 1;
	    image = 1;
	    if (debug)
		fprintf(stderr,"gif: reading image record ...\n");
	    if (h->gif->Image.Interlace) {
		if (debug)
		    fprintf(stderr,"gif: interlaced\n");
		h->il = malloc(h->w * h->h * sizeof(GifPixelType));
		for (i = 0; i < h->h; i += 8)
		    DGifGetLine(h->gif, h->il + h->w*i,h->w);
		for (i = 4; i < h->gif->SHeight; i += 8)
		    DGifGetLine(h->gif, h->il + h->w*i,h->w);
		for (i = 2; i < h->gif->SHeight; i += 4)
		    DGifGetLine(h->gif, h->il + h->w*i,h->w);
	    }
	    break;
	case TERMINATE_RECORD_TYPE:
	default:
	    goto oops;
	}
    }
    if (0 == info->width || 0 == info->height)
	goto oops;

    if (debug)
	fprintf(stderr,"gif: s=%dx%d i=%dx%d\n",
		h->gif->SWidth,h->gif->SHeight,
		h->gif->Image.Width,h->gif->Image.Height);
    return h;

 oops:
    if (debug)
	fprintf(stderr,"gif: fatal error, aborting\n");
    DGifCloseFile(h->gif, NULL);
    fclose(h->infile);
    free(h->row);
    free(h);
    return NULL;
}

static void
gif_read(unsigned char *dst, unsigned int line, void *data)
{
    struct gif_state *h = data;
    GifColorType *cmap;
    int x;
    
    if (h->gif->Image.Interlace) {
	if (line % 2) {
	    DGifGetLine(h->gif, h->row, h->w);
	} else {
	    memcpy(h->row, h->il + h->w * line, h->w);
	}
    } else {
	DGifGetLine(h->gif, h->row, h->w);
    }
    cmap = h->gif->Image.ColorMap ?
	h->gif->Image.ColorMap->Colors : h->gif->SColorMap->Colors;
    for (x = 0; x < h->w; x++) {
        dst[0] = cmap[h->row[x]].Red;
	dst[1] = cmap[h->row[x]].Green;
	dst[2] = cmap[h->row[x]].Blue;
	dst += 3;
    }
}

static void
gif_done(void *data)
{
    struct gif_state *h = data;

    if (debug)
	fprintf(stderr,"gif: done, cleaning up\n");
    DGifCloseFile(h->gif, NULL);
    fclose(h->infile);
    if (h->il)
	free(h->il);
    free(h->row);
    free(h);
}

static struct ida_loader gif_loader = {
    magic: "GIF",
    moff:  0,
    mlen:  3,
    name:  "giflib",
    init:  gif_init,
    read:  gif_read,
    done:  gif_done,
};

static void __init init_rd(void)
{
    load_register(&gif_loader);
}
