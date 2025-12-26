#include <unios/vga.h>
#include <unios/kstate.h>
#include <unios/memory.h>
#include <unios/clock.h>
#include <unios/keyboard.h>
#include <unios/hd.h>
#include <unios/schedule.h>
#include <unios/vfs.h>
#include <unios/fs.h>
#include <unios/graphics.h>
#include <unios/interrupt.h>
#include <unios/tracing.h>
#include <unios/assert.h>
#include <unios/window.h>
#include <unios/font.h>

void init();

void kernel_main() {
    //! ATTENTION: ints is disabled through the whole `kernel_main`

    //! TODO: maybe we can print our logo here?
    vga_set_cursor_visible_unsafe(false);

    kstate_on_init = true;
    kdebug("init kernel");

    init_memory();
    kinfo("init memory done");

    font_init();
    kinfo("init font done");

    graphics_boot_demo();

    init_window_manager();

    // window_t* win1 = create_window(50, 50, 200, 150, "Win1中文测试", 0xFFFF0000); // 红
    // window_t* win2 = create_window(150, 100, 200, 150, "Win2", 0xFF00FF00); // 绿
    // window_t* win3 = create_window(400, 300, 100, 100, "Win3", 0xFF0000FF); // 蓝

    // create_window(-50, -50, 150, 150, "Clipped", 0xFFFFFF00); // 黄色，只有右下角可见
    process_t *proc = try_lock_free_pcb();
    assert(proc != NULL);
    bool ok = init_locked_pcb(proc, "init", init, RPL_TASK);
    assert(ok);
    for (int i = 0; i < NR_TASKS; ++i) {
        process_t *proc = try_lock_free_pcb();
        assert(proc != NULL);
        bool ok = init_locked_pcb(
            proc, task_table[i].name, task_table[i].entry_point, RPL_TASK);
        assert(ok);
        //! NOTE: mark as pre-inited, enable it in the `init` proc later
        proc->pcb.stat = PREINITED;
    }
    kinfo("init startup proc done");

    init_sysclk();
    init_keyboard();
    init_hd();
    kinfo("init device done");

    vfs_setup_and_init();
    kinfo("init vfs done");

    kstate_reenter_cntr = 0;
    kstate_on_init      = false;
    kinfo("init kernel done");

    p_proc_current = proc;
    tss.esp0 = (uint32_t)(p_proc_current + 1);
    restart_initial();
    unreachable();
}
