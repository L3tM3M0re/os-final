#include <unios/graphics.h>
#include <unios/page.h>
#include <unios/tracing.h>
#include <unios/memory.h>
#include <arch/x86.h>
#include <config.h>
#include <unios/layout.h>
#include <math.h>
#include <string.h>

#define BGA_IOPORT_INDEX 0x01ce
#define BGA_IOPORT_DATA  0x01cf

#define BGA_REG_ID             0x0
#define BGA_REG_XRES           0x1
#define BGA_REG_YRES           0x2
#define BGA_REG_BPP            0x3
#define BGA_REG_ENABLE         0x4
#define BGA_REG_BANK           0x5
#define BGA_REG_VIRT_WIDTH     0x6
#define BGA_REG_VIRT_HEIGHT    0x7
#define BGA_REG_X_OFFSET       0x8
#define BGA_REG_Y_OFFSET       0x9
#define BGA_REG_VIDEO_MEMORYKB 0xa

#define BGA_ID0 0xB0C0
#define BGA_ID1 0xB0C1
#define BGA_ID2 0xB0C2
#define BGA_ID3 0xB0C3
#define BGA_ID4 0xB0C4

#define BGA_ENABLED     0x01
#define BGA_LFB_ENABLED 0x40
#define BGA_NOCLEARMEM  0x80

//! fallback LFB base if PCI 探测失败
#define BGA_LFB_PHY_BASE_DEFAULT 0xE0000000u
//! choose a kernel virtual window far from direct-mapped low memory
#define BGA_LFB_LIN_BASE 0xE0000000u

static graphics_mode_t g_mode;
static graphics_surface_t g_front;
static graphics_surface_t g_back;
static bool            g_ready = false;

static uint32_t g_cursor_bg_backup[64 * 64];

typedef struct {
    bool ready;
    bool drawn;
    int  x;
    int  y;
    int  prev_x;
    int  prev_y;
} cursor_state_t;

static cursor_state_t g_cursor;

static uint32_t       g_cursor_pixels[16 * 16];
static graphics_surface_t g_cursor_sprite;

static inline void bga_write(uint16_t reg, uint16_t value) {
    outw(BGA_IOPORT_INDEX, reg);
    outw(BGA_IOPORT_DATA, value);
}

static inline uint16_t bga_read(uint16_t reg) {
    outw(BGA_IOPORT_INDEX, reg);
    return inw(BGA_IOPORT_DATA);
}

static bool bga_available() {
    bga_write(BGA_REG_ID, BGA_ID4);
    uint16_t id = bga_read(BGA_REG_ID);
    return (id & 0xFFF0) == (BGA_ID0 & 0xFFF0);
}

static inline uint32_t pci_cfg_read32(
    uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000u | (bus << 16) | (dev << 11)
                    | (func << 8) | (offset & 0xfc);
    outl(0xcf8, addr);
    return inl(0xcfc);
}

static uintptr_t find_bochs_lfb_base() {
    const uint16_t vendor = 0x1234;
    const uint16_t device = 0x1111; // qemu stdvga/bochs-display

    for (uint8_t dev = 0; dev < 32; ++dev) {
        uint32_t vdid = pci_cfg_read32(0, dev, 0, 0x00);
        if (vdid == 0xffffffffu) { continue; }
        if ((vdid & 0xffffu) != vendor) { continue; }
        if ((vdid >> 16) != device) { continue; }

        uint32_t bar0 = pci_cfg_read32(0, dev, 0, 0x10);
        if (bar0 & 0x1) { continue; } // io bar, skip
        uintptr_t base = bar0 & ~0xFu;
        if ((bar0 & 0x6) == 0x4) {
            uint32_t bar1 = pci_cfg_read32(0, dev, 0, 0x14);
            base |= ((uint64_t)bar1) << 32;
        }
        return base;
    }
    return 0;
}

static bool bga_set_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    bga_write(BGA_REG_ENABLE, 0);
    bga_write(BGA_REG_ID, BGA_ID4);
    bga_write(BGA_REG_XRES, width);
    bga_write(BGA_REG_YRES, height);
    bga_write(BGA_REG_BPP, bpp);
    bga_write(BGA_REG_VIRT_WIDTH, width);
    bga_write(BGA_REG_VIRT_HEIGHT, height);
    bga_write(BGA_REG_BANK, 0);
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(
        BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED | BGA_NOCLEARMEM);

    return bga_read(BGA_REG_XRES) == width
           && bga_read(BGA_REG_YRES) == height
           && bga_read(BGA_REG_BPP) == bpp;
}

static bool map_lfb(uintptr_t phy_base, size_t size, uintptr_t lin_base) {
    uint32_t cr3      = rcr3();
    size_t   mapped   = 0;
    bool     succeed  = true;
    //! mark LFB uncachable to force writes reaching device
    uint32_t pde_attr = PG_P | PG_S | PG_RWX | PG_MASK_PWT | PG_MASK_PCD;
    uint32_t pte_attr = PG_P | PG_S | PG_RWX | PG_MASK_PWT | PG_MASK_PCD;

    while (mapped < size) {
        bool ok = pg_map_laddr(
            cr3, lin_base + mapped, phy_base + mapped, pde_attr, pte_attr);
        if (!ok) {
            succeed = false;
            break;
        }
        mapped += NUM_4K;
    }
    pg_refresh();
    return succeed;
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
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

static graphics_rect_t clamp_rect(
    const graphics_surface_t *surf, graphics_rect_t rect) {
    if (rect.x >= surf->width || rect.y >= surf->height) {
        graphics_rect_t empty = {0};
        return empty;
    }
    uint32_t max_w =
        rect.x + rect.w > surf->width ? surf->width - rect.x : rect.w;
    uint32_t max_h =
        rect.y + rect.h > surf->height ? surf->height - rect.y : rect.h;
    graphics_rect_t r = {
        .x = rect.x,
        .y = rect.y,
        .w = (uint16_t)max_w,
        .h = (uint16_t)max_h,
    };
    return r;
}

// 辅助函数: 求两个矩形的交集
static bool intersect_rect(graphics_rect_t *r1, const graphics_rect_t *r2) {
    int x1 = max(r1->x, r2->x);
    int y1 = max(r1->y, r2->y);
    int x2 = min(r1->x + r1->w, r2->x + r2->w);
    int y2 = min(r1->y + r1->h, r2->y + r2->h);

    if (x2 > x1 && y2 > y1) {
        r1->x = x1;
        r1->y = y1;
        r1->w = x2 - x1;
        r1->h = y2 - y1;
        return true;
    }
    return false;
}

void graphics_blit(graphics_surface_t *dst, int dx, int dy,
                   const graphics_surface_t *src, const graphics_rect_t *src_area) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    // 确定源矩形
    graphics_rect_t s_rect;
    if (src_area) {
        s_rect = *src_area;
    } else {
        s_rect.x = 0; s_rect.y = 0;
        s_rect.w = src->width; s_rect.h = src->height;
    }

    // 初始目标矩形
    graphics_rect_t d_rect = {
        .x = dx, .y = dy, .w = s_rect.w, .h = s_rect.h
    };

    // 裁剪目标矩形
    graphics_rect_t dst_bounds = {0, 0, dst->width, dst->height};

    int final_dst_x = dx;
    int final_dst_y = dy;
    int final_src_x = s_rect.x;
    int final_src_y = s_rect.y;
    int final_w     = s_rect.w;
    int final_h     = s_rect.h;

    // 左边界裁剪
    if (final_dst_x < 0) {
        final_w += final_dst_x;
        final_src_x -= final_dst_x;
        final_dst_x = 0;
    }
    // 上边界裁剪
    if (final_dst_y < 0) {
        final_h += final_dst_y;
        final_src_y -= final_dst_y;
        final_dst_y = 0;
    }
    // 右边界裁剪
    if (final_dst_x + final_w > dst->width) {
        final_w = dst->width - final_dst_x;
    }
    // 下边界裁剪
    if (final_dst_y + final_h > dst->height) {
        final_h = dst->height - final_dst_y;
    }

    // 源边界越界检查 (防止 src_area 指定了超出 src 范围的区域)
    if (final_src_x + final_w > src->width) {
        final_w = src->width - final_src_x;
    }
    if (final_src_y + final_h > src->height) {
        final_h = src->height - final_src_y;
    }

    if (final_w <= 0 || final_h <= 0) return;

    // 执行内存拷贝
    uint32_t bpp_stride = src->bpp / 8;
    uint8_t *dst_base = (uint8_t *)dst->pixels + (final_dst_y * dst->pitch) + (final_dst_x * bpp_stride);
    const uint8_t *src_base = (const uint8_t *)src->pixels + (final_src_y * src->pitch) + (final_src_x * bpp_stride);

    for (int i = 0; i < final_h; ++i) {
        memcpy(dst_base, src_base, final_w * bpp_stride);
        dst_base += dst->pitch;
        src_base += src->pitch;
    }
}

void graphics_fill_rect(
    graphics_surface_t *surf, graphics_rect_t rect, uint32_t argb) {
    if (surf == NULL || surf->pixels == NULL) { return; }
    if (surf->bpp != 32) { return; }
    rect = clamp_rect(surf, rect);
    if (rect.w == 0 || rect.h == 0) { return; }

    uint32_t *base = surf->pixels;
    uint32_t  stride = surf->pitch / 4;
    for (uint32_t y = 0; y < rect.h; ++y) {
        uint32_t *row = base + (rect.y + y) * stride + rect.x;
        for (uint32_t x = 0; x < rect.w; ++x) {
            row[x] = argb;
        }
    }
}

static void cursor_build_bitmap() {
    // static const char *rows[16] = {
    //     "X...............",
    //     "XX..............",
    //     "XWX.............",
    //     "XWWW............",
    //     "XWWWW...........",
    //     "XWWWWW..........",
    //     "XWWWWWW.........",
    //     "XWWWWWXXX.......",
    //     "XWWW..X.........",
    //     "XWW...X.........",
    //     "XW....X.........",
    //     "X.....X.........",
    //     "X.....X.........",
    //     ".......X........",
    //     ".......X........",
    //     ".......X........",
    // };
    // for (int y = 0; y < 16; ++y) {
    //     for (int x = 0; x < 16; ++x) {
    //         char     c   = rows[y][x];
    //         uint32_t pix = 0x00000000;
    //         if (c == 'X') {
    //             pix = 0xFF000000;
    //         } else if (c == 'W') {
    //             pix = 0xFFFFFFFF;
    //         }
    //         g_cursor_pixels[y * 16 + x] = pix;
    //     }
    // }

    static const char *rows[16] = {
        "AA..............",
        "ACAA............",
        "ACCBAA..........",
        "ABCDCBAA........",
        "ABCCDDCA........",
        "ABBCCCA.........",
        "ABBBBA..........",
        "ABAABCA.........",
        "AA..ABBA........",
        ".....AAA........",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    };
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            char     c   = rows[y][x];
            uint32_t pix = 0x00000000;
            if(c == 'A') {
                pix = 0xff452a1b;
            } else if (c == 'B') {
                pix = 0xff543721;
            } else if (c == 'C') {
                pix = 0xff543721;
            } else if (c == 'D') {
                pix = 0xffaf7244;
            }
            g_cursor_pixels[y * 16 + x] = pix;
        }
    }

    g_cursor_sprite.width  = 16;
    g_cursor_sprite.height = 16;
    g_cursor_sprite.bpp    = 32;
    g_cursor_sprite.pitch  = 16 * 4;
    g_cursor_sprite.pixels = g_cursor_pixels;
    g_cursor_sprite.size   = sizeof(g_cursor_pixels);
    g_cursor_sprite.owns   = false;
}

void graphics_cursor_restore_prev() {
    if (!g_ready) return;
    if (!g_cursor.drawn) { return; }

    graphics_rect_t rect = {
        (uint16_t)g_cursor.prev_x,
        (uint16_t)g_cursor.prev_y,
        g_cursor_sprite.width,
        g_cursor_sprite.height,
    };
    graphics_blit(&g_front, rect.x, rect.y, &g_back, &rect);
}

void graphics_cursor_store_prev() {
    if (!g_ready) return;

    g_cursor.drawn  = true;
    g_cursor.prev_x = g_cursor.x;
    g_cursor.prev_y = g_cursor.y;
}

static void cursor_redraw() {
    if (!g_cursor.ready) return;

    graphics_cursor_restore_prev();
    graphics_cursor_draw_to(&g_front, g_cursor.x, g_cursor.y);

    graphics_cursor_store_prev();
}

void graphics_map_lfb(uint32_t cr3) {
    if (!g_ready) { return; }

    size_t   size_pages = round_up(g_mode.lfb_size, NUM_4K);
    // 显存需要设置为不可缓存 (PWT | PCD) 以确保写入立即生效
    uint32_t attr = PG_P | PG_S | PG_RWX | PG_MASK_PWT | PG_MASK_PCD;

    uintptr_t lin_base = (uintptr_t)g_mode.lfb_lin;
    uintptr_t phy_base = g_mode.lfb_phy;
    size_t    mapped   = 0;

    while (mapped < size_pages) {
        pg_map_laddr(cr3, lin_base + mapped, phy_base + mapped, attr, attr);
        mapped += NUM_4K;
    }
}

void graphics_cursor_set(int x, int y) {
    if (!g_ready) { return; }
    g_cursor.x = x;
    g_cursor.y = y;
    cursor_redraw();
}

void graphics_cursor_move(int dx, int dy) {
    if (!g_ready) { return; }

    int new_x = g_cursor.x + dx;
    int new_y = g_cursor.y + dy;
    int max_x = (int)g_front.width - 1;
    int max_y = (int)g_front.height - 1;

    g_cursor.x = clamp_int(new_x, 0, max_x);
    g_cursor.y = clamp_int(new_y, 0, max_y);

    graphics_cursor_render();
}

void graphics_cursor_render(void) {
    if (!g_ready || !g_cursor.ready) { return; }

    if (g_cursor.x == g_cursor.prev_x && g_cursor.y == g_cursor.prev_y) {
        return;
    }

    cursor_redraw();
}

void graphics_cursor_init(void) {
    if (!g_ready) { return; }
    if (g_cursor.ready) { return; }
    cursor_build_bitmap();
    g_cursor.ready  = true;
    g_cursor.drawn  = false;
    g_cursor.x      = g_front.width / 2;
    g_cursor.y      = g_front.height / 2;
    g_cursor.prev_x = g_cursor.prev_y = 0;
    cursor_redraw();
}

void graphics_get_cursor_pos(int *x, int *y) {
    if (g_ready) {
        *x = g_cursor.x;
        *y = g_cursor.y;
    } else {
        *x = 0;
        *y = 0;
    }
}

static void draw_demo_pattern(graphics_surface_t *surf) {
    if (surf == NULL || surf->pixels == NULL) { return; }
    uint32_t *fb     = surf->pixels;
    uint32_t  stride = surf->pitch / 4;
    uint32_t  sum_hw = max(1u, (uint32_t)surf->width + surf->height);
    uint32_t  safe_h = max(1u, (uint32_t)surf->height);
    uint32_t  safe_w = max(1u, (uint32_t)surf->width);

    for (uint32_t y = 0; y < surf->height; ++y) {
        uint8_t g = (uint8_t)((y * 255) / safe_h);
        for (uint32_t x = 0; x < surf->width; ++x) {
            uint8_t r = (uint8_t)((x * 255) / safe_w);
            uint8_t b = (uint8_t)(((x + y) * 255) / sum_hw);
            fb[y * stride + x] = pack_rgb(r, g, b);
        }
    }
}

const graphics_mode_t *graphics_current_mode(void) {
    return g_ready ? &g_mode : NULL;
}

graphics_surface_t *graphics_frontbuffer(void) {
    return g_ready ? &g_front : NULL;
}

graphics_surface_t *graphics_backbuffer(void) {
    return g_ready ? &g_back : NULL;
}

static void _save_cursor_bg(int x, int y) {
    if (!g_back.pixels) return;
    int w = g_cursor_sprite.width;
    int h = g_cursor_sprite.height;

    int bx = x, by = y, bw = w, bh = h;
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx + bw > g_back.width)  bw = g_back.width - bx;
    if (by + bh > g_back.height) bh = g_back.height - by;
    if (bw <= 0 || bh <= 0) return;

    uint32_t *src = (uint32_t *)g_back.pixels;
    int pitch = g_back.pitch / 4;
    for (int i = 0; i < bh; ++i) {
        memcpy(&g_cursor_bg_backup[i * w], &src[(by + i) * pitch + bx], bw * 4);
    }
}

static void _restore_cursor_bg(int x, int y) {
    if (!g_back.pixels) return;
    int w = g_cursor_sprite.width;
    int h = g_cursor_sprite.height;

    int bx = x, by = y, bw = w, bh = h;
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx + bw > g_back.width)  bw = g_back.width - bx;
    if (by + bh > g_back.height) bh = g_back.height - by;
    if (bw <= 0 || bh <= 0) return;

    uint32_t *dst = (uint32_t *)g_back.pixels;
    int pitch = g_back.pitch / 4;
    for (int i = 0; i < bh; ++i) {
        memcpy(&dst[(by + i) * pitch + bx], &g_cursor_bg_backup[i * w], bw * 4);
    }
}

bool graphics_present(const graphics_rect_t *rects, size_t count) {
    if (g_front.pixels == NULL || g_back.pixels == NULL) { return false; }

    bool cursor_active = g_cursor.ready;
    int cx = g_cursor.x;
    int cy = g_cursor.y;

    if (cursor_active) {
        _save_cursor_bg(cx, cy);
        graphics_cursor_draw_to(&g_back, cx, cy);
    }

    if (rects == NULL || count == 0) {
        graphics_blit(&g_front, 0, 0, &g_back, NULL);
    } else {
        for (size_t i = 0; i < count; ++i) {
            graphics_blit(&g_front, rects[i].x, rects[i].y, &g_back, &rects[i]);
        }
    }

    if (cursor_active) {
        _restore_cursor_bg(cx, cy);
        graphics_cursor_store_prev();
    }

    return true;
}

bool graphics_boot_demo(void) {
    if (!CONFIG_GRAPHICS_BOOT_DEMO) { return false; }
    if (g_ready) { return true; }
    if (CONFIG_GRAPHICS_BPP != 32) {
        kwarn("graphics: only 32bpp is supported currently");
        return false;
    }
    if (!bga_available()) {
        kwarn("graphics: bochs-display not detected, skip graphics init");
        return false;
    }

    size_t bytes_per_pixel = CONFIG_GRAPHICS_BPP / 8;
    size_t fb_size =
        (size_t)CONFIG_GRAPHICS_WIDTH * CONFIG_GRAPHICS_HEIGHT * bytes_per_pixel;
    size_t fb_size_pages = round_up(fb_size, NUM_4K);

    uint32_t vram_blocks = bga_read(BGA_REG_VIDEO_MEMORYKB);
    size_t   vram_bytes  = (size_t)vram_blocks * 64 * 1024;
    if (vram_bytes < fb_size) {
        kwarn(
            "graphics: expected LFB >= %zu bytes, only %zu bytes available",
            fb_size,
            vram_bytes);
        return false;
    }

    bool ok = bga_set_mode(CONFIG_GRAPHICS_WIDTH, CONFIG_GRAPHICS_HEIGHT, 32);
    if (!ok) {
        kwarn(
            "graphics: set mode %dx%dx%d failed",
            CONFIG_GRAPHICS_WIDTH,
            CONFIG_GRAPHICS_HEIGHT,
            CONFIG_GRAPHICS_BPP);
        return false;
    }

    uintptr_t lfb_phy = find_bochs_lfb_base();
    if (lfb_phy == 0) {
        lfb_phy = BGA_LFB_PHY_BASE_DEFAULT;
        kwarn(
            "graphics: pci probe failed, fallback lfb base %#x",
            (uint32_t)lfb_phy);
    }

    ok = map_lfb(lfb_phy, fb_size_pages, BGA_LFB_LIN_BASE);
    if (!ok) {
        kwarn("graphics: map LFB failed");
        return false;
    }

    //! quick self-test: write & read back first pixel
    volatile uint32_t *fb_test = (uint32_t *)BGA_LFB_LIN_BASE;
    uint32_t            probe  = 0x00ff00ff;
    fb_test[0]                 = probe;
    if (fb_test[0] != probe) {
        kwarn(
            "graphics: LFB write test failed at fb=%#x, abort demo",
            (uint32_t)lfb_phy);
        return false;
    }

    //! setup surface objects
    memset(&g_mode, 0, sizeof(g_mode));
    g_mode.width    = CONFIG_GRAPHICS_WIDTH;
    g_mode.height   = CONFIG_GRAPHICS_HEIGHT;
    g_mode.bpp      = CONFIG_GRAPHICS_BPP;
    g_mode.pitch    = CONFIG_GRAPHICS_WIDTH * bytes_per_pixel;
    g_mode.lfb_phy  = lfb_phy;
    g_mode.lfb_lin  = (void *)BGA_LFB_LIN_BASE;
    g_mode.lfb_size = fb_size;

    memset(&g_front, 0, sizeof(g_front));
    g_front.width  = g_mode.width;
    g_front.height = g_mode.height;
    g_front.bpp    = g_mode.bpp;
    g_front.pitch  = g_mode.pitch;
    g_front.pixels = g_mode.lfb_lin;
    g_front.size   = g_mode.lfb_size;
    g_front.owns   = false;

    memset(&g_back, 0, sizeof(g_back));
    g_back.width  = g_mode.width;
    g_back.height = g_mode.height;
    g_back.bpp    = g_mode.bpp;
    g_back.pitch  = g_mode.pitch;
    g_back.size   = g_mode.lfb_size;
    g_back.pixels = kmalloc(g_back.size);
    g_back.owns   = true;
    if (g_back.pixels == NULL) {
        kwarn("graphics: backbuffer alloc %zu bytes failed", g_back.size);
        return false;
    }

    g_ready = true;

    draw_demo_pattern(&g_back);
    graphics_present(NULL, 0);
    graphics_cursor_init();

    kinfo(
        "graphics: bochs-display LFB ready %ux%u@%u, fb=%#x -> %p (%zu KB)",
        g_mode.width,
        g_mode.height,
        g_mode.bpp,
        (uint32_t)g_mode.lfb_phy,
        g_mode.lfb_lin,
        g_mode.lfb_size / NUM_1K);

    return true;
}

static void graphics_draw_surface_alpha(
    graphics_surface_t *dst,
    int dx, int dy,
    const graphics_surface_t *src)
{
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int src_x = 0, src_y = 0;
    int w = src->width;
    int h = src->height;

    if (dx < 0) { src_x = -dx; w += dx; dx = 0; }
    if (dy < 0) { src_y = -dy; h += dy; dy = 0; }

    if (dx + w > dst->width)  w = dst->width - dx;
    if (dy + h > dst->height) h = dst->height - dy;

    if (w <= 0 || h <= 0) return;

    uint32_t *src_pixels = (uint32_t *)src->pixels;
    uint32_t *dst_pixels = (uint32_t *)dst->pixels;
    int src_pitch = src->pitch / 4;
    int dst_pitch = dst->pitch / 4;

    for (int y = 0; y < h; ++y) {
        uint32_t *s_row = src_pixels + (src_y + y) * src_pitch + src_x;
        uint32_t *d_row = dst_pixels + (dy + y) * dst_pitch + dx;

        for (int x = 0; x < w; ++x) {
            uint32_t pixel = s_row[x];
            if ((pixel >> 24) != 0) {
                d_row[x] = pixel;
            }
        }
    }
}

void graphics_cursor_draw_to(graphics_surface_t *dst, int x, int y) {
    if (!g_ready || !g_cursor.ready) return;

    graphics_draw_surface_alpha(dst, x, y, &g_cursor_sprite);
}
