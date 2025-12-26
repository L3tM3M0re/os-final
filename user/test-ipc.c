// user/test_ipc.c (伪代码/参考实现)
#include <stdio.h>
#include <sys/types.h>
#include <unios/ipc.h>
#include <unios/syscall.h>

void main() {
    int pid = get_pid();

    // 如果是父进程 (A)
    if (fork() != 0) {
        printf("Process A (PID %d) start.\n", pid);

        message_t msg;
        msg.type = 123;
        msg.u1   = 100;

        // 1. 发送给任意子进程 (这里简化，假设你知道子进程PID，或者用广播)
        // 实际场景建议先获取子进程PID
        int child_pid = pid + 1; // 假设

        printf("A: Sending to %d...\n", child_pid);
        send_rec(BOTH, child_pid, &msg);

        printf("A: Received reply! Result = %d\n", msg.u1);
    }
    // 如果是子进程 (B)
    else {
        int my_pid = get_pid();
        printf("Process B (PID %d) ready.\n", my_pid);

        message_t msg;
        while(1) {
            // 2. 等待接收
            send_rec(RECEIVE, ANY, &msg);

            printf("B: Got msg from %d, val=%d. Sending reply...\n", msg.source, msg.u1);

            msg.u1 = msg.u1 * 2; // 处理数据

            // 3. 回复 (注意：RECEIVE 只是收，回复需要再发一次，或者用 BOTH 机制)
            // 如果你的 BOTH 实现是“发完立刻等回复”，那么 B 这里应该是单纯的 SEND 回去
            // 但通常 IPC 库会封装好 reply。这里假设你用 send_rec 直接发回。
            send_rec(SEND, msg.source, &msg);
        }
    }
}
