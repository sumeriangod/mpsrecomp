// nn::os fibers (Mario Party Superstars' engine and NROs): the SDK switches them by loading another stack and other
// registers and returning into them (DispatchFiberContext), which recompiled code can't do. Here each guest fiber is a
// host fiber with registers of its own: switching saves nothing by hand, the switched-from fiber's registers simply
// stay in its Cpu. The fiber's thread's TLS registers go with it, so it can run on any thread that switches to it.
// FiberType fields as the SDK (11.4.3) keeps them: +0x20 state (1 ready, 2 running, 4 done), +0x21 alias stack,
// +0x28 function, +0x30 argument, +0x38/+0x40 stack, +0x48 its size, +0x50 its context (here: the host Fiber);
// ThreadType (tpidrro + 0x1f8) +0x70 the running fiber (NULL: the thread's own context; nn::os::GetCurrentFiber
// reads it, so the SDK's own runs unchanged).
// shortcut: a guest thread ending (or a reboot) while one of its fibers runs longjmps across host fibers
// (runtime/kernel.c thread_main): fine until a game does that, then end the thread's fibers first.
#include <stdlib.h>
#include <string.h>

#include "../hle.h"

typedef struct Fiber {
    void *host;      // the host fiber it runs on
    Cpu *cpu;        // its registers: its own, or the guest thread's for a thread's own context
    uint64_t guest;  // its FiberType (0: a thread's own context)
} Fiber;

#ifdef _WIN32
#include <windows.h>
static void *host_fiber_self(void) { return IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL); }
static void *host_fiber_create(void (*fn)(void *), void *arg) {
    // (the recompiled code's frames are big: as much stack as the exe's main thread, committed as it grows)
    return CreateFiberEx(64 << 10, 16 << 20, FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)fn, arg);
}
static void host_fiber_switch(void *f) { SwitchToFiber(f); }
static void host_fiber_delete(void *f) { DeleteFiber(f); }
#else
// shortcut: host fibers on Windows only; ucontext (makecontext / swapcontext) for Linux and macOS when a game needs it
static void *host_fiber_self(void) { FATAL("nn::os fibers: not on this platform yet\n"); }
static void *host_fiber_create(void (*fn)(void *), void *arg) { (void)fn, (void)arg; return host_fiber_self(); }
static void host_fiber_switch(void *f) { (void)f; host_fiber_self(); }
static void host_fiber_delete(void *f) { (void)f; }
#endif

static __thread Fiber t_self;  // this thread's own context

#define U8(a) (*(uint8_t *)(uintptr_t)(a))
#define U64(a) (*(uint64_t *)(uintptr_t)(a))
static uint64_t guest_thread(Cpu *c) { return U64(c->tpidrro + 0x1f8); }  // nn::os::ThreadType of the running thread
static Fiber *fiber_of(uint64_t f) { return f ? (Fiber *)(uintptr_t)U64(f + 0x50) : &t_self; }

// `to` goes on on this thread (the caller's registers stay in its own Cpu): back here when switched to again
static void switch_to(Cpu *c, Fiber *to) {
    if (!to || !to->host) FATAL("nn::os::SwitchToFiber: no such fiber\n");
    to->cpu->tpidr = c->tpidr;
    to->cpu->tpidrro = c->tpidrro;
    host_fiber_switch(to->host);
}

// Marks the switch as the SDK does (from: ready again, to: running, the thread's current fiber), then makes it.
static void switch_fiber(Cpu *c, uint64_t from, uint64_t to) {
    if (to && U8(to + 0x20) != 1) FATAL("nn::os::SwitchToFiber: fiber %llx isn't ready (state %u)\n", to, U8(to + 0x20));
    if (from) U8(from + 0x20) = 1;
    if (to) U8(to + 0x20) = 2;
    U64(guest_thread(c) + 0x70) = to;
    switch_to(c, fiber_of(to));
}

uint64_t fiber_current(Cpu *c) { return U64(guest_thread(c) + 0x70); }

void fiber_exit(Cpu *c, uint64_t next) {
    uint64_t g = fiber_current(c);
    if (!g) FATAL("nn::os fiber: ending one while none runs\n");
    U8(g + 0x20) = 4;  // done: not "ready again" as switch_fiber would mark it
    switch_fiber(c, 0, next);
    FATAL("nn::os fiber %llx: switched to after it finished\n", g);
}

static void fiber_main(void *p) {
    Fiber *f = p;
    Cpu *c = f->cpu;
    c->x[0] = U64(f->guest + 0x30);
    c->x[29] = c->x[30] = 0;
    c->sp = (U64(f->guest + 0x40) + U64(f->guest + 0x48)) & ~15ull;
    guest_call(c, U64(f->guest + 0x28));  // FiberType *function(void *argument): the fiber to go on with
    fiber_exit(c, c->x[0]);
}

// void InitializeFiber(FiberType *, FiberFunction, void *argument, void *stack, size_t size, int option): the stack
// is used as given (no alias mapping)
HLE_FUNC("_ZN2nn2os15InitializeFiberEPNS0_9FiberTypeEPFS2_PvES3_S3_mi", os_initialize_fiber) {
    uint64_t g = c->x[0];
    *(uint16_t *)(uintptr_t)(g + 0x20) = 1;  // ready, no alias stack
    U64(g + 0x28) = c->x[1];
    U64(g + 0x30) = c->x[2];
    U64(g + 0x38) = U64(g + 0x40) = c->x[3];
    U64(g + 0x48) = c->x[4];
    Fiber *f = calloc(1, sizeof *f);
    f->cpu = calloc(1, sizeof *f->cpu);
    f->guest = g;
    f->host = host_fiber_create(fiber_main, f);
    if (!f->host) FATAL("nn::os::InitializeFiber: no host fiber\n");
    U64(g + 0x50) = (uint64_t)(uintptr_t)f;
}

// void SwitchToFiber(FiberType *): NULL is the thread's own context
HLE_FUNC("_ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE", os_switch_to_fiber) {
    uint64_t to = c->x[0], from = U64(guest_thread(c) + 0x70);
    if (!from) {  // leaving the thread's own context: it is a fiber too from now on
        if (!to) return;
        if (!t_self.host) t_self.host = host_fiber_self();
        t_self.cpu = c;
    }
    switch_fiber(c, from, to);
}

// void FinalizeFiber(FiberType *): a fiber that isn't running
HLE_FUNC("_ZN2nn2os13FinalizeFiberEPNS0_9FiberTypeE", os_finalize_fiber) {
    uint64_t g = c->x[0];
    Fiber *f = (Fiber *)(uintptr_t)U64(g + 0x50);
    if (f) {
        host_fiber_delete(f->host);
        free(f->cpu);
        free(f);
    }
    U64(g + 0x50) = 0;
    U8(g + 0x20) = 0;
}
