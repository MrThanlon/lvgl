/**
 * @file drm.h
 *
 */

#ifndef DRM_H
#define DRM_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#ifndef LV_DRV_NO_CONF
#ifdef LV_CONF_INCLUDE_SIMPLE
#include "lv_drv_conf.h"
#else
#include "../../lv_drv_conf.h"
#endif
#endif

#if USE_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

struct drm_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint32_t offset;
	unsigned long int size;
	void * map;
	uint32_t fb_handle;
	int dmabuf_fd;
};

struct drm_dev {
	int fd;
	uint32_t conn_id, enc_id, crtc_id, plane_id, crtc_idx;
	uint32_t width, height;
	uint32_t mmWidth, mmHeight;
	uint32_t fourcc;
	drmModeModeInfo mode;
	uint32_t blob_id;
	drmModeCrtc *saved_crtc;
	drmModeAtomicReq *req;
	drmEventContext drm_event_ctx;
	drmModePlane *plane;
	drmModeCrtc *crtc;
	drmModeConnector *conn;
	uint32_t count_plane_props;
	uint32_t count_crtc_props;
	uint32_t count_conn_props;
	drmModePropertyPtr plane_props[128];
	drmModePropertyPtr crtc_props[128];
	drmModePropertyPtr conn_props[128];
	struct drm_buffer drm_bufs[2]; /* DUMB buffers */
	struct drm_buffer *cur_bufs[2]; /* double buffering handling */
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void drm_init(void);
void drm_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi);
void drm_exit(void);
void drm_wait_vsync(void);
int drm_dmabuf_set_plane(struct drm_buffer *buf);

/**********************
 *      MACROS
 **********************/

#endif  /*USE_DRM*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DRM_H*/
