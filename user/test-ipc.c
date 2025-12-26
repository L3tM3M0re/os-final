#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unios/ipc.h>      // 确保能找到 message_t, SEND, RECEIVE, BOTH 等定义
#include <lib/syscall.h>

int main(int argc, char *argv[]) {
    printf("IPC Test Started...\n");

    int pid_child = fork(); // fork 返回子进程 PID 给父进程

    if (pid_child < 0) {
        printf("Fork failed!\n");
        exit(1);
    }

    // ==========================================
    // 父进程 (Client / Sender)
    // ==========================================
    if (pid_child > 0) {
        int my_pid = get_pid();
        printf("[Parent] I am PID %d, my child is PID %d\n", my_pid, pid_child);

        message_t msg;
        msg.type = 123;
        msg.u1   = 100;

        // 1. 发送消息并等待回复 (BOTH)
        // 注意：BOTH 意味着 "Send then Receive"，会阻塞直到子进程回复
        printf("[Parent] Sending msg (val=%d) to child...\n", msg.u1);
        int ret = sendrec(BOTH, pid_child, &msg);

        if (ret == 0) {
            printf("[Parent] Received reply from PID %d: val=%d\n", msg.source, msg.u1);

            // 简单验证
            if (msg.u1 == 200) {
                printf("[Parent] SUCCESS: Valid reply received!\n");
            } else {
                printf("[Parent] FAILURE: Wrong value received.\n");
            }
        } else {
            printf("[Parent] Error: send_rec returned %d\n", ret);
        }

        // 等待子进程退出，防止僵尸进程
        int status;
        wait(&status);
    }
    // ==========================================
    // 子进程 (Server / Receiver)
    // ==========================================
    else {
        int my_pid = get_pid();
        printf("[Child] I am PID %d, waiting for message...\n", my_pid);

        message_t msg;

        // 2. 接收消息 (RECEIVE)
        // 这里的 ANY 表示接收任意来源的消息
        sendrec(RECEIVE, ANY, &msg);

        printf("[Child] Got msg from PID %d, val=%d. Processing...\n", msg.source, msg.u1);

        // 3. 处理数据
        msg.u1 = msg.u1 * 2;
        msg.type = 456; // 修改类型表示回复

        // 4. 发送回复 (SEND)
        // 注意：一定要发给 msg.source (刚才发给我的人)
        printf("[Child] Sending reply back to %d...\n", msg.source);
        sendrec(SEND, msg.source, &msg);

        printf("[Child] Done, exiting.\n");
        exit(0);
    }
    return 0;
}
