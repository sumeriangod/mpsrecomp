// Mario Party Superstars: diagnostics while the game is brought up.
#include <string.h>

#include "hle.h"

// bex::SystemMain::InitializeSystem() starts with `try { throw DummyException(); } catch (...) {}` (warming up the
// exception machinery): the landing pad is the throw's own return address.
#define DUMMY_THROW_RET (MAIN_BASE + 0x307cb0ull)
#define DUMMY_TYPE "ZN3bex10SystemMain16InitializeSystemEvE14DummyException"
// bex's fibers all run main+0x134110: `try { task->Run(); } catch (...) {} return nullptr;`. A task told to stop
// (its yield returning with the flag at +0xa8 set) throws FiberReturnException to unwind to that catch.
#define FIBER_FN (MAIN_BASE + 0x134110ull)
#define FIBER_RETURN_TYPE "N3bex20FiberReturnExceptionE"

// main's C++ throws (its PLT import of __cxa_throw binds here, runtime/loader.c resolve()): the runtime has no guest
// unwinding, so libc++abi's own throw ends in failed_throw on a garbage header.
// shortcut: only the startup DummyException is caught (by hand); any other throw is reported and stops the game. Real
// support (unwinding to the landing pad) if throws turn out to be part of normal play.
HLE_FUNC("__cxa_throw", mps_cxa_throw) {
    uint64_t obj = c->x[0], ti = c->x[1], caller = c->x[30];
    const char *type = ti ? *(const char **)(ti + 8) : "?";  // std::type_info: vtable, then the mangled name
    if (caller == DUMMY_THROW_RET && !strcmp(type, DUMMY_TYPE)) {
        // libc++abi's __cxa_exception sits before the object (offsets as its __cxa_begin_catch reads them), its
        // _Unwind_Exception last: fill in what __cxa_throw would, then enter the landing pad as the unwinder does
        uint64_t hdr = obj - 0x80, unwind = obj - 0x20;
        *(uint64_t *)(hdr + 0x00) = 1;                 // referenceCount
        *(uint64_t *)(hdr + 0x08) = ti;                // exceptionType
        *(uint64_t *)(hdr + 0x10) = c->x[2];           // exceptionDestructor
        *(uint64_t *)(hdr + 0x50) = obj;               // adjustedPtr: what __cxa_begin_catch returns
        *(uint64_t *)unwind = 0x434C4E47432B2B00ull;   // exception_class "CLNGC++\0": a native C++ exception
        c->x[0] = unwind;                              // the landing pad's arguments: the exception...
        c->x[1] = 1;                                   // ...and the selector of the catch clause that matched
        return;
    }
    uint64_t fiber = fiber_current(c);
    if (!strcmp(type, FIBER_RETURN_TYPE) && fiber && *(uint64_t *)(uintptr_t)(fiber + 0x28) == FIBER_FN) {
        // shortcut: the fiber's frames are dropped, not unwound (their destructors don't run): its catch returns
        // nullptr, so the fiber ends and the thread's own context goes on
        fiber_exit(c, 0);
    }
    LOG("[mps] C++ exception: type %s, object %llx, thrown from %llx\n", type, obj, caller);
    if (!strncmp(type, "St", 2)) {  // a std:: exception: what() is the vtable's third slot (after the two destructors)
        c->x[0] = obj;
        guest_call(c, (*(uint64_t **)obj)[2]);
        LOG("[mps]   what(): %s\n", c->x[0] ? (const char *)c->x[0] : "(null)");
    }
    uint64_t fp = c->x[29];  // the guest frame chain: [fp] the caller's fp, [fp + 8] its return address
    for (int i = 0; i < 20 && fp >= GUEST_LO && fp < GUEST_HI && !(fp & 7); i++, fp = *(uint64_t *)fp) {
        uint64_t ret = *(uint64_t *)(fp + 8), off = 0;
        const char *sym = loader_symbol_at(ret, &off);
        LOG("[mps]   #%d %llx %s+%llx\n", i, ret, sym ? sym : "?", off);
    }
    FATAL("C++ exception in the game (no guest unwinding yet)\n");
}
