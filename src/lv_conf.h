/**
 * @file lv_conf.h
 * Configuration file for v9.4.0 (Optimized for Linux DRM)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_PTHREAD

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB888       1
    #define LV_DRAW_SW_SUPPORT_ARGB8888     1
    #define LV_DRAW_SW_DRAW_UNIT_CNT        1
    #define LV_DRAW_SW_COMPLEX              1
#endif

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/*==================
 * FONT USAGE
 *===================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
/* 找到這些行，把 0 改成 1 */
#define LV_FONT_MONTSERRAT_16 1   // ← 改這個
#define LV_FONT_MONTSERRAT_20 1   // ← 改這個
#define LV_FONT_MONTSERRAT_24 1   // ← 或這個（更大）
#define LV_FONT_MONTSERRAT_22 1   // ← 最接近範例相片比例
#define LV_FONT_MONTSERRAT_28 1   // ← 最接近範例相片比例
#define LV_FONT_MONTSERRAT_40 1   // ← 最接近範例相片比例
#define LV_FONT_MONTSERRAT_46 1   // ← 最接近範例相片比例
/*==================
 * WIDGETS
 *================*/
#define LV_USE_LABEL        1
#define LV_USE_BUTTON       1
#define LV_USE_CANVAS       1

/*==================
 * DEVICES / DRIVERS
 *==================*/

/** Driver for /dev/dri/cardX (Linux DRM/KMS) */
#define LV_USE_LINUX_DRM        1
#if LV_USE_LINUX_DRM
    #define LV_LINUX_DRM_USE_EGL     0
    #define LV_USE_LINUX_DRM_GBM_BUFFERS 0
#endif

/** Driver for evdev (輸入裝置) */
#define LV_USE_EVDEV    1

/*=====================
* BUILD OPTIONS
*======================*/
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /*LV_CONF_H*/