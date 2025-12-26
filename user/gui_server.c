#include <syscall.h>
#include <ipc.h>

void main() {
    // 1. 初始化桌面背景
    int bg_win = sys_create_window(0, 0, 1024, 768);
    draw_wallpaper(bg_win); // 在用户态缓冲区画图
    sys_refresh_window(bg_win);

    // 2. 初始化任务栏
    int bar_win = sys_create_window(0, 768-40, 1024, 40);
    draw_taskbar(bar_win);
    sys_refresh_window(bar_win);

    // 3. 事件循环 (充当 Window Manager 的逻辑核心)
    Message msg;
    while(1) {
        // 等待内核转发来的原始输入事件
        ipc_receive(FROM_KERNEL, &msg);

        if (msg.type == MSG_MOUSE_CLICK) {
            int mx = msg.u1, my = msg.u2;

            // WM 逻辑：判断点击了哪里
            int target_pid = sys_get_window_at(mx, my);

            if (target_pid == MY_PID) {
                // 点到了桌面或任务栏，我自己处理
                handle_taskbar_click(mx, my);
            } else {
                // 点到了用户 App，转发给它
                msg.type = MSG_USER_CLICK;
                ipc_send(target_pid, &msg);

                // 还要让内核把那个窗口置顶
                sys_win_zorder(target_pid, TOP);
            }
        }
    }
}
