#include <unios/window.h>
#include <unios/clock.h>
#include <unios/memory.h>
#include <unios/assert.h>
#include <unios/keyboard.h>
#include <unios/tracing.h>
#include <unios/proc.h>
#include <unios/schedule.h>
#include <unios/page.h>
#include <unios/ipc.h>
#include <lib/string.h>
#include <lib/syscall.h>
#include <lib/container_of.h>
#include <unios/font.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <arch/x86.h>

#define DISABLE_INTERRUPTS() __asm__ volatile("cli")
#define ENABLE_INTERRUPTS()  __asm__ volatile("sti")

#define MOUSE_IPC_INTERVAL 8

#define COLOR_BACK 0xFF336699

static window_t* handle_table[WIN_MAX_WINDOWS]; // 句柄表

static bool g_handled      = false;
static int  drag_start_mx  = 0;
static int  drag_start_my  = 0;
static bool is_drag_active = false;

static window_t* root_window = NULL;

static const graphics_rect_t EMPTY_RECT   = {0, 0, 0, 0};
static graphics_rect_t       g_dirty_rect = {0, 0, 0, 0};
static volatile bool         g_has_dirty  = false;

static volatile bool g_mouse_event_pending = false;
static int           g_cmd_x               = 0;
static int           g_cmd_y               = 0;
static int           g_cmd_buttons         = 0;

static int g_mouse_last_x = 0; // 记录上一帧的鼠标位置
static int g_mouse_last_y = 0;

static volatile bool g_key_event_pending = false;
static uint32_t      g_key_pressed       = 0;

static int min_int(int a, int b) {
    return a < b ? a : b;
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static inline uint32_t clamp_u32(uint32_t val, uint32_t min_v, uint32_t max_v) {
    if (val < min_v) { return min_v; }
    if (val > max_v) { return max_v; }
    return val;
}

static inline int clamp_int(int val, int min_v, int max_v) {
    if (val < min_v) { return min_v; }
    if (val > max_v) { return max_v; }
    return val;
}

void window_lock() {
    DISABLE_INTERRUPTS();
}

void window_unlock() {
    ENABLE_INTERRUPTS();
}

static bool check_window_handle(window_t* win, int handle) {
    if (handle < 0 || handle >= WIN_MAX_WINDOWS) { return false; }
    if (handle_table[handle] != win) { return false; }
    return true;
}

static bool set_window_by_handle(int handle, window_t** win) {
    if (handle < 0 || handle >= WIN_MAX_WINDOWS) { return false; }
    if (!handle_table[handle] || handle_table[handle]->id != handle) {
        return false;
    }
    *win = handle_table[handle];
    return true;
}

static bool try_create_window(window_t* win) {
    for (int i = 0; i < WIN_MAX_WINDOWS; ++i) {
        if (handle_table[i] == NULL) {
            handle_table[i] = win;
            win->id         = i;
            return true;
        }
    }
    return false;
}

static bool try_destroy_window(window_t* win) {
    int id = win->id;
    if (!check_window_handle(win, id)) { return false; }
    handle_table[id] = NULL;
    return true;
}

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
    g_has_dirty  = false;
}

void window_mark_dirty() {
    const graphics_mode_t* mode = graphics_current_mode();
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
    const graphics_mode_t* mode = graphics_current_mode();
    if (!mode) { return; }

    if (abs_x < 0) {
        fix_w += abs_x;
        abs_x  = 0;
    }
    if (abs_y < 0) {
        fix_h += abs_y;
        abs_y  = 0;
    }

    if (abs_x + fix_w > mode->width) fix_w = mode->width - abs_x;
    if (abs_y + fix_h > mode->height) fix_h = mode->height - abs_y;

    if (fix_w <= 0 || fix_h <= 0) return;

    graphics_rect_t new_rect = {abs_x, abs_y, fix_w, fix_h};
    // 合并到全局脏区域
    if (!g_has_dirty) {
        g_dirty_rect = new_rect;
        g_has_dirty  = true;
    } else {
        g_dirty_rect = rect_union(g_dirty_rect, new_rect);
    }
}

void window_manager_handler(void) {
    int  last_refresh_tick   = 0;
    int  last_mouse_ipc_tick = 0;
    int  last_sent_buttons   = 0;
    int  pending_mx = 0, pending_my = 0, pending_btns = 0;
    int  current_tick      = 0;
    bool has_pending_mouse = false;

    while (true) {
        current_tick = system_ticks;
        if (g_key_event_pending) {
            window_lock();
            uint32_t key        = g_key_pressed;
            g_key_event_pending = false;
            window_unlock();

            // 获取当前聚焦的窗口
            // 简单起见，这里假设你有一个 focused_window 变量，
            // 或者直接发给 win1
            // (为了测试，我们暂时发给鼠标最后悬停的窗口，或者遍历找)
            // 这里我们演示：发给 "最顶层的活动窗口" (handle_table里的某一个)
            // 但为了让你必定能收到，我们先发给 target_win (这里你需要定义
            // target_win 指向谁)

            // 【临时方案】：直接发给所有有 owner 的窗口，或者你指定的 win1 的
            // owner 为了严谨，建议维护一个 static window_t* g_focused_window;
            // 这里假设发给 root_window 的 owner (即 gui_server)
            // 或者你在 create_window 时记录了 win1

            // 假设我们发给 root_window (因为 gui_server 是 root
            // 的创建者/管理者)
            if (root_window && root_window->owner_pid != -1) {
                message_t msg;
                memset(&msg, 0, sizeof(message_t));
                msg.type = MSG_GUI_KEY; // 确保在 ipc.h 定义了 (例如 101)
                msg.u1   = key;         // 将按键码放入 u1

                // 发送消息
                sendrec(SEND, root_window->owner_pid, &msg);
            }
        }

        if (g_mouse_event_pending) {
            window_lock();
            pending_mx            = g_cmd_x;
            pending_my            = g_cmd_y;
            pending_btns          = g_cmd_buttons;
            g_mouse_event_pending = false;
            window_unlock();

            has_pending_mouse = true;
        }

        if (has_pending_mouse) {
            if (current_tick - last_mouse_ipc_tick >= MOUSE_IPC_INTERVAL
                || pending_btns != last_sent_buttons) {
                window_t* target_win =
                    window_from_point(pending_mx, pending_my);
                if (target_win != NULL && target_win->owner_pid != -1) {
                    message_t msg;
                    memset(&msg, 0, sizeof(message_t));
                    msg.type = MSG_GUI_MOUSE_EVENT;
                    msg.u1   = pending_mx - target_win->x;
                    msg.u2   = pending_my - target_win->y;
                    msg.u3   = pending_btns;

                    sendrec(SEND, target_win->owner_pid, &msg);
                }
                last_mouse_ipc_tick = current_tick;
                last_sent_buttons   = pending_btns;
                has_pending_mouse   = false;
            }
        }

        if (g_has_dirty) {
            if (current_tick - last_refresh_tick >= 16) {
                window_manager_refresh();
                last_refresh_tick = current_tick;
            }
        }

        yield();
    }
}

void init_window_manager() {
    const graphics_mode_t* mode = graphics_current_mode();
    if (!mode) return;

    root_window = kmalloc(sizeof(window_t));
    memset(root_window, 0, sizeof(window_t));

    if (!try_create_window(root_window)) {
        kerror("Failed to create root window");
        return;
    }

    root_window->x     = 0;
    root_window->y     = 0;
    root_window->w     = mode->width;
    root_window->h     = mode->height;
    root_window->flags = WIN_FLAG_VISIBLE;

    root_window->owner_pid = -1;

    window_lock();
    INIT_LIST_HEAD(&root_window->children);
    INIT_LIST_HEAD(&root_window->sibling);
    window_unlock();

    window_fill(root_window, 0xFFFFFF);
    window_mark_dirty();

    kinfo("Window Manager initialized");
}

window_t* create_window(int x, int y, int w, int h, int owner) {
    const graphics_mode_t* mode = graphics_current_mode();
    if (!mode) return NULL;
    if (!root_window) return NULL;

    window_t* win = kmalloc(sizeof(window_t));
    if (!win) return NULL;
    memset(win, 0, sizeof(window_t));

    if (!try_create_window(win)) {
        kfree(win);
        return NULL;
    }

    win->x = x;
    win->y = y;
    win->w = clamp_int(w, 0, mode->width);
    win->h = clamp_int(h, 0, mode->height);

    win->flags = WIN_FLAG_VISIBLE;

    win->owner_pid = owner;

    window_lock();
    INIT_LIST_HEAD(&win->children);
    INIT_LIST_HEAD(&win->sibling);
    window_unlock();

    // 初始化 Surface
    win->surface.width  = win->w;
    win->surface.height = win->h;
    win->surface.bpp    = 32;
    win->surface.pitch  = win->w * 4;
    win->surface.size   = (size_t)win->w * win->h * 4;
    win->surface.owns   = true;
    win->surface.pixels = kmalloc(win->surface.size + PAGE_SIZE);

    // 将新窗口加入到 root 的子窗口链表尾部
    win->parent = root_window;

    window_lock();
    list_add_tail(&win->sibling, &root_window->children);
    window_unlock();

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    return win;
}

bool destroy_window(window_t* win) {
    if (!win || win == root_window) { return false; }
    if (!try_destroy_window(win)) { return false; }

    window_lock();
    list_del(&win->sibling);
    window_unlock();

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    // 释放资源
    if (win->surface.pixels && win->surface.owns) {
        kfree(win->surface.pixels);
        win->surface.pixels = NULL;
    }

    kfree(win);
}

bool window_move_abs(window_t* win, int x, int y) {
    if (!win || win == root_window) { return false; }

    if (win->x == x && win->y == y) { return true; }

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
    win->x = x;
    win->y = y;
    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);

    return true;
}

bool window_fill(window_t* win, uint32_t color) {
    if (!win) { return false; }
    if (win->w < 0 || win->h < 0) { return false; }

    graphics_rect_t rect = {
        0,
        0,
        win->w,
        win->h,
    };
    graphics_fill_rect(&win->surface, rect, color);

    return true;
}

bool window_draw_recursive(
    graphics_surface_t* dest, window_t* win, int abs_x, int abs_y) {
    if (!(win->flags & WIN_FLAG_VISIBLE)) return true;

    // 计算当前窗口在屏幕上的绝对坐标
    int cur_x = abs_x + win->x;
    int cur_y = abs_y + win->y;

    // 绘制当前窗口
    if (win->surface.pixels) {
        graphics_blit(dest, cur_x, cur_y, &win->surface, NULL);
    }

    // 绘制子窗口
    struct list_head* pos;

    list_for_each(pos, &win->children) {
        window_t* child = container_of(pos, window_t, sibling);
        if (!window_draw_recursive(dest, child, cur_x, cur_y)) { return false; }
    }

    return true;
}

void window_manager_refresh() {
    if (!g_has_dirty) return;

    graphics_rect_t dirty = g_dirty_rect;

    reset_dirty_rect();

    graphics_surface_t* back_buffer = graphics_backbuffer();
    if (!back_buffer || !root_window) return;

    graphics_set_clip_rect(dirty.x, dirty.y, dirty.w, dirty.h);

    graphics_fill_rect(back_buffer, dirty, COLOR_BACK);

    // 绘制窗口
    window_lock();
    window_draw_recursive(back_buffer, root_window, 0, 0);
    window_unlock();

    graphics_set_clip_rect(0, 0, back_buffer->width, back_buffer->height);

    graphics_present(&dirty, 1);
}

window_t* window_from_point(int x, int y) {
    if (!root_window) return NULL;

    window_t* win;

    window_lock();
    list_for_each_entry_reverse(win, &root_window->children, sibling) {
        // 检查窗口是否可见
        if (!(win->flags & WIN_FLAG_VISIBLE)) continue;

        if (x >= win->x && x < win->x + win->w && y >= win->y
            && y < win->y + win->h) {
            return win;
        }
    }
    window_unlock();

    return root_window;
}

void window_bring_to_front(window_t* win) {
    if (!win || !win->parent) { return; }
    if (win == root_window) { return; }
    struct list_head* head = &win->parent->children;

    window_lock();
    if (list_is_last(&win->sibling, head)) {
        window_unlock();
        return;
    }
    list_move_tail(&win->sibling, head);
    window_unlock();

    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
}

void window_manager_on_mouse(int x, int y, int buttons) {
    g_cmd_x               = x;
    g_cmd_y               = y;
    g_cmd_buttons         = buttons;
    g_mouse_event_pending = true;
}

void window_manager_on_key(uint32_t key) {
    g_key_pressed       = key;
    g_key_event_pending = true;
}

void window_refresh(window_t* win) {
    if (!win) { return; }
    window_invalidate_rect(root_window, win->x, win->y, win->w, win->h);
}

/* Syscall */

int do_get_root_window_handle() {
    if (!root_window) { return -1; }
    int id = root_window->id;
    if (!check_window_handle(root_window, id)) { return -1; }
    return id;
}

int do_open_window(int x, int y, int w, int h) {
    int       current_pid = proc2pid(p_proc_current);
    window_t* win         = create_window(x, y, w, h, current_pid);

    if (!win) { return -1; }
    int id = win->id;
    if (!check_window_handle(win, id)) {
        destroy_window(win);
        return -1;
    }

    return id;
}

bool do_close_window(int handle) {
    window_t* win;
    if (!set_window_by_handle(handle, &win)) { return false; }
    destroy_window(win);
    return true;
}

bool do_refresh_window(int handle) {
    window_t* win;
    if (!set_window_by_handle(handle, &win)) { return false; }
    window_refresh(win);
    return true;
}

bool do_refresh_window_manager() {
    window_manager_refresh();
    return true;
}

bool do_set_window_surface_buffer(int handle, void** win_surface_buffer) {
    window_t* win;
    if (!set_window_by_handle(handle, &win)) { return false; }

    // owner_pid 判断

    if (win->user_buffer != NULL) {
        *win_surface_buffer = win->user_buffer;
        return true;
    }

    pcb_t* pcb = &p_proc_current->pcb;

    uint32_t k_addr_raw   = (uint32_t)win->surface.pixels;
    uint32_t k_page_start = k_addr_raw & ~(PAGE_SIZE - 1);
    uint32_t offset       = k_addr_raw - k_page_start;

    size_t total_size = win->surface.size + offset;
    size_t pages      = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    lock_or(&pcb->heap_lock, sched);

    void* u_ptr_base = mballoc_alloc(pcb->allocator, pages * PAGE_SIZE);
    release(&pcb->heap_lock);

    if (u_ptr_base == NULL) return false;

    uint32_t cr3 = pcb->cr3;

    uint32_t flags = PG_P | PG_U | PG_RWX;

    for (size_t i = 0; i < pages; ++i) {
        uint32_t k_laddr = k_page_start + i * PAGE_SIZE;
        uint32_t u_laddr = (uint32_t)u_ptr_base + i * PAGE_SIZE;

        uint32_t phy_addr = pg_laddr_phyaddr(cr3, k_laddr);

        if (!pg_map_laddr(cr3, u_laddr, phy_addr, flags, flags)) {
            kerror("Failed to map window buffer page");
            return false;
        }
    }

    pg_refresh();

    void* u_ptr_final = (void*)((uint32_t)u_ptr_base + offset);

    win->kernel_buffer  = (void*)k_addr_raw;
    win->user_buffer    = u_ptr_final;
    *win_surface_buffer = u_ptr_final;
    return true;
}

bool do_set_root_window_owner(int pid) {
    if (!root_window) { return false; }
    root_window->owner_pid = pid;
    return true;
}

bool do_set_window_info(int handle, window_info_t* win_info) {
    window_t* win;

    if (!set_window_by_handle(handle, &win)) {
        win_info->x = -1;
        win_info->y = -1;
        win_info->w = -1;
        win_info->h = -1;
    } else {
        win_info->x = win->x;
        win_info->y = win->y;
        win_info->w = win->w;
        win_info->h = win->h;
    }

    return win_info;
}

bool do_move_abs_window(int handle, int x, int y) {
    window_t* win;
    if (!set_window_by_handle(handle, &win)) { return false; }
    window_move_abs(win, x, y);
    return true;
}
