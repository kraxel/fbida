#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#include "ida.h"

#include "readers.h"
#include "viewer.h"

/* ---------------------------------------------------------------------- */
/* load                                                                   */

struct xpm_color {
    char    name[8];
    XColor  color;
};

struct xpm_state {
    FILE              *infile;
    int               width,height,colors,chars;
    struct xpm_color  *cmap;
    char              *charline;
    char              *rgbrow;
};

static void*
xpm_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *info, int thumbnail)
{
    struct xpm_state *h;
    char line[1024],cname[32],*tmp;
    XColor dummy;
    int i;
    Colormap cmap = DefaultColormapOfScreen(XtScreen(app_shell));

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->infile = fp;

    fgets(line,sizeof(line)-1,fp);  /* XPM */
    fgets(line,sizeof(line)-1,fp);  /* static char ... */
    while (0 == strncmp(line,"/*",2)) {
	while (NULL == strstr(line,"*/"))
	    fgets(line,sizeof(line)-1,fp);
	fgets(line,sizeof(line)-1,fp);
    }

    /* size, colors */
    fgets(line,sizeof(line)-1,fp);
    if (0 == strncmp(line,"/*",2)) {
	while (NULL == strstr(line,"*/"))
	    fgets(line,sizeof(line)-1,fp);
	fgets(line,sizeof(line)-1,fp);
    }
    if (4 != sscanf(line,"\"%d %d %d %d",
		    &h->width,&h->height,&h->colors,&h->chars))
	goto oops;
    if (h->chars > 7)
	goto oops;

    /* read color table */
    h->cmap = malloc(h->colors * sizeof(struct xpm_color));
    memset(h->cmap,0,h->colors * sizeof(struct xpm_color));
    for (i = 0; i < h->colors; i++) {
	fgets(line,sizeof(line)-1,fp);
	while (0 == strncmp(line,"/*",2)) {
	    while (NULL == strstr(line,"*/"))
		fgets(line,sizeof(line)-1,fp);
	    fgets(line,sizeof(line)-1,fp);
	}
	memcpy(h->cmap[i].name,line+1,h->chars);

	if (NULL != (tmp = strstr(line+1+h->chars,"c "))) {
	    /* color */
	    sscanf(tmp+2,"%32[^\" ]",cname);
	} else if (NULL != (tmp = strstr(line+h->chars,"m "))) {
	    /* mono */
	    sscanf(tmp+2,"%32[^\" ]",cname);
	} else if (NULL != (tmp = strstr(line+h->chars,"g "))) {
	    /* gray? */
	    sscanf(tmp+2,"%32[^\" ]",cname);
	} else
	    goto oops;
	if (0 == strcasecmp(cname,"none"))
	    /* transparent */
	    strcpy(cname,"lightgray");
	if (debug)
	    fprintf(stderr,"xpm: cmap: \"%*.*s\" => %s\n",
		    h->chars,h->chars,h->cmap[i].name,cname);
#if 0
	if (1 != sscanf(line+1+h->chars," c %32[^\"]",cname))
	    goto oops;
#endif
	XLookupColor(dpy,cmap,cname,&h->cmap[i].color,&dummy);
    }
    h->charline = malloc(h->width * h->chars + 8);
    h->rgbrow   = malloc(h->width * 3);

    info->width  = h->width;
    info->height = h->height;
    info->npages = 1;
    return h;

 oops:
    fclose(fp);
    free(h);
    return NULL;
}

static void
xpm_read(unsigned char *dst, unsigned int line, void *data)
{
    struct xpm_state *h = data;
    char *src;
    int i,c;

    fgets(h->charline,h->width * h->chars + 8,h->infile);
    while (0 == strncmp(h->charline,"/*",2)) {
	while (NULL == strstr(h->charline,"*/"))
	    fgets(h->charline,h->width * h->chars + 8,h->infile);
	fgets(h->charline,h->width * h->chars + 8,h->infile);
    }
    src = h->charline+1;
    for (i = 0; i < h->width; i++) {
	for (c = 0; c < h->colors; c++) {
	    char *name = h->cmap[c].name;
	    if (src[0] != name[0])
		continue;
	    if (1 == h->chars)
		break;
	    if (src[1] != name[1])
		continue;
	    if (2 == h->chars)
		break;
	    if (0 == strncmp(src+2,name+2,h->chars-2))
		break;
	}
	if (c == h->colors)
	    continue;
	dst[0] = h->cmap[c].color.red   >> 8;
	dst[1] = h->cmap[c].color.green >> 8;
	dst[2] = h->cmap[c].color.blue  >> 8;
	src += h->chars;
	dst += 3;
    }
}

static void
xpm_done(void *data)
{
    struct xpm_state *h = data;
    
    fclose(h->infile);
    free(h->charline);
    free(h->rgbrow);
    free(h->cmap);
    free(h);
}

/* ---------------------------------------------------------------------- */

struct xbm_state {
    FILE              *infile;
    int               width,height;
};

static void*
xbm_init(FILE *fp, char *filename, unsigned int page,
	 struct ida_image_info *info, int thumbnail)
{
    struct xbm_state *h;
    char line[256],dummy[128];
    int i;

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->infile = fp;

    for (i = 0; i < 128; i++) {
	fgets(line,sizeof(line)-1,fp);
	if (0 == strncmp(line,"#define",7))
	    break;
    }
    if (128 == i)
	goto oops;

    if (2 != sscanf(line,"#define %127s %d",dummy,&h->width))
	goto oops;
    fgets(line,sizeof(line)-1,fp);
    if (2 != sscanf(line,"#define %127s %d",dummy,&h->height))
	goto oops;
    if (debug)
	fprintf(stderr,"xbm: %dx%d\n",h->width,h->height);

    for (i = 0; i < 4; i++) {
	fgets(line,sizeof(line)-1,fp);
	if (strstr(line,"[] = {"))
	    break;
    }
    if (4 == i)
	goto oops;

    info->width  = h->width;
    info->height = h->height;
    info->npages = 1;
    return h;

 oops:
    if (debug)
	fprintf(stderr,"xbm: %s",line);
    fclose(fp);
    free(h);
    return NULL;
}

static void
xbm_read(unsigned char *dst, unsigned int line, void *data)
{
    struct xbm_state *h = data;
    int x,val;

    for (x = 0; x < h->width; x++) {
	if (0 == (x % 8))
	    fscanf(h->infile," 0x%x,",&val);
	if (val & (1 << (x % 8))) {
	    *(dst++) = 0;
	    *(dst++) = 0;
	    *(dst++) = 0;
	} else {
	    *(dst++) = 255;
	    *(dst++) = 255;
	    *(dst++) = 255;
	}
    }
}

static void
xbm_done(void *data)
{
    struct xpm_state *h = data;
    
    fclose(h->infile);
    free(h);
}

/* ---------------------------------------------------------------------- */

static struct ida_loader xpm_loader = {
    magic: "/* XPM */",
    moff:  0,
    mlen:  9,
    name:  "xpm parser",
    init:  xpm_init,
    read:  xpm_read,
    done:  xpm_done,
};

static struct ida_loader xbm1_loader = {
    magic: "#define",
    moff:  0,
    mlen:  7,
    name:  "xbm parser",
    init:  xbm_init,
    read:  xbm_read,
    done:  xbm_done,
};

static struct ida_loader xbm2_loader = {
    magic: "/*",
    moff:  0,
    mlen:  2,
    name:  "xbm parser",
    init:  xbm_init,
    read:  xbm_read,
    done:  xbm_done,
};

static void __init init_rd(void)
{
    load_register(&xpm_loader);
    load_register(&xbm1_loader);
    load_register(&xbm2_loader);
}
