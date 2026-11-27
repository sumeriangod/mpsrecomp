// Runtime-internal declarations.
#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "cpu.h"
#include "platform.h"
#include "game.h"

// LOG: to stderr; RECOMP_LOG_TIME=1 prefixes each line with seconds since start (runtime/dispatch.c rt_log)
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) rt_log(__VA_ARGS__)
#define FATAL(...) (fprintf(stderr, "FATAL: " __VA_ARGS__), rt_fatal_trace())

// ---- address space layout (guest addresses == host addresses)
// macOS reserves 0x2_0000_0000-0x70_0000_0000, and guest addresses must fit in 39 bits, so the
// guest lives in a 12.5 GB window at 0x70_0000_0000 claimed at startup (see main.c). iOS 27 with the
// extended-virtual-addressing entitlement gives an app up to 0x80_0000_0000 and keeps 0x2_ee00_0000-0x70_0000_0000
// for itself, so the same window works there (measured on an iPad Pro M5). The window can move (a system with less
// address space): make GUEST_ORIGIN=<base> generates the game code for that base and builds the runtime with it.
#ifndef GUEST_ORIGIN
#define GUEST_ORIGIN 0x7000000000ull
#endif
#define GUEST_LO     GUEST_ORIGIN
#define GUEST_HI     (GUEST_ORIGIN + 0x320000000ull)
#define MAX_MODULES  8                                             // (GAME_MODULES_X, runtime/gamelib.h)
#define MODULE_BASE(i) (GUEST_ORIGIN + (uint64_t)(i) * 0x004000000ull)  // module i of the load order, 64 MB each
#define MAIN_BASE    MODULE_BASE(0)
#define NRO_BASE     (GUEST_ORIGIN + 0x040000000ull)   // NROs (nn::ro): slot i at NRO_BASE + i * NRO_SLOT, as
#define NRO_SLOT     0x0000400000ull                   // tools/gen_nros.py recompiles them (keep the two the same)
#define MAX_NROS     256
#define MAX_LOADED   (MAX_MODULES + MAX_NROS)          // g_modules: the NSOs, then each NRO loaded so far
#define HLE_BASE     (GUEST_ORIGIN + 0x080000000ull)   // fake addresses for host-implemented functions
#define HLE_SIZE     0x0000100000ull
#define TLS_BASE     (GUEST_ORIGIN + 0x090000000ull)
#define TLS_SIZE     0x0000100000ull
#define HLE_HEAP     (GUEST_ORIGIN + 0x092000000ull)   // guest-visible memory owned by host code
#define HLE_HEAP_SIZE 0x000E000000ull
#define ALIAS_BASE   (GUEST_ORIGIN + 0x100000000ull)   // alias region (svcMapMemory targets)
#define ALIAS_SIZE   0x0100000000ull
#define HEAP_BASE    (GUEST_ORIGIN + 0x240000000ull)
#define HEAP_MAX     0x00D0000000ull   // 3.25 GB

// ---- loader.c
typedef struct Module {
    const char *name;
    uint64_t base, size;
    uint64_t text_off, text_size, ro_off, ro_size, data_off, data_size;
    uint64_t dynamic, init, fini, init_array, init_array_sz, fini_array, fini_array_sz;
    uint64_t symtab, strtab, nsyms;
} Module;

extern Module g_modules[MAX_LOADED];
extern int g_nmodules;
void loader_load(const char *exefs_dir);  // (again after a reboot: modules are reloaded into wiped memory)
uint64_t loader_find_symbol(const char *name);
const char *loader_symbol_at(uint64_t addr, uint64_t *offset);
// An NRO image (nn::ro::LoadModule, runtime/hle/ro.c) placed at base, its recompiled code's: copied there, parsed,
// relocated and bound like the NSOs. Its entry is reused when it is loaded again.
Module *loader_load_nro(const char *name, uint64_t base, const uint8_t *image);
void loader_hide(Module *m);   // unloaded: its symbols are no longer found
void module_init(Cpu *c, Module *m);  // (main.c) DT_INIT, else the init_array
void kernel_add_module_regions(const Module *m);  // (kernel.c) its segments in the memory map

// ---- dispatch.c
typedef void (*HostFn)(Cpu *c);
void dispatch_init(void);
GuestFn dispatch_lookup(uint64_t addr);
uint64_t hle_register(const char *name, HostFn fn);  // returns fake guest address
const char *hle_name_of(uint64_t addr);             // the stub's name, or NULL (debugging)
HostFn hle_lookup_impl(const char *name);
void hle_override(uint64_t guest_addr, HostFn fn, const char *name);
void rt_fatal_trace(void) __attribute__((noreturn));
void crash_handler_install(void);  // per thread (alternate signal stack)

// ---- kernel.c
typedef struct KThread KThread;
void kernel_init(void);
void kernel_stop_threads(void (*idle)(void));  // reboot: unwind every guest thread and join it (idle: called while waiting)
void kernel_reset(void);                       // reboot: empty handle table, memory map, thread list
KThread *kernel_create_main_thread(uint64_t entry, uint64_t stack_size, int prio);
void kernel_run_main(KThread *t, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4);
uint32_t kernel_current_thread_handle(void);
Cpu *kernel_current_cpu(void);

// ---- ipc.c
typedef struct Service Service;
int ipc_handle_request(Cpu *c, uint32_t handle);
uint32_t ipc_connect_named_port(const char *name);

// ---- in-place reboot (main.c): switch seeds or go back to the start screen without leaving the process
enum { RT_REBOOT_NONE, RT_REBOOT_GAME, RT_REBOOT_TITLE };  // GAME: boot again at once (new seed); TITLE: start screen
void rt_request_reboot(int kind);  // any thread; the window loop (ui_run) returns it and main() carries it out
int rt_reboot_requested(void);
extern volatile int g_rt_unwind;   // set while guest threads are being stopped (kernel.c)
void rt_unwind_point(void);        // guest threads: unwind to thread_main if a reboot is stopping them

// ---- ui (runtime/nvn/metal.m)
int ui_run(void);  // window loop; returns the reboot kind requested (the window stays open)

// ---- helpers
void guest_call(Cpu *c, uint64_t addr);   // call guest function at addr (saves/restores x30)
uint64_t guest_sym(const char *name);     // cached loader_find_symbol, fatal if missing
void *hle_alloc(uint64_t size);           // zeroed guest-visible memory (never freed)
extern const char *g_romfs_dir;
