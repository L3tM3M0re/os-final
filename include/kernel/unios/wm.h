#pragma once

#include <unios/graphics.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum wm_window_state_e {
    WM_STATE_NORMAL,
    WM_STATE_MINIMIZED,
    WM_STATE_MAXIMIZED,
    WM_STATE_HIDDEN,
} wm_window_state_t;

typedef enum wm_hit_e {
    WM_HIT_NONE,
    WM_HIT_CLIENT,
    WM_HIT_TITLE,
    WM_HIT_LEFT,
    WM_HIT_RIGHT,
    WM_HIT_TOP,
    WM_HIT_BOTTOM,
    WM_HIT_TOPLEFT,
    WM_HIT_TOPRIGHT,
    WM_HIT_BOTTOMLEFT,
    WM_HIT_BOTTOMRIGHT,
} wm_hit_t;

typedef struct wm_window_s wm_window_t;

void wm_init(void);

wm_window_t *wm_create_window(int x, int y, int w, int h, uint32_t color);
void wm_destroy_window(wm_window_t *win);

void wm_move(wm_window_t *win, int x, int y);
void wm_resize(wm_window_t *win, int w, int h);
void wm_set_state(wm_window_t *win, wm_window_state_t state);
void wm_raise(wm_window_t *win);

void wm_composite(void);

void wm_on_mouse(int x, int y, int buttons);
