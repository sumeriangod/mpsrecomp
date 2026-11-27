// nn::ro: the NROs a game loads at run time (Mario Party Superstars' boards and minigames). Their code is recompiled
// ahead of time for fixed bases (tools/gen_nros.py); LoadModule recognizes the image the game read by its build id,
// places it at its base and links it as the console's ro would, against the modules loaded so far (runtime/loader.c).
// No ro service and no NRR checks: the game's calls are answered here, where its main imports them.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../gamelib.h"
#include "../hle.h"

// LARECOMP_DEBUG_FRAME_NRO=<name>:<ms>: <ms> after the NRO <name> is loaded, the frame debug dump F9 asks for
// (build/pass_NNN.png, runtime/nvn/vulkan/exec.c): a scene's frame without knowing its frame number in advance
static void *debug_frame_later(void *ms) {
    for (long t = (long)(intptr_t)ms; t > 0; t -= 100) usleep(100000);
    extern volatile int g_debug_frame_request;
    g_debug_frame_request = 1;
    return NULL;
}
static void debug_frame_on_load(const char *name) {
    const char *e = getenv("LARECOMP_DEBUG_FRAME_NRO"), *colon = e ? strchr(e, ':') : NULL;
    if (!colon || strncmp(e, name, (size_t)(colon - e)) || name[colon - e]) return;
    pthread_t t;
    if (!pthread_create(&t, NULL, debug_frame_later, (void *)(intptr_t)atol(colon + 1))) pthread_detach(t);
}

static const GameLibNro *find_nro(const uint8_t *img) {
    for (const GameLibNro *r = gamelib_tables()->nros; r && r->base; r++)
        if (!memcmp(r->build_id, img + 0x40, sizeof r->build_id)) return r;
    return NULL;
}

// Result Initialize(), Finalize(), RegisterModuleInfo(RegistrationInfo *, const void *nrr),
// UnregisterModuleInfo(RegistrationInfo *): nothing to set up or check
HLE_FUNC("_ZN2nn2ro10InitializeEv", ro_initialize) { c->x[0] = 0; }
HLE_FUNC("_ZN2nn2ro8FinalizeEv", ro_finalize) { c->x[0] = 0; }
HLE_FUNC("_ZN2nn2ro18RegisterModuleInfoEPNS0_16RegistrationInfoEPKv", ro_register_info) { c->x[0] = 0; }
HLE_FUNC("_ZN2nn2ro20UnregisterModuleInfoEPNS0_16RegistrationInfoE", ro_unregister_info) { c->x[0] = 0; }

// Result GetBufferSize(size_t *size, const void *image): the buffer LoadModule takes, the NRO's bss page-aligned (the
// game allocates it; the module goes to its own slot here)
HLE_FUNC("_ZN2nn2ro13GetBufferSizeEPmPKv", ro_buffer_size) {
    uint32_t bss = *(const uint32_t *)(uintptr_t)(c->x[1] + 0x38);
    *(uint64_t *)(uintptr_t)c->x[0] = (bss + 0xFFFull) & ~0xFFFull;
    c->x[0] = 0;
}

// Result LoadModule(Module *out, const void *image, void *buffer, size_t buffer_size, int bind, bool): placed, linked
// (every import now, whatever bind asks), constructors run. Module's first field (the console's RoModule *) holds the
// slot's base here, for UnloadModule.
HLE_FUNC("_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmib", ro_load_module) {
    uint64_t out = c->x[0];
    const uint8_t *img = (const uint8_t *)(uintptr_t)c->x[1];
    if (memcmp(img + 0x10, "NRO0", 4)) FATAL("nn::ro::LoadModule: the image at %llx isn't an NRO\n", c->x[1]);
    const GameLibNro *r = find_nro(img);
    if (!r) {
        char id[17];
        for (int i = 0; i < 8; i++) snprintf(id + 2 * i, 3, "%02x", img[0x40 + i]);
        FATAL("nn::ro::LoadModule: an NRO the game code wasn't built with (build id %s...): run make gen again\n", id);
    }
    Module *m = loader_load_nro(r->fns.name, r->base, img);
    module_init(c, m);
    debug_frame_on_load(r->fns.name);
    *(uint64_t *)(uintptr_t)out = r->base;
    c->x[0] = 0;
}

// void UnloadModule(Module *): its destructors (DT_FINI, else the fini_array backwards), then its symbols are hidden
// (the slot keeps its memory until the NRO is loaded again)
HLE_FUNC("_ZN2nn2ro12UnloadModuleEPNS0_6ModuleE", ro_unload_module) {
    uint64_t *module = (uint64_t *)(uintptr_t)c->x[0], base = *module;  // (x0 changes in the destructors' calls)
    for (int i = 0; i < g_nmodules; i++) {
        Module *m = &g_modules[i];
        if (m->base != base || base < NRO_BASE || !m->nsyms) continue;
        LOG("unload NRO %s\n", m->name);
        if (m->fini) guest_call(c, m->fini);
        else
            for (uint64_t k = m->fini_array_sz / 8; k-- > 0;) {
                uint64_t fn = ((uint64_t *)(uintptr_t)m->fini_array)[k];
                if (fn) guest_call(c, fn);
            }
        loader_hide(m);
        break;
    }
    *module = 0;
}
