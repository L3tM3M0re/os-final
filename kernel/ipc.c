#include <unios/ipc.h>
#include <unios/proc.h>
#include <unios/assert.h>
#include <unios/syscall.h>
#include <unios/schedule.h>
#include <unios/tracing.h>
#include <arch/x86.h> // disable_int, enable_int
#include <sys/types.h>
#include <string.h> // memcpy

static inline bool is_kernel_task(process_t* p) {
    int pid = proc2pid(p);
    return pid < NR_TASKS;
}

static void copy_msg(
    process_t* dest,
    process_t* src,
    message_t* dst_va,
    message_t* src_va,
    int        override_source);
static int  msg_send(process_t* current, int dest_pid, message_t* m);
static int  msg_receive(process_t* current, int src_pid, message_t* m);
static void block(process_t* p);
static void unblock(process_t* p);

/*!
 * \brief 跨进程安全拷贝消息
 * \param dest     目标进程指针
 * \param src      源进程指针
 * \param dst_va   目标进程中的虚拟地址 (接收缓冲区)
 * \param src_va   源进程中的虚拟地址 (发送缓冲区)
 */
static void copy_msg(
    process_t* dest,
    process_t* src,
    message_t* dst_va,
    message_t* src_va,
    int        override_source) {
    message_t msg_buf;
    uint32_t  current_cr3 = rcr3(); // 使用 rcr3 获取当前页目录基址
    // 切换到源进程空间读取消息
    // 注意：如果是从内核中断发来的消息(INTERRUPT)，src可能为NULL或特殊值，需要判断
    // 但通常 msg_send/receive 的源和目标都是有效进程
    if (src == NULL) {
        memcpy(&msg_buf, src_va, sizeof(message_t));
    } else {
        bool need_switch = (src->pcb.cr3 != current_cr3);
        if (need_switch) { lcr3(src->pcb.cr3); }
        memcpy(&msg_buf, src_va, sizeof(message_t));
        if (need_switch) { lcr3(current_cr3); }
    }

    if (override_source != NO_TASK) { msg_buf.source = override_source; }

    // 切换到目标进程空间写入消息
    if (dest->pcb.cr3 != rcr3()) { lcr3(dest->pcb.cr3); }

    // 使用 dest 的页表，可以直接写入 dst_va
    memcpy(dst_va, &msg_buf, sizeof(message_t));

    // 恢复现场
    if (rcr3() != current_cr3) { lcr3(current_cr3); }
}

/*!
 * \brief IPC 系统调用的内核入口
 *
 * \param function 操作码 (SEND/RECEIVE/BOTH)
 * \param src_dest 目标或源 PID
 * \param m 用户空间消息指针
 * \return 0 成功, -1 失败
 */
int do_sendrec(int function, int src_dest, message_t* m) {
    int ret        = 0;
    int caller_pid = proc2pid(p_proc_current);

    // 暂存消息指针
    p_proc_current->pcb.p_msg      = m;
    p_proc_current->pcb.p_recvfrom = NO_TASK;
    p_proc_current->pcb.p_sendto   = NO_TASK;

    if (function == SEND) {
        ret = msg_send(p_proc_current, src_dest, m);
    } else if (function == RECEIVE) {
        ret = msg_receive(p_proc_current, src_dest, m);
    } else if (function == BOTH) {
        ret = msg_send(p_proc_current, src_dest, m);
        if (ret == 0) { ret = msg_receive(p_proc_current, src_dest, m); }
    } else {
        panic("Unknown IPC function");
    }

    return ret;
}

/*!
 * \brief 发送消息的核心逻辑
 *
 * \param current 发送方进程指针
 * \param dest_pid 接收方 PID
 * \param m 消息内容
 * \return 0 成功, -1 死锁
 */
static int msg_send(process_t* current, int dest_pid, message_t* m) {
    process_t* dest = pid2proc(dest_pid);
    disable_int();

    // 死锁检测 (简单检测自身)
    if (proc2pid(current) == dest_pid) {
        enable_int();
        return -1;
    }

    // 检查目标是否正在 RECEIVING 且在等我
    if ((dest->pcb.stat == RECEIVING)
        && (dest->pcb.p_recvfrom == proc2pid(current)
            || dest->pcb.p_recvfrom == ANY)) {
        assert(dest->pcb.p_msg != NULL);
        assert(m != NULL);

        // 拷贝消息到目标的缓冲区
        copy_msg(dest, current, dest->pcb.p_msg, m, proc2pid(current));

        // 唤醒目标
        dest->pcb.p_recvfrom = NO_TASK;
        unblock(dest);
    } else {
        if (is_kernel_task(current)) {
            enable_int();
            return 0;
        }

        current->pcb.stat     = SENDING;
        current->pcb.p_sendto = dest_pid;

        // 加入目标的发送队列
        if (dest->pcb.q_sending) {
            process_t* p = (process_t*)dest->pcb.q_sending;
            while (p->pcb.next_sending) { p = (process_t*)p->pcb.next_sending; }
            p->pcb.next_sending = (struct pcb_s*)current;
        } else {
            dest->pcb.q_sending = (struct pcb_s*)current;
        }
        current->pcb.next_sending = NULL;

        // 阻塞自己 (scheduler 切换上下文时会保存当前的 EFLAGS，其中 IF=0)
        block(current);
    }

    enable_int();

    return 0;
}

/*!
 * \brief 接收消息的核心逻辑
 *
 * \param current 接收方进程指针
 * \param src_pid 期望的消息源 PID (或 ANY)
 * \param m 接收缓冲区
 * \return 0 成功
 */
static int msg_receive(process_t* current, int src_pid, message_t* m) {
    process_t* sender = NULL;
    process_t* prev   = NULL;
    int        found  = 0;

    disable_int();

    // 1查发送队列中是否有匹配的进程
    if (current->pcb.q_sending) {
        process_t* p = (process_t*)current->pcb.q_sending;
        prev         = NULL;

        while (p) {
            if (src_pid == ANY || proc2pid(p) == src_pid) {
                sender = p;
                found  = 1;
                break;
            }
            prev = p;
            p    = (process_t*)p->pcb.next_sending;
        }
    }

    if (found && sender) {
        // 从队列移除发送者
        if (sender == (process_t*)current->pcb.q_sending) {
            current->pcb.q_sending = sender->pcb.next_sending;
        } else {
            prev->pcb.next_sending = sender->pcb.next_sending;
        }

        // 拷贝消息
        assert(m != NULL);
        assert(sender->pcb.p_msg != NULL);

        copy_msg(current, sender, m, sender->pcb.p_msg, proc2pid(sender));

        // 唤醒发送者
        sender->pcb.p_sendto = NO_TASK;
        unblock(sender);
    } else {
        current->pcb.stat       = RECEIVING;
        current->pcb.p_recvfrom = src_pid;

        // 阻塞自己，等待消息到达
        block(current);
    }

    enable_int();
    return 0;
}

/*!
 * \brief 阻塞当前进程并调度
 *
 * \param p 需要阻塞的进程 (通常是 p_proc_current)
 * \note 调用此函数前必须确保中断已关闭 (disable_int)
 */
static void block(process_t* p) {
    // 确保状态已被修改为非 READY
    assert(p->pcb.stat != READY);
    sched();
}

/*!
 * \brief 将进程设为就绪态
 *
 * \param p 目标进程
 */
static void unblock(process_t* p) {
    p->pcb.stat = READY;
}
