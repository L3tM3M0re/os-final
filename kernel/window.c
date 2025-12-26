#include <unios/window.h>
#include <unios/clock.h>
#include <unios/memory.h>
#include <unios/assert.h>
#include <unios/tracing.h>
#include <lib/string.h>
#include <lib/container_of.h>
#include <unios/font.h>
#include <stdlib.h>
#include <stddef.h>
#include <arch/x86.h>

#define DISABLE_INTERRUPTS() __asm__ volatile("cli")
#define ENABLE_INTERRUPTS()  __asm__ volatile("sti")
#define DRAG_THRESHOLD 8

static bool g_handled = false;
static int drag_start_mx = 0;
static int drag_start_my = 0;
static bool is_drag_active = false;

static window_t* root_window = NULL;
static int win_id_counter = 0;

static window_t* drag_window = NULL; // 当前正在拖拽的窗口
static int drag_off_x = 0;           // 鼠标点击位置相对于窗口左上角的偏移
static int drag_off_y = 0;

static const graphics_rect_t EMPTY_RECT = {0, 0, 0, 0};
static graphics_rect_t g_dirty_rect = {0, 0, 0, 0};
static volatile bool g_has_dirty = false;

static volatile bool g_mouse_event_pending = false;
static int g_cmd_x = 0;
static int g_cmd_y = 0;
static int g_cmd_buttons = 0;

static int g_mouse_last_x = 0; // 记录上一帧的鼠标位置
static int g_mouse_last_y = 0;

static bool window_recreate_surface(window_t* win, int new_w, int new_h);
static bool window_set_bounds(window_t* win, int x, int y, int w, int h);
static void window_save_normal_bounds(window_t* win, int x, int y, int w, int h);
static void window_restore_from_minimized(window_t* win);

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }

// 简单矩形并
static graphics_rect_t rect_union(graphics_rect_t a, graphics_rect_t b) {
    if (a.w == 0 || a.h == 0) return b;
    if (b.w == 0 || b.h == 0) return a;

    int x1 = min_int(a.x, b.x);
    int y1 = min_int(a.y, b.y);
    int x2 = max_int(a.x + a.w, b.x + b.w);
    int y2 = max_int(a.y + a.h, b.y + b.h);

    return (graphics_rect_t){x1, y1, x2 - x1, y2 - y1};
}

static void reset_dirty_rect() {
    g_dirty_rect = EMPTY_RECT;
    g_has_dirty = false;
}

void window_mark_dirty() {
    const graphics_mode_t *mode = graphics_current_mode();
    if (mode) {
        window_invalidate_rect(root_window, 0, 0, mode->width, mode->height);
    }
}

void window_invalidate_rect(window_t* win, int x, int y, int w, int h) {
    if (!win) { return; }

    int abs_x = win->x + x;
    int abs_y = win->y + y;
    int fix_w = w;
    int fix_h = h;

    // 边界裁剪
    const graphics_mode_t *mode = graphics_current_mode();
    if (!mode) { return; }

    if (abs_x < 0) {
        fix_w += abs_x;
        abs_x = 0;
    }
    if (abs_y < 0) {
        fix_h += abs_y;
        abs_y = 0;
    }

    if (abs_x + fix_w > mode->width)  fix_w = mode->width - abs_x;
    if (abs_y + fix_h > mode->height) fix_h = mode->height - abs_y;

    if (fix_w <= 0 || fix_h <= 0) return;

    graphics_rect_t new_rect = {abs_x, abs_y, fix_w, fix_h};

    // 合并到全局脏区域
    if (!g_has_dirty) {
        g_dirty_rect = new_rect;
        g_has_dirty = true;
    } else {
        g_dirty_rect = rect_union(g_dirty_rect, new_rect);
    }
}

void window_manager_handler(void) {
    int last_refresh_tick = 0;
    while (true) {

        if (g_mouse_event_pending) {
            DISABLE_INTERRUPTS();
            int x = g_cmd_x;
            int y = g_cmd_y;
            int buttons = g_cmd_buttons;
            g_mouse_event_pending = false;
            ENABLE_INTERRUPTS();
            bool left_btn = (buttons & 1);
            if (left_btn && !g_handled) {
                g_handled = true;
                if (!drag_window) {
                    window_t* target = window_from_point(x, y);
                    if (target && target != root_window) {
                        int local_x = x - target->x;
                        int local_y = y - target->y;
                        int btn_size = WIN_TITLE_HEIGHT - 6;
                        int btn_y = 4;
                        int btn_close_x = target->w - btn_size - 4;
                        int btn_max_x = btn_close_x - btn_size - 4;
                        int btn_min_x = btn_max_x - btn_size - 4;
                        bool in_btn_band = local_y >= btn_y && local_y < btn_y + btn_size;

                        if (in_btn_band &&
                            local_x >= btn_close_x && local_x < btn_close_x + btn_size) {
                            destroy_window(target);
                        } else if (in_btn_band &&
                                   local_x >= btn_max_x && local_x < btn_max_x + btn_size) {
                            window_toggle_maximize(target);
                            window_bring_to_front(target);
                        } else if (in_btn_band &&
                                   local_x >= btn_min_x && local_x < btn_min_x + btn_size) {
                            window_toggle_minimize(target);
                            window_bring_to_front(target);
                        } else if (local_y < WIN_TITLE_HEIGHT) {
                            drag_window = target;
                            drag_off_x = x - target->x;
                            drag_off_y = y - target->y;
                            drag_start_mx = x;
                            drag_start_my = y;
                            is_drag_active = false;
                            window_bring_to_front(target);
                        } else {
                            window_bring_to_front(target);
                        }
                    }
                }
            } else if (left_btn && g_handled) {
                if (!drag_window) {

                } else {
                    if (!is_drag_active) {
                        int dx = x - drag_start_mx;
                        int dy = y - drag_start_my;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;

                        if (dx < DRAG_THRESHOLD && dy < DRAG_THRESHOLD) {

                        } else {
                            is_drag_active = true;
                        }
                    }

                    if (is_drag_active) {
                        if (drag_window->is_maximized) {
                            float ratio = (float)(x - drag_window->x) / (float)drag_window->w;

                            window_toggle_maximize(drag_window);

                            int new_w = drag_window->w;
                            drag_window->x = x - (int)(new_w * ratio);

                            drag_window->y = y - (WIN_TITLE_HEIGHT / 2);

                            drag_off_x = x - drag_window->x;
                            drag_off_y = y - drag_window->y;

                            window_invalidate_rect(root_window, 0, 0, root_window->w, root_window->h);
                        }
                        int new_x = x - drag_off_x;
                        int new_y = y - drag_off_y;
                        if (drag_window->x != new_x || drag_window->y != new_y) {
                            window_invalidate_rect(root_window, drag_window->x, drag_window->y, drag_window->w, drag_window->h);
                            drag_window->x = new_x;
                            drag_window->y = new_y;
                            window_invalidate_rect(root_window, drag_window->x, drag_window->y, drag_window->w, drag_window->h);

                            window_invalidate_rect(root_window, g_mouse_last_x, g_mouse_last_y, 32, 32);
                            window_invalidate_rect(root_window, x, y, 32, 32);
                        }
                    }
                }
            } else {
                if (drag_window) {
                    if (!drag_window->is_maximized && !drag_window->is_minimized) {
                        window_save_normal_bounds(
                            drag_window, drag_window->x, drag_window->y, drag_window->w, drag_window->h);
                    }
                    drag_window = NULL;
                }
                g_handled = false;
                is_drag_active = false;
            }

            g_mouse_last_x = x;
            g_mouse_last_y = y;
        }

        if (g_has_dirty) {
            int current_tick = system_ticks;
            if (current_tick - last_refresh_tick >= 10) {
                window_manager_refresh();
                last_refresh_tick = current_tick;
            }
        }

        yield();
    }
}

void init_window_manager() {
    const graphics_mode_t *mode = graphics_current_mode();
    if (!mode) return;

    root_window = kmalloc(sizeof(window_t));
    memset(root_window, 0, sizeof(window_t));

    root_window->id = win_id_counter++;
    root_window->x = 0;
    root_window->y = 0;
    root_window->w = mode->width;
    root_window->h = mode->height;
    root_window->flags = WIN_FLAG_VISIBLE;

    INIT_LIST_HEAD(&root_window->children);
    INIT_LIST_HEAD(&root_window->sibling);

    window_mark_dirty();

    kinfo("Window Manager initialized");
}

window_t* create_window(int x, int y, int w, int h, const char* title, uint32_t bg_color) {
    if (!root_window) return NULL;

    window_t* win = kmalloc(sizeof(window_t));
    if (!win) return NULL;
    memset(win, 0, sizeof(window_t));

    win->id = win_id_counter++;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    if (win->w < WIN_MIN_WIDTH) { win->w = WIN_MIN_WIDTH; }
    if (win->h < WIN_MIN_HEIGHT) { win->h = WIN_MIN_HEIGHT; }
    win->flags = WIN_FLAG_VISIBLE;
    win->bg_color = bg_color;
    win->has_saved_normal = false;
    win->has_restore_bounds = false;
    win->is_maximized = false;
    win->is_minimized = false;
    win->restore_to_maximized = false;
    if (title) strncpy(win->title, title, WIN_TITLE_MAX - 1);

    INIT_LIST_HEAD(&win->children);
    INIT_LIST_HEAD(&win->sibling);

    // 初始化 Surface
    win->surface.width = win->w;
    win->surface.height = win->h;
    win->surface.bpp = 32;
    win->surface.pitch = win->w * 4;
    win->surface.size = (size_t)win->w * win->h * 4;
    win->surface.owns = true;
    win->surface.pixels = kmalloc(win->surface.size);

    if (win->surface.pixels) {
        window_draw_decoration(win);
    }

    // 将新窗口加入到 root 的子窗口链表尾部
    win->parent = root_window;
    list_add_tail(&win->sibling, &root_window->children);

    window_save_normal_bounds(win, win->x, win->y, win->w, win->h);

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    return win;
}

void destroy_window(window_t* win) {
    if (!win || win == root_window) { return; }

    if (drag_window == win) {
        drag_window = NULL;
    }

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    list_del(&win->sibling);

    // 释放资源
    if (win->surface.pixels && win->surface.owns) {
        kfree(win->surface.pixels);
        win->surface.pixels = NULL;
    }
    kfree(win);
}

void window_fill(window_t* win, uint32_t color) {
    if (!win) return;

    win->bg_color = color;

    if (win->w > 4 && win->h > WIN_TITLE_HEIGHT + 2) {
        graphics_rect_t rect = {
            2,
            WIN_TITLE_HEIGHT,
            win->w - 4,
            win->h - WIN_TITLE_HEIGHT - 2
        };
        graphics_fill_rect(&win->surface, rect, color);
    }
}

// 辅助函数: 递归绘制窗口及其子窗口
static void draw_window_recursive(graphics_surface_t* dest, window_t* win, int abs_x, int abs_y) {
    if (!(win->flags & WIN_FLAG_VISIBLE)) return;

    // 计算当前窗口在屏幕上的绝对坐标
    int cur_x = abs_x + win->x;
    int cur_y = abs_y + win->y;

    // 绘制当前窗口本身
    // 如果是 root window，且没有分配 surface (作为背景容器)，则跳过绘制本体
    if (win->surface.pixels) {
        graphics_blit(dest, cur_x, cur_y, &win->surface, NULL);
    }

    // 绘制子窗口
    struct list_head *pos;
    list_for_each(pos, &win->children) {
        window_t *child = container_of(pos, window_t, sibling);
        draw_window_recursive(dest, child, cur_x, cur_y);
    }
}

void window_manager_refresh() {
    if (!g_has_dirty) return;

    graphics_rect_t dirty = g_dirty_rect;
    reset_dirty_rect();

    graphics_surface_t* back_buffer = graphics_backbuffer();
    if (!back_buffer || !root_window) return;

    graphics_lock();

    graphics_set_clip_rect(dirty.x, dirty.y, dirty.w, dirty.h);
    graphics_fill_rect(back_buffer, dirty, 0xFF336699);


    // 绘制窗口
    draw_window_recursive(back_buffer, root_window, 0, 0);

    graphics_set_clip_rect(0, 0, back_buffer->width, back_buffer->height);

    graphics_present(&dirty, 1);

    graphics_unlock();
}

window_t* window_from_point(int x, int y) {
    if (!root_window) return NULL;

    window_t *win;

    list_for_each_entry_reverse(win, &root_window->children, sibling) {

        // 检查窗口是否可见
        if (!(win->flags & WIN_FLAG_VISIBLE)) continue;

        if (x >= win->x && x < win->x + win->w &&
            y >= win->y && y < win->y + win->h) {
            return win;
        }
    }

    // 点击位置为桌面
    return root_window;
}

void window_bring_to_front(window_t* win) {
    if (!win || !win->parent) return;
    struct list_head *head = &win->parent->children;

    if (list_is_last(&win->sibling, head)) {
        return;
    }

    list_move_tail(&win->sibling, head);

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
}

void window_draw_decoration(window_t* win) {
    if (!win || !win->surface.pixels) return;

    int w = win->w;
    int h = win->h;

    // 绘制主背景
    graphics_rect_t content_rect = {
        2, WIN_TITLE_HEIGHT,
        w - 4, h - WIN_TITLE_HEIGHT - 2
    };
    if (content_rect.w > 0 && content_rect.h > 0) {
        graphics_fill_rect(&win->surface, content_rect, win->bg_color);
    }

    // 绘制标题栏
    graphics_rect_t title_rect = {
        2, 2,
        w - 4, WIN_TITLE_HEIGHT - 2
    };
    graphics_fill_rect(&win->surface, title_rect, C_TITLE_BG);

    int text_x = 6;
    int text_y = (WIN_TITLE_HEIGHT - 16) / 2 + 1;

    int btn_size = WIN_TITLE_HEIGHT - 6;
    int buttons_start_x = w - btn_size * 3 - 12;
    int max_text_width = buttons_start_x - text_x - 4;

    // TODO: 实现可调大小文字
    if (win->title[0] != '\0' && max_text_width > 0) {
        char display_title[WIN_TITLE_MAX];
        int src_idx = 0;
        int dst_idx = 0;
        int current_width = 0;
        int title_full_len = strlen(win->title);

        if (title_full_len * 8 <= max_text_width) {
            strncpy(display_title, win->title, sizeof(display_title)-1);
            display_title[sizeof(display_title)-1] = '\0';
        } else {
            int width_limit = max_text_width - 24;
            int need_ellipsis = 0;

            while (win->title[src_idx] != '\0' && dst_idx < sizeof(display_title) - 4) {
                unsigned char c = (unsigned char)win->title[src_idx];
                int char_w = 0;
                int char_bytes = 0;

                if (c < 128) {
                    char_w = ASCII_WIDTH;
                    char_bytes = 1;
                } else {
                    char_w = FONT_WIDTH;

                    if ((c & 0xE0) == 0xC0) char_bytes = 2;      // 2字节字符
                    else if ((c & 0xF0) == 0xE0) char_bytes = 3; // 3字节字符 (汉字通常在这里)
                    else if ((c & 0xF8) == 0xF0) char_bytes = 4; // 4字节字符
                    else char_bytes = 1; // 异常情况，当1字节处理
                }

                if (current_width + char_w > width_limit) {
                    need_ellipsis = 1;
                    break;
                }

                for (int k = 0; k < char_bytes; k++) {
                    if (win->title[src_idx] == '\0') break;
                    display_title[dst_idx++] = win->title[src_idx++];
                }

                current_width += char_w;
            }

            if (need_ellipsis || win->title[src_idx] != '\0') {
                if (dst_idx < sizeof(display_title) - 4) {
                    display_title[dst_idx++] = '.';
                    display_title[dst_idx++] = '.';
                    display_title[dst_idx++] = '.';
                }
            }
            display_title[dst_idx] = '\0';
        }

        // 绘制处理后的文字
        if (display_title[0] != '\0') {
            graphics_draw_text(&win->surface, text_x, text_y, display_title, 0xFFFFFFFF);
        }
    }

    // if (win->title[0] != '\0') {
    //     graphics_draw_text(&win->surface, text_x, text_y, win->title, 0xFFFFFFFF);
    // }

    // 绘制边框
    graphics_rect_t r_top = {0, 0, w, 2};
    graphics_fill_rect(&win->surface, r_top, C_BORDER_LIGHT);
    graphics_rect_t r_left = {0, 0, 2, h};
    graphics_fill_rect(&win->surface, r_left, C_BORDER_LIGHT);
    graphics_rect_t r_bottom = {0, h - 2, w, 2};
    graphics_fill_rect(&win->surface, r_bottom, C_BORDER_DARK);
    graphics_rect_t r_right = {w - 2, 0, 2, h};
    graphics_fill_rect(&win->surface, r_right, C_BORDER_DARK);

    // 绘制关闭按钮
    int btn_y = 4;
    graphics_rect_t close_rect = {
        w - btn_size - 4,
        btn_y,
        btn_size,
        btn_size
    };
    graphics_fill_rect(&win->surface, close_rect, C_CLOSE_BTN);

    graphics_rect_t max_rect = {
        w - btn_size * 2 - 8,
        btn_y,
        btn_size,
        btn_size
    };
    graphics_fill_rect(&win->surface, max_rect, C_MAX_BTN);

    graphics_rect_t min_rect = {
        w - btn_size * 3 - 12,
        btn_y,
        btn_size,
        btn_size
    };
    graphics_fill_rect(&win->surface, min_rect, C_MIN_BTN);
}

void window_manager_on_mouse(int x, int y, int buttons) {
    g_cmd_x = x;
    g_cmd_y = y;
    g_cmd_buttons = buttons;
    g_mouse_event_pending = true;
}

static bool window_recreate_surface(window_t* win, int new_w, int new_h) {
    if (!win) { return false; }

    int clamped_w = max_int(new_w, WIN_MIN_WIDTH);
    int min_h = win->is_minimized ? WIN_MINIMIZED_HEIGHT : WIN_MIN_HEIGHT;
    int clamped_h = max_int(new_h, min_h);

    size_t new_size = (size_t)clamped_w * clamped_h * 4;
    void* new_pixels = kmalloc(new_size);
    if (!new_pixels) {
        kwarn("window: alloc %zu bytes for resize failed", new_size);
        return false;
    }

    if (win->surface.pixels && win->surface.owns) {
        kfree(win->surface.pixels);
    }

    win->surface.pixels = new_pixels;
    win->surface.width = clamped_w;
    win->surface.height = clamped_h;
    win->surface.pitch = clamped_w * 4;
    win->surface.size = new_size;
    win->surface.owns = true;

    win->w = clamped_w;
    win->h = clamped_h;

    return true;
}

static bool window_set_bounds(window_t* win, int x, int y, int w, int h) {
    if (!win || win == root_window) { return false; }

    int clamped_w = max_int(w, WIN_MIN_WIDTH);
    int min_h = win->is_minimized ? WIN_MINIMIZED_HEIGHT : WIN_MIN_HEIGHT;
    int clamped_h = max_int(h, min_h);

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    if (win->w != clamped_w || win->h != clamped_h || !win->surface.pixels) {
        if (!window_recreate_surface(win, clamped_w, clamped_h)) {
            window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
            return false;
        }
    } else {
        win->surface.width = clamped_w;
        win->surface.height = clamped_h;
        win->surface.pitch = clamped_w * 4;
        win->surface.size = (size_t)clamped_w * clamped_h * 4;
    }

    win->x = x;
    win->y = y;
    win->w = clamped_w;
    win->h = clamped_h;

    window_draw_decoration(win);
    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
    return true;
}

static void window_save_normal_bounds(window_t* win, int x, int y, int w, int h) {
    if (!win) { return; }
    win->saved_normal_x = x;
    win->saved_normal_y = y;
    win->saved_normal_w = w;
    win->saved_normal_h = h;
    win->has_saved_normal = true;
}

static void window_restore_from_minimized(window_t* win) {
    if (!win || !win->is_minimized || !win->has_restore_bounds) { return; }

    bool restore_max = win->restore_to_maximized;
    win->is_minimized = false;
    window_set_bounds(win, win->restore_x, win->restore_y, win->restore_w, win->restore_h);
    win->is_maximized = restore_max;
    win->has_restore_bounds = false;
    win->restore_to_maximized = false;
}

void window_toggle_maximize(window_t* win) {
    const graphics_mode_t *mode = graphics_current_mode();
    if (!win || !mode || win == root_window) { return; }

    if (win->is_minimized) {
        bool was_max = win->restore_to_maximized;
        window_restore_from_minimized(win);
        if (was_max) {
            return;
        }
    }

    if (win->is_maximized) {
        if (win->has_saved_normal) {
            window_set_bounds(
                win,
                win->saved_normal_x,
                win->saved_normal_y,
                win->saved_normal_w,
                win->saved_normal_h);
        }
        win->is_maximized = false;
        return;
    }

    window_save_normal_bounds(win, win->x, win->y, win->w, win->h);
    if (window_set_bounds(win, 0, 0, mode->width, mode->height)) {
        win->is_maximized = true;
        win->is_minimized = false;
        win->has_restore_bounds = false;
        win->restore_to_maximized = false;
    }
}

void window_toggle_minimize(window_t* win) {
    const graphics_mode_t *mode = graphics_current_mode();
    if (!win || !mode || win == root_window) { return; }

    if (win->is_minimized) {
        window_restore_from_minimized(win);
        return;
    }

    win->restore_x = win->x;
    win->restore_y = win->y;
    win->restore_w = win->w;
    win->restore_h = win->h;
    win->restore_to_maximized = win->is_maximized;
    win->has_restore_bounds = true;
    win->is_maximized = false;
    win->is_minimized = true;

    int new_w = win->w;
    if (new_w > mode->width) {
        new_w = mode->width;
    }

    if (!window_set_bounds(win, win->x, win->y, new_w, WIN_MINIMIZED_HEIGHT)) {
        win->is_minimized = false;
        win->has_restore_bounds = false;
        win->restore_to_maximized = false;
    }
}
