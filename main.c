/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "demos/music/lv_demo_music.h"
#include "lv_area.h"
#include "lv_color.h"
#include "lv_hal_disp.h"
#include "lv_vglite_buf.h"
#include "lv_refr.h"
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "lv_drivers/display/drm-ext.h"
#include "lv_drivers/indev/evdev.h"
#include "vg_lite.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#define print(msg, ...)	fprintf(stderr, msg, ##__VA_ARGS__);
#define err(msg, ...)  print("error: " msg "\n", ##__VA_ARGS__)
#define info(msg, ...) print(msg "\n", ##__VA_ARGS__)
#define dbg(msg, ...)  {} //print(DBG_TAG ": " msg "\n", ##__VA_ARGS__)

const char* vg_lite_error_string[] = {
    "VG_LITE_SUCCESS",            /*! Success. */
    "VG_LITE_INVALID_ARGUMENT",       /*! An invalid argument was specified. */
    "VG_LITE_OUT_OF_MEMORY",          /*! Out of memory. */
    "VG_LITE_NO_CONTEXT",             /*! No context or an unintialized context specified. */
    "VG_LITE_TIMEOUT",                /*! A timeout has occurred during a wait. */
    "VG_LITE_OUT_OF_RESOURCES",       /*! Out of system resources. */
    "VG_LITE_GENERIC_IO",             /*! Cannot communicate with the kernel driver. */
    "VG_LITE_NOT_SUPPORT",            /*! Function call not supported. */
    "VG_LITE_ALREADY_EXISTS",         /*! Object already exists */
    "VG_LITE_NOT_ALIGNED",            /*! Data alignment error */
    "VG_LITE_FLEXA_TIME_OUT",         /*! VG timeout requesting for segment buffer */
    "VG_LITE_FLEXA_HANDSHAKE_FAIL",   /*! VG and SBI synchronizer handshake failed */
};

#define tvdelta(tv1,tv2) ((tv2.tv_sec-tv1.tv_sec)*1000000+tv2.tv_usec-tv1.tv_usec)

#define ELAPSED(x) do{\
    struct timeval tv,tv2;\
    gettimeofday(&tv,NULL);\
    x;\
    gettimeofday(&tv2,NULL);\
    fprintf(stderr,#x" elapsed %lu us\n",tvdelta(tv,tv2));\
    }while(0)

#define CKE(x) do{int e=x;if(e){fprintf(stderr,"%s:%d "#x" error: %s\n",__FILE__,__LINE__,vg_lite_error_string[e]);goto error;}}while(0)

lv_coord_t lcd_w;
lv_coord_t lcd_h;
lv_coord_t lcd_dpi;

extern struct drm_dev drm_dev;

static uint32_t draw_buf_size;

static vg_lite_buffer_t gpu_draw_buffer[2];
static vg_lite_buffer_t drm_buffer[2];
#if LV_USE_GPU_NXP_VG_LITE
vg_lite_buffer_t* lv_vglite_get_buf_by_virt(const lv_color_t * buf, const lv_area_t * area, lv_coord_t stride) {
    vg_lite_buffer_t* vgbuf;
    if (buf == gpu_draw_buffer[0].memory) {
        vgbuf = &gpu_draw_buffer[0];
    } else if (buf == gpu_draw_buffer[1].memory) {
        vgbuf = &gpu_draw_buffer[1];
    } else {
        fprintf(stderr, "%s:%d require invalid buffer: %p, stride: %d\n", __FILE__, __LINE__, buf, stride);
        return NULL;
    }
    vgbuf->width = lv_area_get_width(area);
    vgbuf->height = lv_area_get_height(area);
    vgbuf->format = VG_LITE_BGRA8888;
    vgbuf->tiled = VG_LITE_LINEAR;
    vgbuf->image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    vgbuf->transparency_mode = VG_LITE_IMAGE_OPAQUE;
    vgbuf->stride = stride * sizeof(lv_color_t);
    //fprintf(stderr, "stride(%u) width(%u) height(%u)\n", vgbuf->stride, vgbuf->width, vgbuf->height);
    return vgbuf;
}
#endif

static void sw_blit(lv_color_t *dest, const lv_color_t *src, const lv_area_t *area) {
    lv_coord_t w = (area->x2 - area->x1 + 1);

    for (unsigned y = 0, i = area->y1 ; i <= area->y2 ; ++i, ++y) {
        memcpy(dest + (area->x1) + (lcd_w * i), src + (w * y), w * (LV_COLOR_SIZE/8));
    }
}

static void drm_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    int i, y;
    struct drm_buffer* fbuf = &drm_dev.drm_bufs[0];

    lv_coord_t w = (area->x2 - area->x1 + 1);
	lv_coord_t h = (area->y2 - area->y1 + 1);

#define GPU_BLIT 0
#if GPU_BLIT
    vg_lite_matrix_t matrix;

    if (color_p != gpu_draw_buffer[0].memory) {
        fprintf(stderr, "Error: draw buffer is not GPU allocated.\n");
        return;
    }
    gpu_draw_buffer[0].format = VG_LITE_BGRA8888;
    gpu_draw_buffer[0].tiled = VG_LITE_LINEAR;
    gpu_draw_buffer[0].image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    gpu_draw_buffer[0].transparency_mode = VG_LITE_IMAGE_OPAQUE;
    gpu_draw_buffer[0].width = lv_area_get_width(area);
    gpu_draw_buffer[0].height = lv_area_get_height(area);
    gpu_draw_buffer[0].stride = ((gpu_draw_buffer[0].width + 15) & (~15U)) * sizeof(lv_color_t);
    fprintf(stderr, "Debug: stride(%u) width(%u) height(%u)\n",
        gpu_draw_buffer[0].stride,
        gpu_draw_buffer[0].width, gpu_draw_buffer[0].height
    );
    vg_lite_identity(&matrix);
    vg_lite_translate(area->x1, area->y1, &matrix);
    CKE(vg_lite_blit(&drm_buffer[0], &gpu_draw_buffer[0], NULL, VG_LITE_BLEND_SRC_OVER, 0, VG_LITE_FILTER_POINT));
    CKE(vg_lite_finish());
#else
#if LV_USE_GPU_NXP_VG_LITE
    ELAPSED(CKE(vg_lite_finish()));
#endif
    if (disp_drv->direct_mode) {
        memcpy(fbuf->map, color_p, draw_buf_size);
        #if 0
        for (y = 0, i = area->y1; i <= area->y2; i++, y++) {
            memcpy((lv_color_t *)fbuf->map + (area->x1) + (lcd_w * i)
                , (lv_color_t *)color_p + (area->x1) + (lcd_w * i),
                w * (LV_COLOR_SIZE/8));
        }
        lv_disp_t *disp = _lv_refr_get_disp_refreshing();
        #endif
    } else {
        ELAPSED(sw_blit(fbuf->map, color_p, area));
    }
#endif

    if (drm_dev.req)
        drm_wait_vsync();
    if (drm_dmabuf_set_plane(&drm_dev.drm_bufs[0])) {
		err("Flush fail");
		return;
	}

error:

	lv_disp_flush_ready(disp_drv);
}

static void sig_exit(int x) {
    exit(0);
}

int main(void)
{
    signal(SIGINT, sig_exit);
    /*LittlevGL init*/
    lv_init();

    /*Linux frame buffer device init*/
    drm_init();
    drm_get_sizes(&lcd_w, &lcd_h, (uint32_t*)&lcd_dpi);
    printf("lcd w,h,dpi:%d,%d,%d \n", lcd_w, lcd_h, lcd_dpi);

    if (drm_dmabuf_set_plane(&drm_dev.drm_bufs[0])) {
		err("Flush fail");
		return -1;
	}

    draw_buf_size = lcd_w * lcd_h * sizeof(lv_color_t); /*1/4 screen sized buffer has the same performance */
    CKE(vg_lite_init(lcd_w, lcd_h));
    for (unsigned i = 0; i < 1; i++) {
        gpu_draw_buffer[i].width = lcd_w;
        gpu_draw_buffer[i].height = lcd_h;
        gpu_draw_buffer[i].format = VG_LITE_BGRA8888;
        CKE(vg_lite_allocate(&gpu_draw_buffer[i]));
    }

#if GPU_BLIT
    // map drm buffer
    drm_buffer[0].width = lcd_w;
    drm_buffer[0].height = lcd_h;
    drm_buffer[0].stride = lcd_w * sizeof(lv_color_t);
    drm_buffer[0].format = VG_LITE_BGRA8888;
    drm_buffer[0].memory = drm_dev.drm_bufs[0].map;
    CKE(vg_lite_map(&drm_buffer[0], VG_LITE_MAP_DMABUF, drm_dev.drm_bufs[0].dmabuf_fd));
#endif

    static lv_disp_draw_buf_t draw_buf;
    printf("buf1: %p, buf2: %p\n", gpu_draw_buffer[0].memory, gpu_draw_buffer[1].memory);
    lv_disp_draw_buf_init(&draw_buf, gpu_draw_buffer[0].memory, NULL, draw_buf_size / sizeof(lv_color_t));

    /*Initialize and register a display driver*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &draw_buf;
    disp_drv.flush_cb   = drm_flush;
    disp_drv.hor_res    = lcd_w;
    disp_drv.ver_res    = lcd_h;
    disp_drv.screen_transp = 1;
    disp_drv.sw_rotate = 1;
    disp_drv.direct_mode = 0;
    disp_drv.full_refresh = 0;
    disp_drv.rotated = LV_DISP_ROT_180;
    lv_disp_drv_register(&disp_drv);

    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1); /*Basic initialization*/
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

    /*This function will be called periodically (by the library) to get the mouse position and state*/
    indev_drv_1.read_cb = evdev_read;
    lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv_1);


    /*Set a cursor for the mouse*/
    LV_IMG_DECLARE(mouse_cursor_icon)
    lv_obj_t * cursor_obj = lv_img_create(lv_scr_act()); /*Create an image object for the cursor */
    lv_img_set_src(cursor_obj, &mouse_cursor_icon);           /*Set the image source*/
    lv_indev_set_cursor(mouse_indev, cursor_obj);             /*Connect the image  object to the driver*/


    /*Create a Demo*/
    lv_demo_widgets();
    //printf("lvgl version:%d.%d.%d \n", lv_version_major(), lv_version_minor(), lv_version_patch());

    /*Handle LitlevGL tasks (tickless mode)*/
    while(1) {
        lv_timer_handler();
        //usleep(1000);
    }

    return 0;

error:
    return -1;
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
