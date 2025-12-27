#ifndef _UNIOS_WINDOW_H
#define _UNIOS_WINDOW_H

#include <unios/graphics.h>
#include <lib/list.h>
#include <stdint.h>
#include <stdbool.h>

#define WIN_MAX_WINDOWS 256

// 窗口状态标志位
#define WIN_FLAG_VISIBLE     (1 << 0)
#define WIN_FLAG_FOCUSED     (1 << 1)
#define WIN_FLAG_DIRTY       (1 << 2) // 需要重绘
#define WIN_FLAG_TRANSPARENT (1 << 3) // 支持透明混合
#define WIN_FLAG_MAXIMIZED   (1 << 4)
#define WIN_FLAG_MINIMIZED   (1 << 5)

typedef struct window_s {
    int id;
    int owner_pid;

    int x, y, w, h;

    // 窗口内容缓冲
    graphics_surface_t surface;

    size_t buffer_size;
    void*  kernel_buffer;
    void*  user_buffer;

    // 窗口层级树
    struct window_s* parent;
    struct list_head children; // 子窗口链表
    struct list_head sibling;  // 兄弟窗口链表

    // 属性
    uint32_t flags;

} window_t;

typedef struct {
    int x, y, w, h;
} window_info_t;

void window_manager_handler(void);
/*!
 * \brief
 */
void window_mark_dirty();
/*!
 * \brief 标记窗口区域为脏区域
 */
void window_invalidate_rect(window_t* win, int x, int y, int w, int h);
/*!
 * \brief 初始化窗口管理器
 */
void init_window_manager();
/*!
 * \brief 创建一个新窗口
 */
window_t* create_window(int x, int y, int w, int h, int owner);
/*!
 * \brief 销毁指定窗口
 */
bool destroy_window(window_t* win);
/*!
 * \brief 刷新指定窗口（重绘该窗口及其子窗口）
 */
void window_refresh(window_t* win);
/*!
 * \brief 刷新整个窗口管理器（重绘所有窗口）
 */
void window_manager_refresh();
/*!
 * \brief 将指定窗口置于最前
 */
void window_bring_to_front(window_t* win);
/*!
 * \brief 返回指定坐标下的窗口指针
 */
window_t* window_from_point(int x, int y);
/*!
 * \brief 处理鼠标事件
 * \param x 鼠标当前的 X 坐标 (绝对坐标)
 * \param y 鼠标当前的 Y 坐标 (绝对坐标)
 * \param buttons 鼠标按键状态 (Bit 0: 左键, Bit 1: 右键, Bit 2: 中键)
 */
void window_manager_on_mouse(int x, int y, int buttons);
void window_manager_on_key(uint32_t key);
/*!
 * \brief 绘制内容 (目前仅填充背景色)
 */
bool window_fill(window_t* win, uint32_t color);
bool window_draw_recursive(
    graphics_surface_t* dest, window_t* win, int abs_x, int abs_y);
bool window_move_abs(window_t* win, int x, int y);

/* Syscall */

int do_get_root_window_handle(); //< 获取根窗口句柄

bool do_set_root_window_owner(int pid);
bool do_set_window_info(int handle, window_info_t* win_info); //< 获取窗口信息
bool do_set_window_surface_buffer(
    int handle, void** win_surface_buffer); //< 设置窗口的用户内存映射

int  do_open_window(int x, int y, int w, int h); //< 创建窗口, 返回窗口句柄
bool do_close_window(int handle);                //< 关闭窗口
bool do_refresh_window(int handle);              //< 刷新指定窗口
bool do_refresh_window_manager();                //< 刷新所有窗口

bool do_move_abs_window(int handle, int x, int y);

// TODO:

bool do_change_cursor(int style);
bool do_resize_window(int handle, int w, int h);

#endif
