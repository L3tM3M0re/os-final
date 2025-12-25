#ifndef _UNIOS_WINDOW_H
#define _UNIOS_WINDOW_H

#include <unios/graphics.h>
#include <lib/list.h>
#include <stdint.h>
#include <stdbool.h>

#define WIN_TITLE_MAX 64

// 窗口状态标志位
#define WIN_FLAG_VISIBLE    (1 << 0)
#define WIN_FLAG_FOCUSED    (1 << 1)
#define WIN_FLAG_DIRTY      (1 << 2) // 需要重绘
#define WIN_FLAG_TRANSPARENT (1 << 3) // 支持透明混合

typedef struct window_s {
    int id;

    int x;
    int y;
    int w;
    int h;

    // 窗口内容缓冲
    graphics_surface_t surface;

    // 窗口层级树
    struct window_s *parent;
    struct list_head children;  // 子窗口链表
    struct list_head sibling;   // 兄弟窗口链表

    // 属性
    uint32_t flags;
    uint32_t bg_color;      // 背景色，如果 surface 内容未完全覆盖时使用
    char title[WIN_TITLE_MAX];

    // 暂时预留，未来可以扩展为 onClick, onKey 等
    void *user_data;

} window_t;

/*!
 * \brief 初始化窗口管理器
 */
void init_window_manager();
/*!
 * \brief 创建一个新窗口
 */
window_t* create_window(int x, int y, int w, int h, const char* title, uint32_t bg_color);
/*!
 * \brief 销毁指定窗口
 */
void destroy_window(window_t* win);
/*!
 * \brief 刷新指定窗口（重绘该窗口及其子窗口）
 */
void window_refresh(window_t* win);
/*!
 * \brief 刷新整个窗口管理器（重绘所有窗口）
 */
void window_manager_refresh();
/*!
 * \brief 绘制测试
 */
void window_fill(window_t* win, uint32_t color);
#endif
