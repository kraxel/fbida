#include <xf86drm.h>
#include <xf86drmMode.h>

/* internal */
extern int drm_fd;
extern drmModeEncoder *drm_enc;
extern drmModeModeInfo *drm_mode;
extern drmModeConnector *drm_conn;
void drm_cleanup_display(void);
int drm_init_dev(const char *dev, const char *output, const char *mode);

/* drmtools.c */
gfxstate *drm_init(const char *device, const char *output,
                   const char *mode, bool pageflip);
void drm_info(const char *device);
