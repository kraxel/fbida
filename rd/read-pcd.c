#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pcd.h"

#include "readers.h"

extern int pcd_res;

/* ---------------------------------------------------------------------- */
/* load                                                                   */

struct pcd_state {
    struct PCD_IMAGE img;
    int    left,top,width,height;
};

static void*
pcd_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *i, int thumbnail)
{
    struct pcd_state *h;
    
    fclose(fp);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    if (0 != pcd_open(&h->img, filename))
	goto oops;
    if (-1 == pcd_select(&h->img, thumbnail ? 1 : pcd_res,
			 0,0,0, pcd_get_rot(&h->img, 0),
                         &h->left, &h->top, &h->width, &h->height))
	goto oops;
    if (-1 == pcd_decode(&h->img))
	goto oops;
    
    i->width  = h->width;
    i->height = h->height;
    i->npages = 1;
    return h;

 oops:
    free(h);
    return NULL;
}

static void
pcd_read(unsigned char *dst, unsigned int line, void *data)
{
    struct pcd_state *h = data;

    pcd_get_image_line(&h->img, line, dst, PCD_TYPE_RGB, 0);
}

static void
pcd_done(void *data)
{
    struct pcd_state *h = data;

    pcd_close(&h->img);
    free(h);
}

static struct ida_loader pcd_loader = {
    magic: "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
    moff:  0,
    mlen:  16,
    name:  "libpcd",
    init:  pcd_init,
    read:  pcd_read,
    done:  pcd_done,
};

static void __init init_rd(void)
{
    load_register(&pcd_loader);
}
