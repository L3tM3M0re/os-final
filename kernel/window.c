#include <unios/window.h>
#include <unios/memory.h>
#include <unios/assert.h>
#include <unios/tracing.h>
#include <lib/string.h>
#include <lib/container_of.h>

static window_t* root_window = NULL;
static int win_id_counter = 0;

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
        // 初始填充背景色
        graphics_rect_t r = {0, 0, w, h};
        graphics_fill_rect(&win->surface, r, bg_color);
    }

    // 将新窗口加入到 root 的子窗口链表尾部
    win->parent = root_window;
    list_add_tail(&win->sibling, &root_window->children);

    return win;
}

void window_fill(window_t* win, uint32_t color) {
    if (!win) return;
    graphics_rect_t rect = {0, 0, win->w, win->h};
    graphics_fill_rect(&win->surface, rect, color);
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
