/* SPDX-License-Identifier: Apache-2.0 */
#include <rtthread.h>
#include <rthw.h>
#include <stdint.h>
#include <stddef.h>
#include "control_shared.h"
#ifdef RTT_XEN_QEMU_TEST
#define GICD 0x08000000UL
#define GICC 0x08010000UL
#else
#define GICD 0x03001000UL
#define GICC 0x03002000UL
#endif

uint64_t boot_dtb;
rt_ubase_t irq_switch_pending, irq_switch_from, irq_switch_to;
extern char _image_end[];
static uint64_t timer_frequency, timer_period, timer_next;
struct hvm_param { uint16_t domid, pad; uint32_t index; uint64_t value; };
struct xen_console {
    char in[1024], out[2048];
    volatile uint32_t in_cons, in_prod, out_cons, out_prod;
};
static volatile struct xen_console *console;
static uint32_t console_event;
static long hypercall(unsigned long op, unsigned long a, unsigned long b, unsigned long c);
static struct rtt_control_shared shared;
static volatile uint32_t fp_failures;
static volatile uint64_t irq_count;
static volatile uint32_t last_iar;

static void map_shared_block(uintptr_t address)
{
    extern uint64_t boot_l2[];
    if (address < 0x40000000UL && address >= 0x30000000UL) {
        boot_l2[address >> 21] = 0;
        __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
        boot_l2[address >> 21] = (address & ~0x1fffffUL) | 0x705;
        __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
    }
}

static int shared_grants_init(void)
{
#ifdef RTT_XEN_QEMU_TEST
    return 0;
#else
    uint32_t version = 1;
    long rc = hypercall(20, 8, (uintptr_t)&version, 1);
    if (rc) return rc;
    struct { uint16_t domid, size; uint32_t space; uint64_t idx, gpfn; } map = {
        .domid = 0x7ff0, .space = 1, .gpfn = 0x38000,
    };
    rc = hypercall(12, 7, (uintptr_t)&map, 0);
    if (rc) return rc;
    struct grant_entry { volatile uint16_t flags; uint16_t domid; uint32_t frame; };
    volatile struct grant_entry *table = (void *)0x38000000;
    for (unsigned i = 0; i < RTT_GRANT_COUNT; i++) {
        unsigned ref = RTT_GRANT_FIRST + i;
        table[ref].flags = 0;
        table[ref].domid = 1;
        table[ref].frame = ((uintptr_t)&shared >> 12) + i;
        __asm__ volatile("dmb ishst" ::: "memory");
        table[ref].flags = 1;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    return 0;
#endif
}

static long hypercall(unsigned long op, unsigned long a, unsigned long b, unsigned long c)
{
    register unsigned long x0 __asm__("x0") = a;
    register unsigned long x1 __asm__("x1") = b;
    register unsigned long x2 __asm__("x2") = c;
    register unsigned long x16 __asm__("x16") = op;
    __asm__ volatile("hvc #0xea1" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x16)
                     : : "memory", "cc");
    return x0;
}

static uint64_t hvm_value(uint32_t index)
{
    struct hvm_param param = { .domid = 0x7ff0, .index = index };
    if (hypercall(34, 1, (uintptr_t)&param, 0)) return 0;
    return param.value;
}

void rt_hw_console_output(const char *text)
{
#ifdef RTT_XEN_QEMU_TEST
    while (*text) {
        while (*(volatile uint32_t *)0x09000018 & (1U << 5)) {}
        *(volatile uint32_t *)0x09000000 = (uint8_t)*text++;
    }
    return;
#endif
    if (!console) {
        hypercall(18, 0, rt_strlen(text), (uintptr_t)text);
        return;
    }
    while (*text) {
        unsigned tries = 0;
        uint32_t prod = console->out_prod;
        while ((uint32_t)(prod - console->out_cons) >= 2048) {
            if (++tries > 10000) return;
            hypercall(32, 4, (uintptr_t)&console_event, 0);
            hypercall(29, 0, 0, 0);
        }
        console->out[prod & 2047] = *text++;
        __asm__ volatile("dmb ish" ::: "memory");
        console->out_prod = prod + 1;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    hypercall(32, 4, (uintptr_t)&console_event, 0);
}

rt_base_t rt_hw_interrupt_disable(void)
{
    rt_base_t level;
    __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(level) :: "memory");
    return level;
}
void rt_hw_interrupt_enable(rt_base_t level)
{
    __asm__ volatile("msr daif, %0" :: "r"(level) : "memory");
}
void rt_hw_context_switch_interrupt(rt_ubase_t from, rt_ubase_t to,
                                     rt_thread_t from_thread, rt_thread_t to_thread)
{
    (void)from_thread; (void)to_thread;
    if (!irq_switch_pending) irq_switch_from = from;
    irq_switch_to = to;
    irq_switch_pending = 1;
}
rt_uint8_t *rt_hw_stack_init(void *entry, void *parameter, rt_uint8_t *top, void *exit)
{
    uintptr_t end = ((uintptr_t)top + sizeof(rt_ubase_t)) & ~(uintptr_t)15;
    uint64_t *frame = (uint64_t *)(end - 800);
    for (unsigned i = 0; i < 100; i++) frame[i] = 0;
    frame[0] = (uintptr_t)parameter;
    frame[30] = (uintptr_t)exit;
    frame[32] = (uintptr_t)entry;
    frame[33] = 5;
    return (rt_uint8_t *)frame;
}

static inline void mmio_write(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static inline uint64_t counter(void)
{
    uint64_t value;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value));
    return value;
}
static void timer_set(uint64_t deadline)
{
    __asm__ volatile("msr cntv_cval_el0, %0; isb" :: "r"(deadline) : "memory");
    /* Xen may mask an expired timer on injection; explicitly rearm it. */
    __asm__ volatile("msr cntv_ctl_el0, %0; isb" :: "r"(1UL) : "memory");
}
void guest_irq_handler(void)
{
    irq_count++;
    uint32_t iar = *(volatile uint32_t *)(GICC + 0x0c);
    last_iar = iar;
    uint32_t irq = iar & 1023;
    if (irq == 27) {
        uint64_t now = counter();
        do {
            timer_next += timer_period;
            rt_tick_increase();
        } while ((int64_t)(now - timer_next) >= 0);
        timer_set(timer_next);
    }
    if (irq < 1020) mmio_write(GICC + 0x10, iar);
}
void guest_exception(uint64_t esr, uint64_t pc, uint64_t far)
{
    __asm__ volatile("msr daifset, #15" ::: "memory");
    rt_kprintf("RTT-XEN exception esr=%lx pc=%lx far=%lx\n", esr, pc, far);
    for (;;) __asm__ volatile("wfi");
}
void rt_hw_board_init(void)
{
#ifndef RTT_XEN_QEMU_TEST
    uint64_t pfn = hvm_value(17);
    console_event = hvm_value(18);
    if (pfn) {
        map_shared_block(pfn << 12);
        console = (void *)(uintptr_t)(pfn << 12);
    }
#endif
    rt_system_heap_init(_image_end, (void *)0x4e000000);
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_frequency));
    timer_period = timer_frequency / RT_TICK_PER_SECOND;
    if (!timer_period) guest_exception(0, 0, 0);
    /* Xen's GICv2 virtual distributor and CPU interface. */
    mmio_write(GICD, 1);
    *(volatile uint8_t *)(GICD + 0x41b) = 0x80;
    mmio_write(GICD + 0x100, 1U << 27);
    mmio_write(GICC + 4, 0xff);
    mmio_write(GICC + 8, 0);
    mmio_write(GICC, 3);
    timer_next = counter() + timer_period;
    timer_set(timer_next);
    __asm__ volatile("msr cntv_ctl_el0, %0; isb" :: "r"(1UL) : "memory");
    rt_kprintf("RTT-XEN EL1 single CPU, guest=256MiB, timer=%luHz, dtb=%lx\n",
               timer_frequency, boot_dtb);
    shared.magic = RTT_CONTROL_MAGIC;
    shared.abi = RTT_CONTROL_ABI;
    shared.size = RTT_SHARED_SIZE;
    shared.grant_first = RTT_GRANT_FIRST;
    shared.counter_frequency = timer_frequency;
    rt_kprintf("RTT-XEN shared grant setup rc=%d refs=8..11 bytes=16384\n", shared_grants_init());
}

void rt_hw_cpu_shutdown(void) { for (;;) __asm__ volatile("wfi"); }
void rt_hw_cpu_reset(void) { rt_hw_cpu_shutdown(); }
void rt_hw_cpu_idle_exec(void) { __asm__ volatile("wfi"); }
void rt_hw_us_delay(rt_uint32_t us)
{
    uint64_t end = counter() + timer_frequency * us / 1000000;
    while ((int64_t)(counter() - end) < 0) __asm__ volatile("nop");
}

static volatile uint64_t control_count, last_compute_us, max_compute_us;
static volatile int control_output;
static volatile uint64_t max_late_us;
static volatile uint32_t deadline_misses;
static void guest_idle(void)
{
    __asm__ volatile("wfi");
}
static void control_task(void *arg)
{
    (void)arg;
    int position[2] = {0}, integral[2] = {0}, old_error[2] = {0};
    rt_base_t irq_level = rt_hw_interrupt_disable();
    rt_tick_t wake = rt_tick_get();
    uint64_t expected = timer_next - timer_period;
    rt_hw_interrupt_enable(irq_level);
    uint64_t period = timer_frequency / 100;
    int target[2] = {-1000, 500};
    uint32_t accepted_sequence = 0;
    for (;;) {
        uint64_t begin = counter();
        int64_t late = (int64_t)(begin - expected);
        uint64_t late_us = late > 0 ? (uint64_t)late * 1000000 / timer_frequency : 0;
        if (control_count && late_us > max_late_us) max_late_us = late_us;
        if (control_count && late_us > 10000) deadline_misses++;
        volatile struct rtt_control_input *in = &shared.input;
        uint32_t seq = in->sequence;
        __asm__ volatile("dmb ish" ::: "memory");
        int requested = in->target_x;
        int requested_y = in->target_y;
        uint32_t external = in->external_mode;
        __asm__ volatile("dmb ish" ::: "memory");
        if (!(seq & 1) && seq == in->sequence) {
            if (external) {
                if (requested > 10000) requested = 10000;
                if (requested < -10000) requested = -10000;
                if (requested_y > 10000) requested_y = 10000;
                if (requested_y < -10000) requested_y = -10000;
                target[0] = requested;
                target[1] = requested_y;
            } else {
                target[0] = ((control_count / 100) & 1) ? 1000 : -1000;
                target[1] = -target[0] / 2;
            }
            accepted_sequence = seq;
        }
        int results[2];
        for (unsigned axis = 0; axis < 2; axis++) {
            int error = target[axis] - position[axis];
            integral[axis] += error;
            if (integral[axis] > 10000) integral[axis] = 10000;
            if (integral[axis] < -10000) integral[axis] = -10000;
            int result = error / 8 + integral[axis] / 128 + (error - old_error[axis]) / 4;
            if (result > 200) result = 200;
            if (result < -200) result = -200;
            position[axis] += result / 8;
            old_error[axis] = error;
            results[axis] = result;
        }
        control_output = results[0];
        control_count++;
        last_compute_us = (counter() - begin) * 1000000 / timer_frequency;
        if (last_compute_us > max_compute_us) max_compute_us = last_compute_us;
        volatile struct rtt_control_output *out = &shared.output;
        uint32_t next = out->sequence + 2;
        out->sequence = next - 1;
        __asm__ volatile("dmb ish" ::: "memory");
        out->input_sequence = accepted_sequence;
        out->pan = results[0];
        out->tilt = results[1];
        out->control_count = control_count;
        out->counter = counter();
        out->compute_us = last_compute_us;
        out->max_compute_us = max_compute_us;
        out->max_late_us = max_late_us;
        out->deadline_misses = deadline_misses;
        out->fp_failures = fp_failures;
        __asm__ volatile("dmb ish" ::: "memory");
        out->sequence = next;
        expected += period;
        rt_thread_delay_until(&wake, 10);
    }
}

static void fp_check_task(void *argument)
{
    unsigned index = (uintptr_t)argument;
    rt_kprintf("FP task %u started\n", index);
    uint64_t expected = index ? (3UL << 22) : 0;
    __asm__ volatile("msr fpcr, %0" :: "r"(expected));
    register double value __asm__("d8") = index ? 2.25 : 1.25;
    double original = value;
    for (;;) {
        __asm__ volatile("" : "+w"(value));
        rt_thread_mdelay(1);
        __asm__ volatile("" : "+w"(value));
        uint64_t actual;
        __asm__ volatile("mrs %0, fpcr" : "=r"(actual));
        if (actual != expected || value != original) fp_failures++;
    }
}
int main(void)
{
    rt_thread_idle_sethook(guest_idle);
    volatile uint64_t *probe = rt_malloc(32U * 1024U * 1024U);
    RT_ASSERT(probe != RT_NULL);
    for (unsigned i = 0; i < (32U * 1024U * 1024U) / sizeof(*probe); i += 512)
        probe[i] = 0x5254545800000000ULL ^ i;
    for (unsigned i = 0; i < (32U * 1024U * 1024U) / sizeof(*probe); i += 512)
        RT_ASSERT(probe[i] == (0x5254545800000000ULL ^ i));
    rt_free((void *)probe);
    rt_kprintf("RTT-XEN heap probe 32MiB PASS (4KiB stride)\n");
    rt_thread_t task = rt_thread_create("control", control_task, RT_NULL, 8192, 5, 10);
    RT_ASSERT(task != RT_NULL);
    rt_thread_startup(task);
    for (unsigned i = 0; i < 2; i++) {
        task = rt_thread_create(i ? "fp-b" : "fp-a", fp_check_task,
                                (void *)(uintptr_t)i, 4096, 8 + i, 10);
        RT_ASSERT(task != RT_NULL);
        rt_thread_startup(task);
    }
    rt_kprintf("RTT-XEN genuine RT-Thread kernel; synthetic PID control at 100Hz\n");
    for (;;) {
        rt_kprintf("RTT-XEN heartbeat tick=%lu control=%lu output=%d compute_us=%lu max_us=%lu late_us=%lu fp_fail=%u\n",
                   (unsigned long)rt_tick_get(), control_count, control_output,
                   last_compute_us, max_compute_us, max_late_us, fp_failures);
        rt_thread_mdelay(1000);
    }
}

/* Compiler-emitted freestanding memory helpers. */
void *memcpy(void *d, const void *s, size_t n) { return rt_memcpy(d, s, n); }
void *memset(void *d, int c, size_t n) { return rt_memset(d, c, n); }
void *memmove(void *d, const void *s, size_t n) { return rt_memmove(d, s, n); }
int memcmp(const void *a, const void *b, size_t n) { return rt_memcmp(a, b, n); }
size_t strlen(const char *s) { return rt_strlen(s); }
