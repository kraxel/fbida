#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <tiffio.h>

#include "readers.h"
#include "writers.h"
#include "viewer.h"

/* ---------------------------------------------------------------------- */
/* save                                                                   */

static int
tiff_write(FILE *fp, struct ida_image *img)
{
    TIFF          *TiffHndl;
    tdata_t       buf;
    unsigned int  y;

    TiffHndl = TIFFFdOpen(fileno(fp),"42.tiff","w");
    if (TiffHndl == NULL)
	return -1;
    TIFFSetField(TiffHndl, TIFFTAG_IMAGEWIDTH, img->i.width);
    TIFFSetField(TiffHndl, TIFFTAG_IMAGELENGTH, img->i.height);
    TIFFSetField(TiffHndl, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(TiffHndl, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(TiffHndl, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(TiffHndl, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(TiffHndl, TIFFTAG_ROWSPERSTRIP, 2);
    TIFFSetField(TiffHndl, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
#if 0 /* fixme: make this configureable */
    TIFFSetField(TiffHndl, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    TIFFSetField(TiffHndl, TIFFTAG_PREDICTOR, 2);
#endif
    if (img->i.dpi) {
	float dpi = img->i.dpi;
	TIFFSetField(TiffHndl, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
	TIFFSetField(TiffHndl, TIFFTAG_XRESOLUTION,    dpi);
	TIFFSetField(TiffHndl, TIFFTAG_YRESOLUTION,    dpi);
    }

    for (y = 0; y < img->i.height; y++) {
	buf = img->data + 3*img->i.width*y;
	TIFFWriteScanline(TiffHndl, buf, y, 0);
    }
    TIFFClose(TiffHndl);
    return 0;
}

static struct ida_writer tiff_writer = {
    label:  "TIFF",
    ext:    { "tif", "tiff", NULL},
    write:  tiff_write,
};

static void __init init_wr(void)
{
    write_register(&tiff_writer);
}
