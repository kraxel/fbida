#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "readers.h"

/* ---------------------------------------------------------------------- */
/* load                                                                   */

struct ppm_state {
    FILE          *infile;
    int           width,height;
    unsigned char *row;
};

static void*
pnm_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *i, int thumbnail)
{
    struct ppm_state *h;
    char line[1024];
    char p;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->infile = fp;
    fgets(line,sizeof(line),fp); /* P[456] */
    p = line[1];
    fgets(line,sizeof(line),fp); /* width height */
    while ('#' == line[0])
	fgets(line,sizeof(line),fp); /* skip comments */
    sscanf(line,"%d %d",&h->width,&h->height);
    if (p != '4')
	fgets(line,sizeof(line),fp); /* depth ??? */
    if (0 == h->width || 0 == h->height)
	goto oops;
    i->width  = h->width;
    i->height = h->height;
    i->npages = 1;
    h->row = malloc(h->width*3);

    return h;

 oops:
    fclose(fp);
    free(h);
    return NULL;
}

static void
ppm_read(unsigned char *dst, unsigned int line, void *data)
{
    struct ppm_state *h = data;

    fread(dst,h->width,3,h->infile);
}

static void
pgm_read(unsigned char *dst, unsigned int line, void *data)
{
    struct ppm_state *h = data;
    unsigned char *src;
    int x;

    fread(h->row,h->width,1,h->infile);
    src = h->row;
    for (x = 0; x < h->width; x++) {
	dst[0] = src[0];
	dst[1] = src[0];
	dst[2] = src[0];
	dst += 3;
	src += 1;
    }
}

static void
pbm_read(unsigned char *dst, unsigned int line, void *data)
{
    struct ppm_state *h = data;
    int bpl;

    bpl = ((h->width+7) >> 3);
    fread(h->row,bpl,1,h->infile);
    load_bits_msb(dst,(unsigned char*)(h->row),h->width,0,255);
}

static void
pnm_done(void *data)
{
    struct ppm_state *h = data;

    fclose(h->infile);
    free(h->row);
    free(h);
}

struct ida_loader ppm_loader = {
    magic: "P6",
    moff:  0,
    mlen:  2,
    name:  "ppm parser",
    init:  pnm_init,
    read:  ppm_read,
    done:  pnm_done,
};

static struct ida_loader pgm_loader = {
    magic: "P5",
    moff:  0,
    mlen:  2,
    name:  "pgm parser",
    init:  pnm_init,
    read:  pgm_read,
    done:  pnm_done,
};

static struct ida_loader pbm_loader = {
    magic: "P4",
    moff:  0,
    mlen:  2,
    name:  "pbm parser",
    init:  pnm_init,
    read:  pbm_read,
    done:  pnm_done,
};

static void __init init_rd(void)
{
    load_register(&ppm_loader);
    load_register(&pgm_loader);
    load_register(&pbm_loader);
}

