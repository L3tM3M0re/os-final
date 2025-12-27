#pragma once

#include <stdint.h>
#include <unios/layout.h>

// IPC 常量定义
#define SEND    1
#define RECEIVE 2
#define BOTH    3 // SEND_REC

#define ANY       (NR_PCBS + 10) // 接收任意进程的消息
#define NO_TASK   (NR_PCBS + 20) // 无目标
#define INTERRUPT -10            // 硬件中断消息源

// 常用消息类型
#define MSG_HARDWARE_INTR 1
#define MSG_PING          2
#define MSG_OPEN          3
#define MSG_CLOSE         4
#define MSG_READ          5
#define MSG_WRITE         6
// GUI 相关
#define MSG_GUI_CLICK       100
#define MSG_GUI_KEY         101
#define MSG_GUI_MOUSE_EVENT 102

// 消息结构体 (24 B)
typedef struct {
    int source; // 发送者 PID
    int type;   // 消息类型

    union {
        struct {
            int u1, u2, u3, u4;
        }; // 通用整型载荷

        struct {
            void* ptr;
            int   _p1, _p2, _p3;
        }; // 指针载荷

        struct {
            int val;
            int _i1, _i2, _i3;
        } integer; // 别名方便使用
    };
} message_t;

/*!
 * \brief IPC 系统调用的内核入口，处理发送和接收逻辑
 *
 * \param function  操作类型：SEND, RECEIVE 或 BOTH
 * \param src_dest  如果是 SEND，表示目标 PID；如果是 RECEIVE，表示源 PID (或
 * ANY)
 * \param m         指向用户态消息缓冲区的指针 (逻辑地址)
 *
 * \return 0 表示成功，非 0 表示失败 (如死锁或非法 PID)
 *
 * \note 此函数会导致进程状态改变（阻塞或就绪），并可能触发调度
 */
int do_sendrec(int function, int src_dest, message_t* m);

/*!
 * \brief 唤醒正在等待该信号量的进程
 *
 * \param channel 等待通道 (通常是变量地址)
 */
void wakeup_exclusive(void* channel);
