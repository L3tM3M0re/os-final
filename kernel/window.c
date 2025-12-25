#include <unios/window.h>
#include <unios/memory.h>
#include <unios/assert.h>
#include <unios/tracing.h>
#include <lib/string.h>
#include <lib/container_of.h>

static window_t* root_window = NULL;
static int win_id_counter = 0;

static window_t* drag_window = NULL; // 当前正在拖拽的窗口
static int drag_off_x = 0;           // 鼠标点击位置相对于窗口左上角的偏移
static int drag_off_y = 0;

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
    win->flags = WIN_FLAG_VISIBLE;
    win->bg_color = bg_color;
    if (title) strncpy(win->title, title, WIN_TITLE_MAX - 1);

    INIT_LIST_HEAD(&win->children);
    INIT_LIST_HEAD(&win->sibling);

    // 初始化 Surface
    win->surface.width = w;
    win->surface.height = h;
    win->surface.bpp = 32;
    win->surface.pitch = w * 4;
    win->surface.size = w * h * 4;
    win->surface.owns = true;
    win->surface.pixels = kmalloc(win->surface.size);

    if (win->surface.pixels) {
        window_draw_decoration(win);
    }

    // 将新窗口加入到 root 的子窗口链表尾部
    win->parent = root_window;
    list_add_tail(&win->sibling, &root_window->children);

    return win;
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
    graphics_surface_t* back_buffer = graphics_backbuffer();
    if (!back_buffer || !root_window) return;

    // 绘制纯色背景 (清屏)
    graphics_rect_t screen_rect = {0, 0, back_buffer->width, back_buffer->height};
    graphics_fill_rect(back_buffer, screen_rect, 0xFF336699);

    // 递归绘制窗口树
    // 如果 root_window 也有 surface，这行代码会把它画出来；如果没有 surface，它只负责遍历子节点
    draw_window_recursive(back_buffer, root_window, 0, 0);
    graphics_present(NULL, 0);
}

window_t* window_from_point(int x, int y) {
    if (!root_window) return NULL;

    window_t *win;

    list_for_each_entry_reverse(win, &root_window->children, sibling) {

        // 1. 检查窗口是否可见
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

    list_del(&win->sibling);
    list_add_tail(&win->sibling, &win->parent->children);

    // TODO: 脏矩形刷新
    // 目前没有实现脏矩形刷新，直接刷新整个窗口管理器
    window_manager_refresh();
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
    graphics_fill_rect(&win->surface, content_rect, win->bg_color);

    // 绘制标题栏
    graphics_rect_t title_rect = {
        2, 2,
        w - 4, WIN_TITLE_HEIGHT - 2
    };
    graphics_fill_rect(&win->surface, title_rect, C_TITLE_BG);

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
    int btn_size = WIN_TITLE_HEIGHT - 6;
    graphics_rect_t close_rect = {
        w - btn_size - 4,
        4,
        btn_size,
        btn_size
    };
    graphics_fill_rect(&win->surface, close_rect, C_CLOSE_BTN);

    // TODO: 绘制标题文本, 需要实现文字渲染功能
}

void window_manager_on_mouse(int x, int y, int buttons) {
    bool left_btn = (buttons & 1);

    if (left_btn) {
        // 按下鼠标左键
        if (!drag_window) {
            // 尝试捕捉窗口
            window_t* target = window_from_point(x, y);

            if (target && target != root_window) {
                drag_window = target;
                drag_off_x = x - target->x;
                drag_off_y = y - target->y;

                // 置顶选中窗口
                window_bring_to_front(target);
                window_manager_refresh();
            }
        } else {
            // 拖拽
            int new_x = x - drag_off_x;
            int new_y = y - drag_off_y;

            if (drag_window->x != new_x || drag_window->y != new_y) {
                drag_window->x = new_x;
                drag_window->y = new_y;
                window_manager_refresh();
            }
        }
    } else {
        // 松开左键
        if (drag_window) {
            drag_window = NULL;
        }
    }
}
