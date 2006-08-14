#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/XWDFile.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#endif

#include "readers.h"
#include "viewer.h"
#include "xwd.h"
#include "x11.h"
#include "ida.h"

/* xwd files are big endian */
#if BYTE_ORDER == BIG_ENDIAN
# define be16_to_cpu(x) (x)
# define be32_to_cpu(x) (x)
#elif BYTE_ORDER == LITTLE_ENDIAN
# define be16_to_cpu(x) ((((uint16_t)x>>8)  &     0x00ff) |\
                         (((uint16_t)x<<8)  &     0xff00))
# define be32_to_cpu(x) ((((uint32_t)x>>24) & 0x000000ff) |\
                         (((uint32_t)x>>8)  & 0x0000ff00) |\
                         (((uint32_t)x<<8)  & 0x00ff0000) |\
                         (((uint32_t)x<<24) & 0xff000000))
#else
# error "Oops: unknown byte order"
#endif

static char *vclass[] = {
    "StaticGray",
    "GrayScale",
    "StaticColor",
    "PseudoColor",
    "TrueColor",
    "DirectColor",
};
static char *order[] = {
    "LSBFirst",
    "MSBFirst",
};
static char *fmt[] = {
    "XYBitmap",
    "XYPixmap",
    "ZPixmap",
};

/* ----------------------------------------------------------------------- */

struct xwd_state {
    FILE          *infile;
    XWDFileHeader header;
    XWDColor      cmap[256];
    int           width,bpp;
    unsigned char *row;
    unsigned long *pix;
    unsigned long r_mask,g_mask,b_mask;
    int           r_shift,g_shift,b_shift;
    int           r_bits,g_bits,b_bits;
};

static void
xwd_map(struct xwd_state *h)
{
    int             i;
    unsigned long   mask;

    h->r_mask = be32_to_cpu(h->header.red_mask);
    h->g_mask = be32_to_cpu(h->header.green_mask);
    h->b_mask = be32_to_cpu(h->header.blue_mask);
    for (i = 0; i < 32; i++) {
	mask = (1 << i);
	if (h->r_mask & mask)
	    h->r_bits++;
	else if (!h->r_bits)
	    h->r_shift++;
	if (h->g_mask & mask)
	    h->g_bits++;
	else if (!h->g_bits)
	    h->g_shift++;
	if (h->b_mask & mask)
	    h->b_bits++;
	else if (!h->b_bits)
	    h->b_shift++;
    }
    h->r_shift -= (8 - h->r_bits);
    h->g_shift -= (8 - h->g_bits);
    h->b_shift -= (8 - h->b_bits);
#if 0
    fprintf(stderr,"xwd: color: bits shift\n");
    fprintf(stderr,"xwd: red  : %04i %05i\n", h->r_bits, h->r_shift);
    fprintf(stderr,"xwd: green: %04i %05i\n", h->g_bits, h->g_shift);
    fprintf(stderr,"xwd: blue : %04i %05i\n", h->b_bits, h->b_shift);
#endif
}


static void*
xwd_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *i, int thumbnail)
{
    struct xwd_state *h;
    char *buf;
    int size;
    
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->infile = fp;

    fread(&h->header,sizeof(h->header),1,fp);

    if ((be32_to_cpu(h->header.pixmap_format) >sizeof(fmt)/sizeof(char*))   ||
	(be32_to_cpu(h->header.byte_order)    >sizeof(order)/sizeof(char*)) ||
	(be32_to_cpu(h->header.visual_class)  >sizeof(vclass)/sizeof(char*))) {
	fprintf(stderr,"xwd: invalid file\n");
	goto oops;
    }

    if (debug)
	fprintf(stderr,
		"xwd: fmt=%s depth=%" PRId32 " size=%" PRId32 "x%" PRId32
		" bpp=%" PRId32 " bpl=%" PRId32 "\n"
		"xwd: order=%s vclass=%s masks=%" PRIx32 "/%" PRIx32
		"/%" PRIx32 " cmap=%" PRId32 "\n",
		fmt[be32_to_cpu(h->header.pixmap_format)],
		be32_to_cpu(h->header.pixmap_depth),
		be32_to_cpu(h->header.pixmap_width),
		be32_to_cpu(h->header.pixmap_height),
		be32_to_cpu(h->header.bits_per_pixel),
		be32_to_cpu(h->header.bytes_per_line),
		order[be32_to_cpu(h->header.byte_order)],
		vclass[be32_to_cpu(h->header.visual_class)],
		be32_to_cpu(h->header.red_mask),
		be32_to_cpu(h->header.green_mask),
		be32_to_cpu(h->header.blue_mask),
		be32_to_cpu(h->header.colormap_entries));

    size = be32_to_cpu(h->header.header_size)-sizeof(h->header);
    buf = malloc(size);
    fread(buf,size,1,fp);
    if (debug)
	fprintf(stderr,"xwd: name=%s\n",buf);
    free(buf);

    /* check format */
    if (8  != be32_to_cpu(h->header.bits_per_pixel) &&
	16 != be32_to_cpu(h->header.bits_per_pixel) &&
	24 != be32_to_cpu(h->header.bits_per_pixel) &&
	32 != be32_to_cpu(h->header.bits_per_pixel)) {
	fprintf(stderr,"xwd: Oops: bpp != 8/16/24/32\n");
	goto oops;
    }
    if (be32_to_cpu(h->header.pixmap_format) != ZPixmap) {
	fprintf(stderr,"xwd: Oops: can read only ZPixmap format\n");
	goto oops;
    }

    /* color map */
    if (be32_to_cpu(h->header.colormap_entries) > 256) {
	fprintf(stderr,"xwd: colormap too big (%" PRId32 " > 256)\n",
		be32_to_cpu(h->header.colormap_entries));
	goto oops;
    }
    fread(&h->cmap,sizeof(XWDColor),be32_to_cpu(h->header.colormap_entries),fp);
#if 0
    for (i = 0; i < be32_to_cpu(h->header.colormap_entries); i++)
	fprintf(stderr, "xwd cmap: %d: "
		"pix=%ld rgb=%d/%d/%d flags=%d pad=%d\n",i,
		be32_to_cpu(h->cmap[i].pixel),
		be16_to_cpu(h->cmap[i].red),
		be16_to_cpu(h->cmap[i].green),
		be16_to_cpu(h->cmap[i].blue),
		h->cmap[i].flags,
		h->cmap[i].pad);
#endif

    switch (be32_to_cpu(h->header.visual_class)) {
    case StaticGray:
    case PseudoColor:
	/* nothing */
	break;
    case TrueColor:
    case DirectColor:
	xwd_map(h);
	break;
    default:
	fprintf(stderr,"xwd: Oops: visual not implemented [%s]\n",
		vclass[be32_to_cpu(h->header.visual_class)]);
	goto oops;
    }

    h->bpp    = be32_to_cpu(h->header.bits_per_pixel);
    h->width  = be32_to_cpu(h->header.pixmap_width);
    h->pix    = malloc(h->width*sizeof(unsigned long));
    h->row    = malloc(be32_to_cpu(h->header.bytes_per_line));
    i->width  = be32_to_cpu(h->header.pixmap_width);
    i->height = be32_to_cpu(h->header.pixmap_height);
    i->npages = 1;
    return h;
	  
 oops:
    fclose(h->infile);
    free(h);
    return NULL;
}

static void
xwd_parse(unsigned char *dst, unsigned int line, void *data)
{
    struct xwd_state *h = data;
    unsigned long r,g,b;
    int x,i,bits;
    
    /* data to 32bit values */
    memset(h->pix,0,h->width * sizeof(unsigned long));
    if (be32_to_cpu(h->header.byte_order) == LSBFirst) {
	for (i = 0, x = 0; x < h->width; x++)
	    for (bits = 0; bits < h->bpp; bits += 8)
		h->pix[x] |= h->row[i++] << bits;
    } else {
	for (i = 0, x = 0; x < h->width; x++)
	    for (bits = 0; bits < h->bpp; bits += 8)
		h->pix[x] <<= 8, h->pix[x] |= h->row[i++];
    }

    /* transform to rgb */
    switch (be32_to_cpu(h->header.visual_class)) {
    case StaticGray:
	for (x = 0; x < h->width; x++) {
	    dst[0] = h->pix[x];
	    dst[1] = h->pix[x];
	    dst[2] = h->pix[x];
	    dst += 3;
	}
	break;
    case PseudoColor:
	for (x = 0; x < h->width; x++) {
	    dst[0] = be16_to_cpu(h->cmap[h->pix[x]].red)   >> 8;
	    dst[1] = be16_to_cpu(h->cmap[h->pix[x]].green) >> 8;
	    dst[2] = be16_to_cpu(h->cmap[h->pix[x]].blue)  >> 8;
	    dst += 3;
	}
	break;
    case TrueColor:
    case DirectColor:
	for (x = 0; x < h->width; x++) {
	    r = h->pix[x] & h->r_mask;
	    if (h->r_shift > 0)
		r >>= h->r_shift;
	    if (h->r_shift < 0)
		r <<= -h->r_shift;
	    g = h->pix[x] & h->g_mask;
	    if (h->g_shift > 0)
		g >>= h->g_shift;
	    if (h->g_shift < 0)
		g <<= -h->g_shift;
	    b = h->pix[x] & h->b_mask;
	    if (h->b_shift > 0)
		b >>= h->b_shift;
	    if (h->b_shift < 0)
		b <<= -h->b_shift;
	    dst[0] = r;
	    dst[1] = g;
	    dst[2] = b;
	    dst += 3;
	}
	break;
    }
}

static void
xwd_read(unsigned char *dst, unsigned int line, void *data)
{
    struct xwd_state *h = data;

    fread(h->row,be32_to_cpu(h->header.bytes_per_line),1,h->infile);
    xwd_parse(dst, line, data);
}

static void
xwd_done(void *data)
{
    struct xwd_state *h = data;

    fclose(h->infile);
    free(h->pix);
    free(h->row);
    free(h);
}

static struct ida_loader xwd_loader = {
    magic: "\0\0\0\7",
    moff:  4,
    mlen:  4,
    name:  "xwd",
    init:  xwd_init,
    read:  xwd_read,
    done:  xwd_done,
};

static void __init init_rd(void)
{
    load_register(&xwd_loader);
}

/* ----------------------------------------------------------------------- */

void
parse_ximage(struct ida_image *dest, XImage *src)
{
    struct xwd_state h;
    Colormap cmap;
    XColor col;
    int y,i;

    memset(&h,0,sizeof(h));
    h.width               = src->width;
    h.bpp                 = src->bits_per_pixel;
    h.header.red_mask     = be32_to_cpu(info->red_mask);
    h.header.green_mask   = be32_to_cpu(info->green_mask);
    h.header.blue_mask    = be32_to_cpu(info->blue_mask);
    h.header.visual_class = be32_to_cpu(info->class);
    h.header.byte_order   = be32_to_cpu(ImageByteOrder(dpy));
    h.pix = malloc(src->width * sizeof(unsigned long));

    switch (be32_to_cpu(h.header.visual_class)) {
    case PseudoColor:
	cmap = DefaultColormapOfScreen(XtScreen(app_shell));
	for (i = 0; i < 256; i++) {
	    col.pixel = i;
	    XQueryColor(dpy,cmap,&col);
	    h.cmap[i].red   = be16_to_cpu(col.red);
	    h.cmap[i].green = be16_to_cpu(col.green);
	    h.cmap[i].blue  = be16_to_cpu(col.blue);
	}
	break;
    case TrueColor:
    case DirectColor:
	xwd_map(&h);
	break;
    }

    memset(dest,0,sizeof(*dest));
    dest->i.width  = src->width;
    dest->i.height = src->height;
    dest->data     = malloc(dest->i.width * dest->i.height * 3);
    memset(dest->data,0,dest->i.width * dest->i.height * 3);
    
    for (y = 0; y < src->height; y++) {
	h.row = src->data + y*src->bytes_per_line;
	xwd_parse(dest->data + 3*y*dest->i.width, y, &h);
    }
    free(h.pix);
}
