#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "gfx.h"
#include "drmtools.h"

/* ------------------------------------------------------------------ */

/* device */
static int fd;
static drmModeConnector *conn = NULL;
static drmModeEncoder *enc = NULL;
static drmModeModeInfo *mode = NULL;
static drmModeCrtc *scrtc = NULL;
static uint32_t fb_id;

/* dumb fb */
static struct drm_mode_create_dumb creq;
static uint8_t *fbmem;

/* ------------------------------------------------------------------ */

static void drm_cleanup_display(void)
{
    /* restore crtc */
    if (scrtc) {
        drmModeSetCrtc(fd, scrtc->crtc_id, scrtc->buffer_id, scrtc->x, scrtc->y,
                       &conn->connector_id, 1, &scrtc->mode);
    }
}

static int drm_init_dev(const char *dev)
{
    drmModeRes *res;
    int i, rc;
    uint64_t has_dumb;

    /* open device */
    fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "drm: open %s: %s\n", dev, strerror(errno));
        return -1;
    }

    rc = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
    if (rc < 0 || !has_dumb) {
        fprintf(stderr, "drm: no dumb buffer support\n");
        return -1;
    }

    /* find connector (using first for now) */
    res = drmModeGetResources(fd);
    if (res == NULL) {
        fprintf(stderr, "drm: drmModeGetResources() failed\n");
        return -1;
    }
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn &&
            (conn->connection == DRM_MODE_CONNECTED) &&
            conn->count_modes)
            break;
        drmModeFreeConnector(conn);
        conn = NULL;
    }
    if (!conn) {
        fprintf(stderr, "drm: no usable connector found\n");
        return -1;
    }
    mode = &conn->modes[0];
    enc = drmModeGetEncoder(fd, conn->encoder_id);
    if (enc == NULL) {
        fprintf(stderr, "drm: drmModeGetEncoder() failed\n");
        return -1;
    }

    /* save crtc */
    scrtc = drmModeGetCrtc(fd, enc->crtc_id);
    return 0;
}

static int drm_init_fb(void)
{
    struct drm_mode_map_dumb mreq;
    int rc;

    /* create framebuffer */
    memset(&creq, 0, sizeof(creq));
    creq.width = mode->hdisplay;
    creq.height = mode->vdisplay;
    creq.bpp = 32;
    rc = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (rc < 0) {
        fprintf(stderr, "drm: DRM_IOCTL_MODE_CREATE_DUMB: %s\n", strerror(errno));
        return -1;
    }
    rc = drmModeAddFB(fd, creq.width, creq.height, 24, 32, creq.pitch,
                      creq.handle, &fb_id);
    if (rc < 0) {
        fprintf(stderr, "drm: drmModeAddFB() failed\n");
        return -1;
    }

    /* map framebuffer */
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    rc = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (rc < 0) {
        fprintf(stderr, "drm: DRM_IOCTL_MODE_MAP_DUMB: %s\n", strerror(errno));
        return -1;
    }
    fbmem = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
    if (fbmem == MAP_FAILED) {
        fprintf(stderr, "drm: framebuffer mmap: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int drm_show_fb(void)
{
    int rc;

    rc = drmModeSetCrtc(fd, enc->crtc_id, fb_id, 0, 0,
                        &conn->connector_id, 1,
                        &conn->modes[0]);
    if (rc < 0) {
        fprintf(stderr, "drm: drmModeSetCrtc() failed\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static void drm_restore_display(void)
{
    drm_show_fb();
}

gfxstate *drm_init(const char *device)
{
    gfxstate *gfx;
    char dev[64];

    if (device) {
        snprintf(dev, sizeof(dev), "%s", device);
    } else {
        snprintf(dev, sizeof(dev), DRM_DEV_NAME, DRM_DIR_NAME, 0);
    }
    fprintf(stderr, "trying drm: %s ...\n", dev);

    if (drm_init_dev(dev) < 0)
        return NULL;
    if (drm_init_fb() < 0)
        return NULL;
    if (drm_show_fb() < 0)
        return NULL;

    /* prepare gfx */
    gfx = malloc(sizeof(*gfx));
    memset(gfx, 0, sizeof(*gfx));

    gfx->hdisplay        = mode->hdisplay;
    gfx->vdisplay        = mode->vdisplay;
    gfx->stride          = creq.pitch;
    gfx->mem             = fbmem;

    gfx->rlen            =  8;
    gfx->glen            =  8;
    gfx->blen            =  8;
    gfx->tlen            =  8;
    gfx->roff            = 16;
    gfx->goff            =  8;
    gfx->boff            =  0;
    gfx->toff            = 24;
    gfx->bits_per_pixel  = 32;

    gfx->restore_display = drm_restore_display;
    gfx->cleanup_display = drm_cleanup_display;
    return gfx;
}
