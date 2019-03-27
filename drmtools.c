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
#include <sys/stat.h>

#include "gfx.h"
#include "drmtools.h"

/* ------------------------------------------------------------------ */

/* device */

int drm_fd;
drmModeEncoder *drm_enc = NULL;
drmModeModeInfo *drm_mode = NULL;
drmModeConnector *drm_conn = NULL;

static drmModeCrtc *scrtc = NULL;
static gfxfmt *drm_fmt = NULL;
static char drm_dev[64];

struct drmfb {
    uint32_t id;
    struct drm_mode_create_dumb creq;
    uint8_t *mem;
} fb1, fb2, *fbc;

/* ------------------------------------------------------------------ */

static const char *conn_type[] = {
    [ DRM_MODE_CONNECTOR_Unknown      ] = "unknown",
    [ DRM_MODE_CONNECTOR_VGA          ] = "vga",
    [ DRM_MODE_CONNECTOR_DVII         ] = "dvi-i",
    [ DRM_MODE_CONNECTOR_DVID         ] = "dvi-d",
    [ DRM_MODE_CONNECTOR_DVIA         ] = "dvi-a",
    [ DRM_MODE_CONNECTOR_Composite    ] = "composite",
    [ DRM_MODE_CONNECTOR_SVIDEO       ] = "svideo",
    [ DRM_MODE_CONNECTOR_LVDS         ] = "lvds",
    [ DRM_MODE_CONNECTOR_Component    ] = "component",
    [ DRM_MODE_CONNECTOR_9PinDIN      ] = "9pin-din",
    [ DRM_MODE_CONNECTOR_DisplayPort  ] = "dp",
    [ DRM_MODE_CONNECTOR_HDMIA        ] = "hdmi-a",
    [ DRM_MODE_CONNECTOR_HDMIB        ] = "hdmi-b",
    [ DRM_MODE_CONNECTOR_TV           ] = "tv",
    [ DRM_MODE_CONNECTOR_eDP          ] = "edp",
    [ DRM_MODE_CONNECTOR_VIRTUAL      ] = "virtual",
    [ DRM_MODE_CONNECTOR_DSI          ] = "dsi",
};

static void drm_conn_name(drmModeConnector *conn, char *dest, int dlen)
{
    const char *type;

    if (conn->connector_type_id < sizeof(conn_type)/sizeof(conn_type[0]) &&
        conn_type[conn->connector_type]) {
        type = conn_type[conn->connector_type];
    } else {
        type = "unknown";
    }
    snprintf(dest, dlen, "%s-%d", type, conn->connector_type_id);
}

/* ------------------------------------------------------------------ */

void drm_cleanup_display(void)
{
    /* restore crtc */
    if (scrtc) {
        drmModeSetCrtc(drm_fd, scrtc->crtc_id, scrtc->buffer_id, scrtc->x, scrtc->y,
                       &drm_conn->connector_id, 1, &scrtc->mode);
    }
}

int drm_init_dev(const char *dev, const char *output, const char *mode)
{
    drmModeRes *res;
    char name[64], m[64];
    uint64_t has_dumb;
    int i, rc;

    /* open device */
    drm_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        fprintf(stderr, "drm: open %s: %s\n", dev, strerror(errno));
        return -1;
    }

    rc = drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
    if (rc < 0 || !has_dumb) {
        fprintf(stderr, "drm: no dumb buffer support\n");
        return -1;
    }

    /* find connector (using first for now) */
    res = drmModeGetResources(drm_fd);
    if (res == NULL) {
        fprintf(stderr, "drm: drmModeGetResources() failed\n");
        return -1;
    }
    for (i = 0; i < res->count_connectors; i++) {
        drm_conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (drm_conn &&
            (drm_conn->connection == DRM_MODE_CONNECTED) &&
            drm_conn->count_modes) {
            if (output) {
                drm_conn_name(drm_conn, name, sizeof(name));
                if (strcmp(name, output) == 0) {
                    break;
                }
            } else {
                break;
            }
        }
        drmModeFreeConnector(drm_conn);
        drm_conn = NULL;
    }
    if (!drm_conn) {
        if (output) {
            fprintf(stderr, "drm: output %s not found or disconnected\n",
                    output);
        } else {
            fprintf(stderr, "drm: no usable output found\n");
        }
        return -1;
    }

    if (mode) {
        for (i = 0; i < drm_conn->count_modes; i++) {
            snprintf(m, sizeof(m), "%dx%d",
                     drm_conn->modes[i].hdisplay,
                     drm_conn->modes[i].vdisplay);
            if (strcmp(m, mode) == 0) {
                fprintf(stderr, "Using mode %s\n", mode);
                drm_mode = &drm_conn->modes[i];
                break;
            }
        }
    }
    if (!drm_mode) {
        drm_mode = &drm_conn->modes[0];
    }

    drm_enc = drmModeGetEncoder(drm_fd, drm_conn->encoder_id);
    if (drm_enc == NULL) {
        fprintf(stderr, "drm: drmModeGetEncoder() failed\n");
        return -1;
    }

    /* save crtc */
    scrtc = drmModeGetCrtc(drm_fd, drm_enc->crtc_id);
    return 0;
}

static int drm_init_fb(struct drmfb *fb, struct gfxfmt *fmt, bool logerrors)
{
    struct drm_mode_map_dumb mreq;
    int rc;

    /* create framebuffer */
    memset(&fb->creq, 0, sizeof(fb->creq));
    fb->creq.width = drm_mode->hdisplay;
    fb->creq.height = drm_mode->vdisplay;
    fb->creq.bpp = fmt->bpp;
    rc = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &fb->creq);
    if (rc < 0) {
        if (logerrors)
            fprintf(stderr, "drm: DRM_IOCTL_MODE_CREATE_DUMB: %s\n", strerror(errno));
        return -1;
    }
    rc = drmModeAddFB(drm_fd, fb->creq.width, fb->creq.height,
                      fmt->depth, fmt->bpp, fb->creq.pitch,
                      fb->creq.handle, &fb->id);
    if (rc < 0) {
        if (logerrors)
            fprintf(stderr, "drm: drmModeAddFB(%c%c%c%c) failed\n",
                    (fmt->fourcc >>  0) & 0xff,
                    (fmt->fourcc >>  8) & 0xff,
                    (fmt->fourcc >> 16) & 0xff,
                    (fmt->fourcc >> 24) & 0xff);
        return -1;
    }
    if (logerrors)
        fprintf(stderr, "drm: drmModeAddFB(%c%c%c%c) OK\n",
                (fmt->fourcc >>  0) & 0xff,
                (fmt->fourcc >>  8) & 0xff,
                (fmt->fourcc >> 16) & 0xff,
                (fmt->fourcc >> 24) & 0xff);

    /* map framebuffer */
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = fb->creq.handle;
    rc = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (rc < 0) {
        if (logerrors)
            fprintf(stderr, "drm: DRM_IOCTL_MODE_MAP_DUMB: %s\n", strerror(errno));
        return -1;
    }
    fb->mem = mmap(fb->mem, fb->creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   drm_fd, mreq.offset);
    if (fb->mem == MAP_FAILED) {
        if (logerrors)
            fprintf(stderr, "drm: framebuffer mmap: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int drm_fini_fb(struct drmfb *fb)
{
    munmap(fb->mem, fb->creq.size);
    return 0;
}

static int drm_show_fb(struct drmfb *fb)
{
    int rc;

    fprintf(stderr, "%s: fb %d\n", __func__, fb->id);
    rc = drmModeSetCrtc(drm_fd, drm_enc->crtc_id, fb->id, 0, 0,
                        &drm_conn->connector_id, 1,
                        drm_mode);
    if (rc < 0) {
        fprintf(stderr, "drm: drmModeSetCrtc() failed\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static void drm_suspend_display(void)
{
    if (fb2.mem)
        drm_fini_fb(&fb2);
    drm_fini_fb(&fb1);
    close(drm_fd);
    drm_fd = -1;
}

static void drm_resume_display(void)
{
    drm_fd = open(drm_dev, O_RDWR | O_CLOEXEC);
    drm_init_fb(&fb1, drm_fmt, false);
    if (fb2.mem)
        drm_init_fb(&fb2, drm_fmt, false);
}

static void drm_flush_display(bool second)
{
    fbc = second ? &fb2 : &fb1;
    drm_show_fb(fbc);
    drmModeDirtyFB(drm_fd, fbc->id, 0, 0);
}

gfxstate *drm_init(const char *device, const char *output,
                   const char *mode, bool pageflip)
{
    struct stat st;
    gfxstate *gfx;
    int i;

    if (device) {
        snprintf(drm_dev, sizeof(drm_dev), "%s", device);
    } else {
        snprintf(drm_dev, sizeof(drm_dev), DRM_DEV_NAME, DRM_DIR_NAME, 0);
    }
    fprintf(stderr, "trying drm: %s ...\n", drm_dev);

    if (drm_init_dev(drm_dev, output, mode) < 0)
        return NULL;
    for (i = 0; i < fmt_count; i++) {
        if (drm_init_fb(&fb1, fmt_list + i, true) < 0)
            continue;
        drm_fmt = fmt_list + i;
        break;
    }
    if (drm_fmt == NULL)
        return NULL;
    if (drm_show_fb(&fb1) < 0)
        return NULL;

    /* prepare gfx */
    gfx = malloc(sizeof(*gfx));
    memset(gfx, 0, sizeof(*gfx));

    gfx->hdisplay        = drm_mode->hdisplay;
    gfx->vdisplay        = drm_mode->vdisplay;
    gfx->stride          = fb1.creq.pitch;
    gfx->mem             = fb1.mem;
    gfx->fmt             = drm_fmt;

    gfx->suspend_display = drm_suspend_display;
    gfx->resume_display  = drm_resume_display;
    gfx->cleanup_display = drm_cleanup_display;
    gfx->flush_display   = drm_flush_display;

    fstat(drm_fd, &st);
    gfx->devnum = st.st_rdev;
    snprintf(gfx->devpath, sizeof(gfx->devpath), "%s", drm_dev);

    if (pageflip) {
        if (drm_init_fb(&fb2, drm_fmt, false) == 0) {
            gfx->mem2 = fb2.mem;
        } else {
            fprintf(stderr, "drm: can't alloc two fbs, pageflip disabled.\n");
        }
    }
    return gfx;
}

/* ------------------------------------------------------------------ */

void drm_info(const char *device)
{
    drmModeConnector *conn;
    drmModeEncoder *enc;
    drmModeCrtc *crtc;
    drmModeRes *res;
    char name[64];
    char dev[64];
    int i;

    if (device) {
        snprintf(dev, sizeof(dev), "%s", device);
    } else {
        snprintf(dev, sizeof(dev), DRM_DEV_NAME, DRM_DIR_NAME, 0);
    }
    drm_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        fprintf(stderr, "drm: open %s: %s\n", dev, strerror(errno));
        return;
    }
    fprintf(stdout, "connectors for %s:\n", dev);

    res = drmModeGetResources(drm_fd);
    if (res == NULL) {
        return;
    }

    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn)
            continue;
        if (!conn->count_encoders)
            return;
        drm_conn_name(conn, name, sizeof(name));

        enc = NULL;
        crtc = NULL;
        if (conn->encoder_id) {
            enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
            if (enc && enc->crtc_id) {
                crtc = drmModeGetCrtc(drm_fd, enc->crtc_id);
            }
        }

        if (conn->connection == DRM_MODE_CONNECTED && crtc) {
            fprintf(stdout, "    %-10s: %dx%d\n", name,
                    crtc->width, crtc->height);
        } else {
            fprintf(stdout, "    %-10s: disconnected\n", name);
        }

        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(enc);
        drmModeFreeConnector(conn);
    }
}
