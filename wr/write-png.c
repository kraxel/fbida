#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <png.h>

#include "readers.h"
#include "writers.h"
#include "viewer.h"

/* ---------------------------------------------------------------------- */
/* save                                                                   */

static int
png_write(FILE *fp, struct ida_image *img)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr  = NULL;
    png_bytep row;
    unsigned int y;
    
   /* Create and initialize the png_struct with the desired error handler
    * functions.  If you want to use the default stderr and longjump method,
    * you can supply NULL for the last three parameters.  We also check that
    * the library version is compatible with the one used at compile time,
    * in case we are using dynamically linked libraries.  REQUIRED.
    */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if (png_ptr == NULL)
	goto oops;

   /* Allocate/initialize the image information data.  REQUIRED */
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL)
       goto oops;
   if (setjmp(png_jmpbuf(png_ptr)))
       goto oops;

   png_init_io(png_ptr, fp);
   png_set_IHDR(png_ptr, info_ptr, img->i.width, img->i.height, 8,
		PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
   if (img->i.dpi) {
       png_set_pHYs(png_ptr, info_ptr,
		    res_inch_to_m(img->i.dpi),
		    res_inch_to_m(img->i.dpi),
		    PNG_RESOLUTION_METER);
   }
   png_write_info(png_ptr, info_ptr);
   png_set_packing(png_ptr);

   for (y = 0; y < img->i.height; y++) {
       row = img->data + y * 3 * img->i.width;
       png_write_rows(png_ptr, &row, 1);
   }
   png_write_end(png_ptr, info_ptr);
   png_destroy_write_struct(&png_ptr, &info_ptr);
   return 0;

 oops:
   fprintf(stderr,"can't save image: libpng error\n");
   if (png_ptr)
       png_destroy_write_struct(&png_ptr,  (png_infopp)NULL);
   return -1;
}

static struct ida_writer png_writer = {
    label:  "PNG",
    ext:    { "png", NULL},
    write:  png_write,
};

static void __init init_wr(void)
{
    write_register(&png_writer);
}
