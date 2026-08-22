/* pk_hooks.h - patch engine: LDE-safe detours, before/after/override
 * semantics, VMT-slot and IAT patches. Register convention (EAX=Self,
 * EDX/ECX args, callee cleans via ret N). */
#ifndef PK_HOOKS_H
#define PK_HOOKS_H

#include "pk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PK_PATCH_OK = 0,
    PK_PATCH_E_ARGS,
    PK_PATCH_E_SHORT,     /* <5 bytes of safely movable prologue */
    PK_PATCH_E_EXISTS,
    PK_PATCH_E_ALLOC,
    PK_PATCH_E_PROTECT,
    PK_PATCH_E_NOTFOUND
} PkPatchStatus;

typedef struct PkPatchInfo PkPatchInfo;

/* Callback categories. `self`/`a1`/`a2`/`s1..s4` mirror the register ABI
 * args captured at the hook site. Return semantics:
 *   before:   nonzero result skips the original call (becomes override)
 *   after:    may modify *retval
 *   override: full replacement (receives the same args, returns EAX)
 */
typedef int  (*PkHookBefore)(PkPatchInfo *p, void *self, void *a1, void *a2);
typedef void (*PkHookAfter)(PkPatchInfo *p, void *self, void *a1, void *a2,
                            uint32_t *retval);
typedef uint32_t (*PkHookOverride)(PkPatchInfo *p, void *self, void *a1,
                                   void *a2);

struct PkPatchInfo {
    uint32_t target;         /* hooked code VA */
    const char *symbol;      /* "Unit.Class.Method" or custom tag */
    int stack_cleanup;       /* ret N bytes the target pops (0 for ret) */
    PkHookBefore before;
    PkHookAfter after;
    PkHookOverride override;
    void *user;              /* mod context */
    void *trampoline;        /* call-through to the original */
};

/* Install/inspect/remove. All installs should happen on the main thread
 * (the PeekMessage tick provides a safe window). */
PkPatchStatus pk_hook_function(uint32_t code_va, const char *symbol,
                               PkHookBefore before, PkHookAfter after,
                               PkHookOverride override, void *user,
                               PkPatchInfo **out);
PkPatchStatus pk_unhook(uint32_t code_va);
int  pk_hook_active(uint32_t code_va);

/* Symbolic form: resolves "Unit.Class.Method" via pk_rtti + the static DB. */
PkPatchStatus pk_hook_symbol(const char *symbol,
                             PkHookBefore before, PkHookAfter after,
                             PkHookOverride override, void *user,
                             PkPatchInfo **out);

/* VMT-slot patch: rewrites ClassType+slot (in the image) to `target`.
 * slot is a signed VMT offset (e.g. PK_VMT_DESTROY). The original value is
 * returned via *orig_out for chaining. */
PkPatchStatus pk_hook_vmt_slot(uint32_t classtype, int slot,
                               uint32_t target, uint32_t *orig_out);

/* IAT patch (proven path from v1 - used for the PeekMessageW tick). */
typedef void *PkIatFn;
PkPatchStatus pk_hook_iat(const char *dll, const char *func,
                          PkIatFn replacement, PkIatFn *orig_out);

/* Per-frame main-thread tick (installed via PeekMessageW IAT hook). */
typedef void (*PkTickFn)(void *user, uint32_t frame_no);
PkPatchStatus pk_tick_register(PkTickFn fn, void *user);

#ifdef __cplusplus
}
#endif
#endif /* PK_HOOKS_H */
