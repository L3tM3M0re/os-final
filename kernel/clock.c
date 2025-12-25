#include <unios/clock.h>
#include <unios/syscall.h>
#include <unios/proc.h>
#include <unios/graphics.h>
#include <unios/interrupt.h>
#include <unios/kstate.h>
#include <arch/x86.h>
#include <sys/defs.h>

#define RENDER_INTERVAL_TICKS (SYSCLK_FREQ_HZ / 60)

int system_ticks;

static int render_timer = 0;

void clock_handler(int irq) {
    ++system_ticks;
    if (kstate_on_init) { return; }

    if (++render_timer >= RENDER_INTERVAL_TICKS) {
        graphics_cursor_render();
        render_timer = 0;
    }

    --p_proc_current->pcb.live_ticks;
    wakeup_exclusive(&system_ticks);
}

void init_sysclk() {
    //! use 8253 PIT timer0 as system clock
    outb(TIMER_MODE, RATE_GENERATOR);
    outb(TIMER0, (uint8_t)((TIMER_FREQ / SYSCLK_FREQ_HZ) >> 0));
    outb(TIMER0, (uint8_t)((TIMER_FREQ / SYSCLK_FREQ_HZ) >> 8));
    system_ticks = 0;

    //! enable clock irq for 8259A
    put_irq_handler(CLOCK_IRQ, clock_handler);
    enable_irq(CLOCK_IRQ);
}

int do_get_ticks() {
    return system_ticks;
}
