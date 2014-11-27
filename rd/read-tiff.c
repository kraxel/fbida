#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <tiffio.h>

#include "readers.h"

struct tiff_state {
    TIFF*          tif;
    char           emsg[1024];
    tdir_t         ndirs;     /* Number of directories                     */
                              /* (could be interpreted as number of pages) */
    uint32         width,height;
    uint16         config,nsamples,depth,fillorder,photometric;
    uint32*        row;
    uint32*        image;
    uint16         resunit;
    float          xres,yres;
};

static void*
tiff_init(FILE *fp, char *filename, unsigned int page,
	  struct ida_image_info *i, int thumbnail)
{
    struct tiff_state *h;

    fclose(fp);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    TIFFSetWarningHandler(NULL);
    h->tif = TIFFOpen(filename,"r");
    if (NULL == h->tif)
	goto oops;
    /* Determine number of directories */
    h->ndirs = 1;
    while (TIFFReadDirectory(h->tif))
        h->ndirs++;
    i->npages = h->ndirs;
    /* Select requested directory (page) */
    if (!TIFFSetDirectory(h->tif, (tdir_t)page))
        goto oops;
    
    TIFFGetField(h->tif, TIFFTAG_IMAGEWIDTH,      &h->width);
    TIFFGetField(h->tif, TIFFTAG_IMAGELENGTH,     &h->height);
    TIFFGetField(h->tif, TIFFTAG_PLANARCONFIG,    &h->config);
    TIFFGetField(h->tif, TIFFTAG_SAMPLESPERPIXEL, &h->nsamples);
    TIFFGetField(h->tif, TIFFTAG_BITSPERSAMPLE,   &h->depth);
    TIFFGetField(h->tif, TIFFTAG_FILLORDER,       &h->fillorder);
    TIFFGetField(h->tif, TIFFTAG_PHOTOMETRIC,     &h->photometric);
    h->row = malloc(TIFFScanlineSize(h->tif));
    if (debug)
	fprintf(stderr,"tiff: %" PRId32 "x%" PRId32 ", planar=%d, "
		"nsamples=%d, depth=%d fo=%d pm=%d scanline=%" PRId32 "\n",
		h->width,h->height,h->config,h->nsamples,h->depth,
		h->fillorder,h->photometric,
		(uint32_t)TIFFScanlineSize(h->tif));

    if (PHOTOMETRIC_PALETTE   == h->photometric  ||
	PHOTOMETRIC_YCBCR     == h->photometric  ||
	PHOTOMETRIC_SEPARATED == h->photometric  ||
	TIFFIsTiled(h->tif)                      ||
	(1 != h->depth  &&  8 != h->depth)) {
	/* for the more difficuilt cases we let libtiff
	 * do all the hard work.  Drawback is that we lose
	 * progressive loading and decode everything here */
	if (debug)
	    fprintf(stderr,"tiff: reading whole image [TIFFReadRGBAImage]\n");
	h->image=malloc(4*h->width*h->height);
	TIFFReadRGBAImage(h->tif, h->width, h->height, h->image, 0);
    } else {
	if (debug)
	    fprintf(stderr,"tiff: reading scanline by scanline\n");
	h->row = malloc(TIFFScanlineSize(h->tif));
    }

    i->width  = h->width;
    i->height = h->height;

    if (TIFFGetField(h->tif, TIFFTAG_RESOLUTIONUNIT,  &h->resunit) &&
	TIFFGetField(h->tif, TIFFTAG_XRESOLUTION,     &h->xres)    &&
	TIFFGetField(h->tif, TIFFTAG_YRESOLUTION,     &h->yres)) {
	switch (h->resunit) {
	case RESUNIT_NONE:
	    break;
	case RESUNIT_INCH:
	    i->dpi = h->xres;
	    break;
	case RESUNIT_CENTIMETER:
	    i->dpi = res_cm_to_inch(h->xres);
	    break;
	}
    }

    return h;

 oops:
    if (h->tif)
	TIFFClose(h->tif);
    free(h);
    return NULL;
}

static void
tiff_read(unsigned char *dst, unsigned int line, void *data)
{
    struct tiff_state *h = data;
    int s,on,off;

    if (h->image) {
	/* loaded whole image using TIFFReadRGBAImage() */
	uint32 *row = h->image + h->width * (h->height - line -1);
	load_rgba(dst,(unsigned char*)row,h->width);
	return;
    }
    
    if (h->config == PLANARCONFIG_CONTIG) {
	TIFFReadScanline(h->tif, h->row, line, 0);
    } else if (h->config == PLANARCONFIG_SEPARATE) {
	for (s = 0; s < h->nsamples; s++)
	    TIFFReadScanline(h->tif, h->row, line, s);
    }

    switch (h->nsamples) {
    case 1:
	if (1 == h->depth) {
	    /* black/white */
	    on = 0, off = 0;
	    if (PHOTOMETRIC_MINISWHITE == h->photometric)
		on = 0, off = 255;
	    if (PHOTOMETRIC_MINISBLACK == h->photometric)
		on = 255, off = 0;
#if 0
	    /* Huh?  Does TIFFReadScanline handle this already ??? */
	    if (FILLORDER_MSB2LSB == h->fillorder)
		load_bits_msb(dst,(unsigned char*)(h->row),h->width,on,off);
	    else
		load_bits_lsb(dst,(unsigned char*)(h->row),h->width,on,off);
#else
	    load_bits_msb(dst,(unsigned char*)(h->row),h->width,on,off);
#endif
	} else {
	    /* grayscaled */
	    load_gray(dst,(unsigned char*)(h->row),h->width);
	}
	break;
    case 3:
	/* rgb */
	memcpy(dst,h->row,3*h->width);
	break;
    case 4:
	/* rgb+alpha */
	load_rgba(dst,(unsigned char*)(h->row),h->width);
	break;
    }
}

static void
tiff_done(void *data)
{
    struct tiff_state *h = data;

    TIFFClose(h->tif);
    if (h->row)
	free(h->row);
    if (h->image)
	free(h->image);
    free(h);
}

static struct ida_loader tiff1_loader = {
    magic: "MM\x00\x2a",
    moff:  0,
    mlen:  4,
    name:  "libtiff",
    init:  tiff_init,
    read:  tiff_read,
    done:  tiff_done,
};
static struct ida_loader tiff2_loader = {
    magic: "II\x2a\x00",
    moff:  0,
    mlen:  4,
    name:  "libtiff",
    init:  tiff_init,
    read:  tiff_read,
    done:  tiff_done,
};

static void __init init_rd(void)
{
    load_register(&tiff1_loader);
    load_register(&tiff2_loader);
}
