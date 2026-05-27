#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include "lvgl.h"
#include "drivers/display/drm/lv_linux_drm.h"

// 設定你的裝置節點 (依據你的 modetest 輸出)
#define DRM_DEVICE "/dev/dri/card1"
#define CONNECTOR_ID 48
#define KEY_DEVICE "/dev/input/event1" // 請確保此路徑正確

static lv_obj_t * status_label;
static bool running = true;

// UI 更新函式 (確保執行緒安全)
static void update_label_cb(void * data) {
    lv_label_set_text(status_label, (const char *)data);
}

// 按鍵監聽執行緒
static void *input_thread(void *arg) {
    int fd = open(KEY_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("無法開啟按鍵裝置");
        return NULL;
    }

    struct input_event ev;
    while (running && read(fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY) {
            // 透過 async_call 確保 UI 更新在主執行緒執行
            if (ev.value == 1)      lv_async_call(update_label_cb, "狀態: 按下 (Pressed)");
            else if (ev.value == 0) lv_async_call(update_label_cb, "狀態: 釋放 (Released)");
        }
    }
    close(fd);
    return NULL;
}

void exit_handler(int sig) { running = false; }

int main(void) {
    signal(SIGINT, exit_handler);
    lv_init();

    // 1. 初始化 DRM 顯示
    lv_display_t * disp = lv_linux_drm_create();
    lv_linux_drm_set_file(disp, DRM_DEVICE, CONNECTOR_ID);

    // 2. 建立 UI
    lv_obj_t * scr = lv_screen_active();
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "System Ready");
    lv_obj_center(status_label);

    // 3. 啟動按鍵監聽執行緒
    pthread_t thread;
    pthread_create(&thread, NULL, input_thread, NULL);
    pthread_detach(thread);

    // 4. 主迴圈
    while(running) {
        lv_timer_handler();
        usleep(5000);
    }
    return 0;
}