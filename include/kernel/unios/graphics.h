#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h> // IWYU pragma: keep

typedef struct graphics_mode_s {
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    uint32_t pitch;
    uintptr_t lfb_phy;
    void *lfb_lin;
    size_t lfb_size;
} graphics_mode_t;

typedef struct graphics_rect_s {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} graphics_rect_t;

typedef struct graphics_surface_s {
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    uint32_t pitch;
    void *pixels;
    size_t size;
    bool owns;
} graphics_surface_t;

/*!
 * \brief 通用 Blit 函数
 * \param dst 目标 Surface
 * \param dx 目标位置 X
 * \param dy 目标位置 Y
 * \param src 源 Surface
 * \param src_area 源区域 (NULL 表示拷贝整个源 Surface)
 */
void graphics_blit(graphics_surface_t *dst, int dx, int dy,
                   const graphics_surface_t *src, const graphics_rect_t *src_area);
/*!
 * \brief 将 LFB (线性帧缓冲区) 映射到指定进程的页表中
 *
 * \param cr3 目标进程页目录的物理地址 (CR3 寄存器值)
 *
 * \note 此函数用于确保在不同进程上下文（如中断处理）中也能访问显存，防止 Page Fault
 */
void graphics_map_lfb(uint32_t cr3);
/*!
 * \brief 初始化 bochs VBE LFB 并绘制简单测试图案
 *
 * \return true 表示初始化成功且已绘制测试图案
 */
bool graphics_boot_demo(void);

/*!
 * \brief 获取当前 LFB 模式信息
 *
 * \return 已初始化则返回信息指针，否则返回 NULL
 */
const graphics_mode_t *graphics_current_mode(void);

/*!
 * \brief 获取前缓冲（LFB）surface
 */
graphics_surface_t *graphics_frontbuffer(void);

/*!
 * \brief 获取后备缓冲 surface（系统分配的 RAM）
 */
graphics_surface_t *graphics_backbuffer(void);

/*!
 * \brief 将后备缓冲内容拷贝到前缓冲
 *
 * \param rects 指定脏矩形数组，若为 NULL 或 count==0 则全屏拷贝
 */
bool graphics_present(const graphics_rect_t *rects, size_t count);

/*!
 * \brief 在 surface 上填充矩形（仅支持 32bpp）
 */
void graphics_fill_rect(
    graphics_surface_t *surf, graphics_rect_t rect, uint32_t argb);

/*!
 * \brief 初始化并显示鼠标光标，内部在首个 graphics_boot_demo 成功后调用
 */
void graphics_cursor_init(void);

/*!
 * \brief 相对移动鼠标光标，支持负值
 * 不执行真正绘制工作
 */
void graphics_cursor_move(int dx, int dy);

/*!
 * \brief 设置鼠标光标绝对位置
 */
void graphics_cursor_set(int x, int y);

/*!
 * \brief 绘制光标
 */
void graphics_cursor_render(void);
