#include <lib/syscall.h>       // 包含 get_root_window_handle, refresh_window 等
#include <unios/ipc.h>     // 包含 sendrec, message_t
#include <unios/window.h>
#include <stdint.h>
#include <stddef.h>

// --- 颜色定义 (ARGB / RGB) ---
#define COLOR_DESKTOP   0x3A6EA5
#define COLOR_TASKBAR   0xC0C0C0
#define COLOR_START_BTN 0x008000
#define COLOR_WHITE     0xFFFFFF

#define SCREEN_W 1024
#define SCREEN_H 768

int main() {
    // 1. 【关键】获取内核创建的根窗口句柄
    // 此时屏幕可能还是全黑的
    int h_desktop = get_root_window_handle();

    if (h_desktop < 0) {
        return 0;
    }

    int win1 = open_window(100, 100, 200, 200 + WIN_TITLE_HEIGHT, "test", COLOR_START_BTN);

    uint32_t *win1_pixels;
    if (!set_window_surface_buffer(win1, (void **)&win1_pixels)) {
        return 0;
    }
    for(int y = WIN_TITLE_HEIGHT; y < 200 + WIN_TITLE_HEIGHT; y++) {
        for(int x = 0; x < 200; x++) {
            int pos = y * 200 + x;
            if(x < 100) {
                win1_pixels[pos] = 0xFF0000;
            }
        }
    }
    refresh_window(win1);

    message_t msg;
    while (1) {
        // 阻塞等待消息
        // 这里的 src_dest = ANY 表示接收任何进程的消息
        int src = ANY;
        int ret = sendrec(RECEIVE, src, &msg);

        if (ret == 0) {
            // --- 处理消息 ---

            // 示例：处理鼠标点击 (假设内核 WM 会发 MOUSE_CLICK 消息给你)
            // if (msg.type == MOUSE_CLICK) {
            //     // 判断点击了哪里
            //     if (msg.u.m3.y > SCREEN_H - 40) {
            //         // 点击了任务栏...
            //     }
            // }

            // 示例：其他进程请求重绘
            // if (msg.type == MSG_REQ_REDRAW) {
            //     refresh_window(h_desktop);
            // }
        }
    }
}
