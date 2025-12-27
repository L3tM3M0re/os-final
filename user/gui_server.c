#include <lib/syscall.h> // 包含 get_root_window_handle, refresh_window 等
#include <unios/ipc.h>   // 包含 sendrec, message_t
#include <unios/window.h>
#include <unios/keyboard.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// --- 颜色定义 (ARGB / RGB) ---
#define COLOR_DESKTOP   0x3A6EA5
#define COLOR_TASKBAR   0xC0C0C0
#define COLOR_START_BTN 0x008000
#define COLOR_WHITE     0xFFFFFF

#define SCREEN_W 1024
#define SCREEN_H 768

int main() {
    int h_desktop = get_root_window_handle();

    if (h_desktop < 0) { return 0; }

    int pid = get_pid();
    set_root_window_owner(pid);

    int win_x = 100;
    int win_y = 100;
    int win_w = 200;
    int win_h = 200;

    int win1 = open_window(win_x, win_y, win_w, win_h);

    uint32_t *win1_pixels;
    if (!set_window_surface_buffer(win1, (void **)&win1_pixels)) { return 0; }
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 200; x++) {
            int pos = y * 200 + x;
            if (x < 100) { win1_pixels[pos] = 0xFF0000; }
        }
    }
    refresh_window(win1);
    // printf("test\n");
    message_t msg;
    while (1) {
        int src = ANY;
        int ret = sendrec(RECEIVE, src, &msg);

        if (ret == 0) {
            // --- 处理键盘消息 ---
            if (msg.type == MSG_GUI_KEY) {
                uint32_t key = (uint32_t)msg.u1; // 从 u1 取出按键
                // printf("[Debug] key: %d", key);
                // 判断是否是向左键
                // 注意：你需要确认 keyboard.h 中 LEFT 的具体数值
                if (key == LEFT) {
                    win_x -= 10; // 向左移动 10 像素

                    // 调用系统调用移动窗口
                    move_abs_window(win1, win_x, win_y);

                    // 移动后通常不需要手动 refresh_window(win1)，
                    // 因为 move_window 内核实现里应该会标记脏矩形并重绘。
                    // 但为了保险，可以刷一下桌面（因为窗口移走后，原来的位置需要重绘）
                    // refresh_window(h_desktop);
                }
            }

            // ... (其他消息处理) ...
        }
    }
}
