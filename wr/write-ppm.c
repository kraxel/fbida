#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "readers.h"
#include "writers.h"
#include "viewer.h"

/* ---------------------------------------------------------------------- */
/* save                                                                   */

static int
ppm_write(FILE *fp, struct ida_image *img)
{
    fprintf(fp,"P6\n"
	    "# written by ida " VERSION "\n"
	    "# http://bytesex.org/ida/\n"
	    "%d %d\n255\n",
            img->i.width,img->i.height);
    fwrite(img->data, img->i.height, 3*img->i.width, fp);
    return 0;
}

static struct ida_writer ppm_writer = {
    label:  "PPM",
    ext:    { "ppm", NULL},
    write:  ppm_write,
};

static void __init init_wr(void)
{
    write_register(&ppm_writer);
}
