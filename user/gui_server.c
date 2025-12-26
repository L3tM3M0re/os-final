#include <lib/syscall.h>       // 包含 get_root_window_handle, refresh_window 等
#include <unios/ipc.h>     // 包含 sendrec, message_t
#include <stdint.h>
#include <stddef.h>

// --- 颜色定义 (ARGB / RGB) ---
#define COLOR_DESKTOP   0x3A6EA5  // 经典的 Win2000 蓝
#define COLOR_TASKBAR   0xC0C0C0  // 经典的银灰色
#define COLOR_START_BTN 0x008000  // 绿色开始按钮
#define COLOR_WHITE     0xFFFFFF

// --- 假设的屏幕分辨率 (应从 syscall 获取或由内核传递) ---
#define SCREEN_W 1024
#define SCREEN_H 768

// --- 补充声明绘图 syscall (如果你还没加到头文件里) ---
// 建议你的 syscall 列表里加上 int fill_rect(int handle, int x, int y, int w, int h, uint32_t color);
// extern int fill_rect(int handle, int x, int y, int w, int h, uint32_t color);

// void draw_desktop_ui(int h_root) {
//     // 1. 绘制纯色背景 (覆盖内核初始化的黑屏)
//     fill_rect(h_root, 0, 0, SCREEN_W, SCREEN_H, COLOR_DESKTOP);

//     // 2. 绘制底部任务栏
//     int taskbar_h = 40;
//     int taskbar_y = SCREEN_H - taskbar_h;
//     fill_rect(h_root, 0, taskbar_y, SCREEN_W, taskbar_h, COLOR_TASKBAR);

//     // 3. 绘制一个简单的“开始菜单”按钮
//     fill_rect(h_root, 5, taskbar_y + 5, 60, taskbar_h - 10, COLOR_START_BTN);

//     // (可选) 如果支持画字，可以在这里画 "Start"
// }

int main() {
    // 1. 【关键】获取内核创建的根窗口句柄
    // 此时屏幕可能还是全黑的
    int h_desktop = get_root_window_handle();

    if (h_desktop < 0) {
        return 0;
    }

    // 2. 绘制桌面环境
    // draw_desktop_ui(h_desktop);

    // 3. 刷新显示
    // 这一步之后，屏幕会瞬间从黑屏变成你画的桌面
    refresh_window(h_desktop);

    // 4. 进入合成器/消息循环 (Compositor Loop)
    message_t msg;
    while (1) {
        // // 阻塞等待消息
        // // 这里的 src_dest = ANY 表示接收任何进程的消息
        // int src = ANY;
        // int ret = sendrec(RECEIVE, src, &msg);

        // if (ret == 0) {
        //     // --- 处理消息 ---

        //     // 示例：处理鼠标点击 (假设内核 WM 会发 MOUSE_CLICK 消息给你)
        //     // if (msg.type == MOUSE_CLICK) {
        //     //     // 判断点击了哪里
        //     //     if (msg.u.m3.y > SCREEN_H - 40) {
        //     //         // 点击了任务栏...
        //     //     }
        //     // }

        //     // 示例：其他进程请求重绘
        //     // if (msg.type == MSG_REQ_REDRAW) {
        //     //     refresh_window(h_desktop);
        //     // }
        // }
        refresh_all_window();
        for (int i = 0; i < 100000; i++);
    }
}
