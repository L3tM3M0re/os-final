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

    int clock_pid = fork();
    if (clock_pid == 0) {
        // [子进程]
        // 注意：你需要确认编译生成的 clock 可执行文件在文件系统中的绝对路径
        // 假设你的构建系统把它放在了 /user/clock
        execve("clock", NULL, NULL);

        // 如果 exec 返回，说明出错了 (比如文件找不到)
        printf("[GUI] Failed to start clock!\n");
        exit(1);
    }

    message_t msg;
    while (1) {
        int src = ANY;
        int ret = sendrec(RECEIVE, src, &msg);

        if (ret == 0) {
            // --- 处理键盘消息 ---
            if (msg.type == MSG_GUI_KEY) {
                uint32_t key = (uint32_t)msg.u1;
                if (key == LEFT) { win_x -= 10; }
                if (key == RIGHT) { win_x += 10; }
                if (key == UP) { win_y -= 10; }
                if (key == DOWN) { win_y += 10; }
                printf("[Debug] win_x: %d, win_y: %d", win_x, win_y);
                move_abs_window(win1, win_x, win_y);
            }

            // ... (其他消息处理) ...
        }
    }
}
