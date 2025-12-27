#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lib/syscall.h>
#include <unios/window.h>

// --- 颜色定义 ---
#define COLOR_BG      0x222222
#define COLOR_SEG_ON  0x00FF00 // 亮绿色
#define COLOR_SEG_OFF 0x1A1A1A // 暗灰色

// --- 七段数码管逻辑 ---
//   A
// F   B
//   G
// E   C
//   D
// 编码：GFEDCBA (Bit 0 is A)
static uint8_t DIGIT_MAP[] = {
    0x3F, // 0: 011 1111 (ABCDEF)
    0x06, // 1: 000 0110 (BC)
    0x5B, // 2: 101 1011 (ABDEG)
    0x4F, // 3: 100 1111 (ABCDG)
    0x66, // 4: 110 0110 (BCFG)
    0x6D, // 5: 110 1101 (ACDFG)
    0x7D, // 6: 111 1101 (ACDEFG)
    0x07, // 7: 000 0111 (ABC)
    0x7F, // 8: 111 1111 (All)
    0x6F  // 9: 110 1111 (ABCFG)
};

static uint32_t *win_buf;
static int       win_w, win_h;

// 画矩形辅助函数
void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            if (x + i >= 0 && x + i < win_w && y + j >= 0 && y + j < win_h) {
                win_buf[(y + j) * win_w + (x + i)] = color;
            }
        }
    }
}

// 绘制单个数字
// x,y: 左上角坐标; scale: 大小倍率
void draw_digit(int x, int y, int scale, int num) {
    if (num < 0 || num > 9) return;
    uint8_t map = DIGIT_MAP[num];

    int w = 4 * scale;  // 笔画宽
    int l = 10 * scale; // 笔画长

    // 定义7个段的位置
    // A (顶横)
    draw_rect(x + w, y, l, w, (map & 0x01) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // B (上右竖)
    draw_rect(
        x + w + l, y + w, w, l, (map & 0x02) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // C (下右竖)
    draw_rect(
        x + w + l,
        y + 2 * w + l,
        w,
        l,
        (map & 0x04) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // D (底横)
    draw_rect(
        x + w,
        y + 2 * w + 2 * l,
        l,
        w,
        (map & 0x08) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // E (下左竖)
    draw_rect(
        x, y + 2 * w + l, w, l, (map & 0x10) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // F (上左竖)
    draw_rect(x, y + w, w, l, (map & 0x20) ? COLOR_SEG_ON : COLOR_SEG_OFF);
    // G (中横)
    draw_rect(
        x + w, y + w + l, l, w, (map & 0x40) ? COLOR_SEG_ON : COLOR_SEG_OFF);
}

int main() {
    // 1. 打开窗口 (加宽到 360 以容纳间隔)
    win_w      = 360;
    win_h      = 128;
    int handle = open_window(400, 100, win_w, win_h);
    if (handle < 0) {
        printf("Clock: open window failed\n");
        return 1;
    }

    // 2. 获取显存缓冲
    if (!set_window_surface_buffer(handle, (void **)&win_buf)) {
        printf("Clock: get buffer failed\n");
        return 1;
    }

    int counter = 0;
    int scale   = 3;
    int digit_w = (4 + 10 + 4) * scale; // = 54px
    int gap     = 15;                   // 数字之间的间隔

    while (1) {
        // --- 绘制背景 ---
        for (int i = 0; i < win_w * win_h; ++i) { win_buf[i] = COLOR_BG; }

        // --- 计算时间 ---
        int total_seconds = counter;
        int mins          = (total_seconds / 60) % 60;
        int secs          = total_seconds % 60;

        // --- 绘制布局 ---
        int cursor_x = 30; // 左边距
        int start_y  = 20;

        // 分钟十位
        draw_digit(cursor_x, start_y, scale, mins / 10);
        cursor_x += digit_w + gap;

        // 分钟个位
        draw_digit(cursor_x, start_y, scale, mins % 10);
        cursor_x += digit_w + gap;

        // 冒号 (两个方块)
        int colon_w = 20; // 冒号区域宽度
        // 上点
        draw_rect(
            cursor_x + 5,
            start_y + 20,
            scale * 2,
            scale * 2,
            (counter % 2) ? COLOR_SEG_ON : COLOR_SEG_OFF);
        // 下点
        draw_rect(
            cursor_x + 5,
            start_y + 50,
            scale * 2,
            scale * 2,
            (counter % 2) ? COLOR_SEG_ON : COLOR_SEG_OFF);
        cursor_x += colon_w + gap;

        // 秒钟十位
        draw_digit(cursor_x, start_y, scale, secs / 10);
        cursor_x += digit_w + gap;

        // 秒钟个位
        draw_digit(cursor_x, start_y, scale, secs % 10);

        // --- 刷新窗口 ---
        refresh_window(handle);

        // --- 延时 ---
        sleep(1000);
        counter++;
    }

    return 0;
}
