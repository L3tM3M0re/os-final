#include <unios/wm.h>
#include <unios/memory.h>
#include <unios/tracing.h>
#include <unios/assert.h>
#include <unios/page.h>
#include <unios/graphics.h>
#include <unios/tty.h>
#include <stdlib.h>
#include <string.h>

#define WM_BORDER 4
#define WM_TITLE  20
#define WM_MIN_W  80
#define WM_MIN_H  60

typedef struct wm_window_s {
    int                id;
    graphics_surface_t surface;
    graphics_rect_t    rect;       // screen pos & size
    graphics_rect_t    restored;   // for restore after maximize
    wm_window_state_t  state;
    bool               visible;
    uint32_t           color;
    struct wm_window_s *prev;
    struct wm_window_s *next;
} wm_window_t;

static wm_window_t *g_zlist_head;
static wm_window_t *g_zlist_tail;
static wm_window_t *g_active;
static wm_window_t *g_capture;
static int          g_next_id = 1;

typedef enum {
    WM_ACTION_NONE,
    WM_ACTION_MOVE,
    WM_ACTION_RESIZE,
} wm_action_t;

static struct {
    wm_action_t   action;
    wm_hit_t      hit;
    int           start_x;
    int           start_y;
    graphics_rect_t start_rect;
} g_mouse_ctx;

static void zlist_push_front(wm_window_t *win) {
    win->prev = NULL;
    win->next = g_zlist_head;
    if (g_zlist_head) { g_zlist_head->prev = win; }
    g_zlist_head = win;
    if (g_zlist_tail == NULL) { g_zlist_tail = win; }
}

void wm_raise(wm_window_t *win) {
    if (win == NULL) { return; }
    if (win == g_zlist_head) { return; }
    if (win->prev) { win->prev->next = win->next; }
    if (win->next) { win->next->prev = win->prev; }
    if (g_zlist_tail == win) { g_zlist_tail = win->prev; }
    zlist_push_front(win);
}

static wm_window_t *wm_find_top_at(int x, int y) {
    wm_window_t *p = g_zlist_head;
    while (p) {
        if (p->visible && p->state != WM_STATE_MINIMIZED
            && p->state != WM_STATE_HIDDEN) {
            int rx = p->rect.x;
            int ry = p->rect.y;
            int rw = p->rect.w;
            int rh = p->rect.h;
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
                return p;
            }
        }
        p = p->next;
    }
    return NULL;
}

static wm_hit_t wm_hittest(wm_window_t *win, int x, int y) {
    if (win == NULL) { return WM_HIT_NONE; }
    int rx = win->rect.x;
    int ry = win->rect.y;
    int rw = win->rect.w;
    int rh = win->rect.h;
    if (x < rx || x >= rx + rw || y < ry || y >= ry + rh) {
        return WM_HIT_NONE;
    }

    bool left   = x < rx + WM_BORDER;
    bool right  = x >= rx + rw - WM_BORDER;
    bool top    = y < ry + WM_BORDER;
    bool bottom = y >= ry + rh - WM_BORDER;
    bool title  = y < ry + WM_TITLE;

    if (top && left) { return WM_HIT_TOPLEFT; }
    if (top && right) { return WM_HIT_TOPRIGHT; }
    if (bottom && left) { return WM_HIT_BOTTOMLEFT; }
    if (bottom && right) { return WM_HIT_BOTTOMRIGHT; }
    if (top) { return WM_HIT_TOP; }
    if (bottom) { return WM_HIT_BOTTOM; }
    if (left) { return WM_HIT_LEFT; }
    if (right) { return WM_HIT_RIGHT; }
    if (title) { return WM_HIT_TITLE; }
    return WM_HIT_CLIENT;
}

wm_window_t *wm_create_window(int x, int y, int w, int h, uint32_t color) {
    if (w < WM_MIN_W) { w = WM_MIN_W; }
    if (h < WM_MIN_H) { h = WM_MIN_H; }
    wm_window_t *win = kmalloc(sizeof(wm_window_t));
    if (win == NULL) { return NULL; }
    memset(win, 0, sizeof(*win));
    win->id       = g_next_id++;
    win->rect.x   = x;
    win->rect.y   = y;
    win->rect.w   = w;
    win->rect.h   = h;
    win->restored = win->rect;
    win->state    = WM_STATE_NORMAL;
    win->visible  = true;
    win->color    = color;

    win->surface.width  = w;
    win->surface.height = h;
    win->surface.bpp    = 32;
    win->surface.pitch  = w * 4;
    win->surface.size   = win->surface.pitch * h;
    win->surface.pixels = kmalloc(win->surface.size);
    win->surface.owns   = true;
    if (win->surface.pixels == NULL) {
        kerror("wm: window surface alloc failed");
        kfree(win);
        return NULL;
    }
    // fill client area
    graphics_rect_t rect = {0, 0, w, h};
    graphics_fill_rect(&win->surface, rect, color);

    zlist_push_front(win);
    return win;
}

void wm_destroy_window(wm_window_t *win) {
    if (win == NULL) { return; }
    if (win->prev) { win->prev->next = win->next; }
    if (win->next) { win->next->prev = win->prev; }
    if (g_zlist_head == win) { g_zlist_head = win->next; }
    if (g_zlist_tail == win) { g_zlist_tail = win->prev; }
    if (win->surface.owns && win->surface.pixels) {
        kfree(win->surface.pixels);
    }
    kfree(win);
}

void wm_move(wm_window_t *win, int x, int y) {
    if (win == NULL) { return; }
    win->rect.x = x;
    win->rect.y = y;
}

void wm_resize(wm_window_t *win, int w, int h) {
    if (win == NULL) { return; }
    if (w < WM_MIN_W) { w = WM_MIN_W; }
    if (h < WM_MIN_H) { h = WM_MIN_H; }
    win->rect.w = w;
    win->rect.h = h;
}

void wm_set_state(wm_window_t *win, wm_window_state_t state) {
    if (win == NULL) { return; }
    if (win->state == state) { return; }
    if (state == WM_STATE_MAXIMIZED) {
        win->restored = win->rect;
        const graphics_mode_t *m = graphics_current_mode();
        if (m) {
            win->rect.x = 0;
            win->rect.y = 0;
            win->rect.w = m->width;
            win->rect.h = m->height;
        }
    } else if (state == WM_STATE_NORMAL && win->state == WM_STATE_MAXIMIZED) {
        win->rect = win->restored;
    }
    win->state = state;
}

static void wm_draw_chrome(wm_window_t *win, graphics_surface_t *dst) {
    if (!win->visible || win->state == WM_STATE_MINIMIZED
        || win->state == WM_STATE_HIDDEN) {
        return;
    }
    graphics_rect_t title_rect = {
        win->rect.x,
        win->rect.y,
        win->rect.w,
        WM_TITLE,
    };
    graphics_fill_rect(dst, title_rect, 0xFF333333);

    graphics_rect_t border_rect = {
        win->rect.x,
        win->rect.y + WM_TITLE,
        win->rect.w,
        win->rect.h - WM_TITLE,
    };
    graphics_fill_rect(dst, border_rect, 0xFF666666);
}

static void wm_draw_window(wm_window_t *win, graphics_surface_t *dst) {
    if (!win->visible || win->state == WM_STATE_MINIMIZED
        || win->state == WM_STATE_HIDDEN) {
        return;
    }
    graphics_rect_t src_rect = {0, 0, win->rect.w, win->rect.h};
    graphics_blit(&win->surface, src_rect, dst, win->rect.x, win->rect.y);
    wm_draw_chrome(win, dst);
}

void wm_composite(void) {
    graphics_surface_t *back = graphics_backbuffer();
    if (back == NULL) { return; }
    const graphics_surface_t *bg = graphics_background();
    if (bg) {
        graphics_rect_t full = {0, 0, bg->width, bg->height};
        graphics_blit(bg, full, back, 0, 0);
    } else {
        graphics_rect_t full = {0, 0, back->width, back->height};
        graphics_fill_rect(back, full, 0xFF1E1E1E);
    }

    wm_window_t *p = g_zlist_tail;
    while (p) {
        wm_draw_window(p, back);
        p = p->prev;
    }
    graphics_present(NULL, 0);
    graphics_cursor_render();
}

void wm_init(void) {
    g_zlist_head = g_zlist_tail = g_active = g_capture = NULL;
    g_mouse_ctx.action = WM_ACTION_NONE;
    g_mouse_ctx.hit    = WM_HIT_NONE;
    const graphics_mode_t *m = graphics_current_mode();
    if (!m) { return; }
    wm_window_t *w1 = wm_create_window(40, 40, m->width / 2, m->height / 2, 0xFF80C0FF);
    wm_window_t *w2 = wm_create_window(m->width / 4, m->height / 4, m->width / 2, m->height / 2, 0xFF90FF90);
    (void)w1;
    (void)w2;
    wm_composite();
}

static void wm_begin_drag(wm_window_t *win, wm_hit_t hit, int x, int y) {
    g_capture            = win;
    g_mouse_ctx.action   = (hit == WM_HIT_CLIENT || hit == WM_HIT_TITLE)
                               ? WM_ACTION_MOVE
                               : WM_ACTION_RESIZE;
    g_mouse_ctx.hit      = hit;
    g_mouse_ctx.start_x  = x;
    g_mouse_ctx.start_y  = y;
    g_mouse_ctx.start_rect = win->rect;
}

static void wm_update_drag(int x, int y) {
    if (g_capture == NULL) { return; }
    int dx = x - g_mouse_ctx.start_x;
    int dy = y - g_mouse_ctx.start_y;
    graphics_rect_t rect = g_mouse_ctx.start_rect;

    if (g_mouse_ctx.action == WM_ACTION_MOVE) {
        rect.x += dx;
        rect.y += dy;
    } else if (g_mouse_ctx.action == WM_ACTION_RESIZE) {
        switch (g_mouse_ctx.hit) {
        case WM_HIT_LEFT:
        case WM_HIT_TOPLEFT:
        case WM_HIT_BOTTOMLEFT:
            rect.x += dx;
            rect.w -= dx;
            break;
        default: break;
        }
        switch (g_mouse_ctx.hit) {
        case WM_HIT_RIGHT:
        case WM_HIT_TOPRIGHT:
        case WM_HIT_BOTTOMRIGHT:
            rect.w += dx;
            break;
        default: break;
        }
        switch (g_mouse_ctx.hit) {
        case WM_HIT_TOP:
        case WM_HIT_TOPLEFT:
        case WM_HIT_TOPRIGHT:
            rect.y += dy;
            rect.h -= dy;
            break;
        default: break;
        }
        switch (g_mouse_ctx.hit) {
        case WM_HIT_BOTTOM:
        case WM_HIT_BOTTOMLEFT:
        case WM_HIT_BOTTOMRIGHT:
            rect.h += dy;
            break;
        default: break;
        }
    }

    if (rect.w < WM_MIN_W) rect.w = WM_MIN_W;
    if (rect.h < WM_MIN_H) rect.h = WM_MIN_H;
    g_capture->rect = rect;
}

void wm_on_mouse(int x, int y, int buttons) {
    static int prev_buttons = 0;
    bool left_down          = buttons & MOUSE_LEFT_BUTTON;
    bool left_prev          = prev_buttons & MOUSE_LEFT_BUTTON;
    bool need_comp          = false;

    if (left_down && !left_prev) {
        wm_window_t *hit_win = wm_find_top_at(x, y);
        if (hit_win) {
            g_active = hit_win;
            wm_raise(hit_win);
            wm_hit_t hit = wm_hittest(hit_win, x, y);
            wm_begin_drag(hit_win, hit, x, y);
            need_comp = true;
        }
    } else if (!left_down && left_prev) {
        g_capture          = NULL;
        g_mouse_ctx.action = WM_ACTION_NONE;
        g_mouse_ctx.hit    = WM_HIT_NONE;
        need_comp          = true;
    } else if (left_down && g_capture) {
        wm_update_drag(x, y);
        need_comp = true;
    }

    prev_buttons = buttons;
    if (need_comp) { wm_composite(); }
}
