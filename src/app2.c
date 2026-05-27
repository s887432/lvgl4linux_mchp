#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "lvgl.h"
#include "drivers/display/drm/lv_linux_drm.h"

// ── 裝置設定 ──────────────────────────────────────────
#define DRM_DEVICE   "/dev/dri/card1"
#define CONNECTOR_ID 48
#define KEY_DEVICE   "/dev/input/event1"

// ── 螢幕 / 版面 ─────────────────────────────────────
#define SCR_W       720
#define SCR_H       1280
#define COLS        2
#define ROWS        13

#define COL_W       (SCR_W / COLS)
#define ROW_H       (SCR_H / ROWS)

#define CELL_PAD    5
#define CARD_W      (COL_W - CELL_PAD * 2)
#define CARD_H      (ROW_H - CELL_PAD * 2)

#define ICON_CIRCLE 44

// 閃白遮罩持續 (ms)
#define FLASH_MS    150

// ── 顏色 ─────────────────────────────────────────────
#define C_BG              0x0a1a2a
#define C_CARD            0x0d2035
#define C_CARD_SEL        0x163050
#define C_BORDER          0x1e4878
#define C_ICON_PHONE      0xffffff
#define C_NAME            0x00e676
#define C_STATUS_TXT      0xb0c8e0
#define C_FLASH_OVERLAY   0xe0e8ff
#define FLASH_OPA         LV_OPA_50

// ── 狀態 ────────────────────────────────────────────
typedef enum {
    STATUS_IDLE    = 0,
    STATUS_BUSY    = 1,
    STATUS_CALLING = 2,
    STATUS_AWAY    = 3,
    STATUS_COUNT   = 4
} cell_status_t;

static const char *status_str[STATUS_COUNT] = {
    "Idle", "Busy", "On Call", "Away"
};

static const uint32_t icon_color[STATUS_COUNT] = {
    0x00c060,  // Idle    → 綠
    0xffab00,  // Busy    → 橙黃
    0xff1744,  // On Call → 紅
    0x40c4ff,  // Away    → 藍
};

// ── 聯絡人資料 ──────────────────────────────────────────
static const char *contact_name[ROWS][COLS] = {
    { "Iva Ortega",   "Kyle Martin"  },
    { "Bette King",   "Betty Rios"   },
    { "180074135",    "80074"        },
    { "Hannah Gar",   "Eva Abbott"   },
    { "Lena Flores",  "Luis Perry"   },
    { "Lee Lane",     "Frank Oliver" },
    { "Brian Sharp",  "Bill Tate"    },
    { "737283883",    "1424454"      },
    { "Elijah Gray",  "Shane Huff"   },
    { "Iva Dixon",    "Cole Marsh"   },
    { "5322573",      "29304"        },
    { "Larry Miller", "Glen Moore"   },
    { "5613939",      "1739061"      },
};

// ── 全域狀態（atomic）跨執行緒安全）───────────────────
static bool           running           = true;
static int            selected_row      = 0;
static int            selected_col      = 0;

// 按鍵原子旗標（input thread 寫，main thread 讀）
static atomic_int     key_press_event   = 0;  // 1=剛按下, -1=剛釋放, 0=無事
static atomic_bool    key_held          = false;

// 左上格手動計數
static int            manual_key_count  = 0;

static cell_status_t  cell_status[ROWS][COLS];

// ── 每格閃爍計時器（記錄剩餘閃爍 tick）──────────────
#define FLASH_TICKS   (FLASH_MS / 5)   // 主迴圈 5ms 一 tick
static int            flash_ticks[ROWS][COLS];

// ── LVGL 物件 ─────────────────────────────────────────
static lv_obj_t *cards[ROWS][COLS];
static lv_obj_t *overlays[ROWS][COLS];
static lv_obj_t *icon_circles[ROWS][COLS];
static lv_obj_t *name_labels[ROWS][COLS];
static lv_obj_t *status_txts[ROWS][COLS];

static lv_timer_t *status_timer = NULL;

// ─────────────────────────────────────────────────────
//  重繪單一格外觀（不處理閃爍遮罩，交給主迴圈）
// ─────────────────────────────────────────────────────
static void redraw_cell(int r, int c) {
    cell_status_t st = cell_status[r][c];
    bool is_sel      = (r == selected_row && c == selected_col);

    lv_obj_set_style_bg_color(cards[r][c],
        lv_color_hex(is_sel ? C_CARD_SEL : C_CARD), 0);

    lv_obj_set_style_bg_color(icon_circles[r][c],
        lv_color_hex(icon_color[st]), 0);

    lv_label_set_text(status_txts[r][c], status_str[st]);
}

// ─────────────────────────────────────────────────────
//  觸發閃爍（設定 ticks，主迴圈負責開/關遮罩）
// ─────────────────────────────────────────────────────
static void trigger_flash(int r, int c) {
    flash_ticks[r][c] = FLASH_TICKS;
    lv_obj_remove_flag(overlays[r][c], LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────
//  定時隨機狀態更新（跳過 [0][0]）
//  修正：只在狀態真正改變時才觸發閃爍
// ─────────────────────────────────────────────────────
static void status_update_cb(lv_timer_t *timer) {
    (void)timer;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (r == 0 && c == 0) continue;
            cell_status_t old_st = cell_status[r][c];
            cell_status_t new_st = (cell_status_t)(rand() % STATUS_COUNT);
            if (new_st != old_st) {
                cell_status[r][c] = new_st;
                redraw_cell(r, c);
                trigger_flash(r, c);
            }
        }
    }
}

// ─────────────────────────────────────────────────────
//  按鍵監聽執行緒（只寫 atomic flag，極低延遲）
// ─────────────────────────────────────────────────────
static void *input_thread(void *arg) {
    (void)arg;
    int fd = open(KEY_DEVICE, O_RDONLY);
    if (fd < 0) { perror("無法開啟按鍵裝置"); return NULL; }

    struct input_event ev;
    while (running && read(fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY) {
            if (ev.value == 1) {
                atomic_store(&key_held, true);
                atomic_store(&key_press_event, 1);
            } else if (ev.value == 0) {
                atomic_store(&key_held, false);
                atomic_store(&key_press_event, -1);
            }
        }
    }
    close(fd);
    return NULL;
}

// ─────────────────────────────────────────────────────
//  處理按鍵事件（在主迴圈呼叫，直接操作 LVGL）
//  修正：[0][0] 釋放時不再觸發 flash，狀態立即可見
// ─────────────────────────────────────────────────────
static void process_key_events(void) {
    int ev = atomic_exchange(&key_press_event, 0);
    if (ev == 0) return;

    if (ev == 1) {
        // 按下：顯示選中格遮罩（不閃爍，持續亮）
        lv_obj_remove_flag(overlays[selected_row][selected_col], LV_OBJ_FLAG_HIDDEN);
        flash_ticks[selected_row][selected_col] = 0;
    } else if (ev == -1) {
        // 釋放：關閉遮罩
        lv_obj_add_flag(overlays[selected_row][selected_col], LV_OBJ_FLAG_HIDDEN);

        // 左上格 [0][0]：釋放時輪流切換狀態，立即更新不加 flash
        if (selected_row == 0 && selected_col == 0) {
            manual_key_count++;
            cell_status[0][0] = (cell_status_t)(manual_key_count % STATUS_COUNT);
            redraw_cell(0, 0);
        }
    }
}

// ─────────────────────────────────────────────────────
//  閃爍 tick 更新（在主迴圈每 5ms 呼叫一次）
// ─────────────────────────────────────────────────────
static void update_flash_ticks(void) {
    bool key_is_held = atomic_load(&key_held);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (flash_ticks[r][c] <= 0) continue;

            flash_ticks[r][c]--;
            if (flash_ticks[r][c] <= 0) {
                bool keep = (r == selected_row && c == selected_col && key_is_held);
                if (!keep) {
                    lv_obj_add_flag(overlays[r][c], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────
//  建立 UI
// ─────────────────────────────────────────────────────
static void create_grid(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_size(scr, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    const lv_font_t *font_name   = &lv_font_montserrat_40;
    const lv_font_t *font_phone  = &lv_font_montserrat_20;
    const lv_font_t *font_status = &lv_font_montserrat_16;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            flash_ticks[r][c] = 0;

            // slot
            lv_obj_t *slot = lv_obj_create(scr);
            lv_obj_set_size(slot, COL_W, ROW_H);
            lv_obj_set_pos(slot, c * COL_W, r * ROW_H);
            lv_obj_set_style_bg_color(slot, lv_color_hex(C_BG), 0);
            lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(slot, 0, 0);
            lv_obj_set_style_pad_all(slot, 0, 0);
            lv_obj_set_style_radius(slot, 0, 0);
            lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

            // card
            lv_obj_t *card = lv_obj_create(slot);
            lv_obj_set_size(card, CARD_W, CARD_H);
            lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
            lv_obj_set_style_radius(card, 14, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_set_style_clip_corner(card, true, 0);
            lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            cards[r][c] = card;

            // overlay（閃白遮罩，z-order 最高）
            lv_obj_t *ov = lv_obj_create(card);
            lv_obj_set_size(ov, CARD_W, CARD_H);
            lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(ov, lv_color_hex(C_FLASH_OVERLAY), 0);
            lv_obj_set_style_bg_opa(ov, FLASH_OPA, 0);
            lv_obj_set_style_border_width(ov, 0, 0);
            lv_obj_set_style_radius(ov, 14, 0);
            lv_obj_set_style_pad_all(ov, 0, 0);
            lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
            overlays[r][c] = ov;

            // icon circle
            int icon_left = 10;
            lv_obj_t *circle = lv_obj_create(card);
            lv_obj_set_size(circle, ICON_CIRCLE, ICON_CIRCLE);
            lv_obj_align(circle, LV_ALIGN_LEFT_MID, icon_left, 0);
            lv_obj_set_style_bg_color(circle,
                lv_color_hex(icon_color[STATUS_IDLE]), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_style_pad_all(circle, 0, 0);
            lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
            icon_circles[r][c] = circle;

            // phone symbol
            lv_obj_t *phone = lv_label_create(circle);
            lv_label_set_text(phone, LV_SYMBOL_CALL);
            lv_obj_set_style_text_color(phone, lv_color_hex(C_ICON_PHONE), 0);
            lv_obj_set_style_text_font(phone, font_phone, 0);
            lv_obj_align(phone, LV_ALIGN_CENTER, 0, 0);

            // 文字區域
            int txt_x = icon_left + ICON_CIRCLE + 8;
            int txt_w = CARD_W - txt_x - 6;

            // 姓名（靠上）
            lv_obj_t *name = lv_label_create(card);
            lv_label_set_text(name, contact_name[r][c]);
            lv_obj_set_style_text_color(name, lv_color_hex(C_NAME), 0);
            lv_obj_set_style_text_font(name, font_name, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(name, txt_w);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, txt_x, -10);
            name_labels[r][c] = name;

            // 狀態文字（姓名下方）
            lv_obj_t *stxt = lv_label_create(card);
            lv_label_set_text(stxt, status_str[STATUS_IDLE]);
            lv_obj_set_style_text_color(stxt, lv_color_hex(C_STATUS_TXT), 0);
            lv_obj_set_style_text_font(stxt, font_status, 0);
            lv_obj_set_width(stxt, txt_w);
            lv_obj_align(stxt, LV_ALIGN_LEFT_MID, txt_x, 20);
            status_txts[r][c] = stxt;
        }
    }

    // 初始渲染
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            redraw_cell(r, c);

    // 500ms 狀態更新 timer（必須 > FLASH_MS 以確保閃爍能完成）
    status_timer = lv_timer_create(status_update_cb, 500, NULL);
}

// ─────────────────────────────────────────────────────
void exit_handler(int sig) { (void)sig; running = false; }

int main(void) {
    srand((unsigned)time(NULL));
    signal(SIGINT,  exit_handler);
    signal(SIGTERM, exit_handler);

    lv_init();

    lv_display_t *disp = lv_linux_drm_create();
    lv_linux_drm_set_file(disp, DRM_DEVICE, CONNECTOR_ID);

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            cell_status[r][c] = STATUS_IDLE;

    create_grid();

    pthread_t tid;
    pthread_create(&tid, NULL, input_thread, NULL);
    pthread_detach(tid);

    // 主迴圈（5ms tick）
    while (running) {
        process_key_events();
        update_flash_ticks();
        lv_timer_handler();
        usleep(5000);
    }

    lv_deinit();
    return 0;
}
