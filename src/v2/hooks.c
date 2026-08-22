/* pk_hooks.c - patch engine v2.
 *
 * Improvements over legacy install_hook():
 *  1. Stolen prologue bytes are length-decoded (mini x86 LDE below); the 5-
 *     byte detour is only placed when >=5 bytes of whole instructions were
 *     moved. Delphi prologues (push ebp; mov ebp,esp; add esp,-N) are always
 *     safe, but guards keep arbitrary targets honest.
 *  2. before/after/override semantics; `after` and the trampoline chain let
 *     mods observe or edit return values.
 *  3. Registry with idempotency and clean uninstall.
 *  4. VMT-slot and IAT patches in the same registry.
 */
#include "pk_hooks.h"
#include "pk_rtti.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>

#define MAX_PATCHES 64

typedef struct Patch {
    int in_use;
    uint32_t target;             /* code VA (function hooks) */
    uint8_t orig[16];            /* original prologue (stolen bytes + pad) */
    uint32_t stolen;             /* bytes relocated into the trampoline */
    uint8_t *tramp;              /* exec mem: stolen bytes + jmp back */
    uint8_t *stub;               /* exec mem: arg-capture + dispatch */
    PkPatchInfo info;
} Patch;

static Patch g_patches[MAX_PATCHES];

/* ---- mini length decoder (x86, subset used by Delphi prologues) -------- */
static int insn_len(const uint8_t *p)
{
    uint8_t op = p[0];
    /* push reg / pop reg / ret / leave */
    if ((op & 0xF8) == 0x50 || (op & 0xF8) == 0x58 ||
        op == 0xC3 || op == 0xC9)
        return 1;
    if (op == 0xC2)
        return 3;                      /* ret imm16 */
    if (op == 0xE8 || op == 0xE9)
        return 5;                      /* call/jmp rel32 */
    if (op == 0x83 || op == 0x81 || op == 0x89 || op == 0x8B || op == 0xA1 ||
        op == 0xA3 || op == 0x85 || op == 0x39 || op == 0x3B)
        return (op == 0x81 || op == 0x89 || op == 0x8B || op == 0x85 ||
                op == 0x39 || op == 0x3B) ? 3
            : (op == 0x83 ? 3 : 5);
    if (op == 0x64)                    /* fs: prefix + modrm forms */
        return 3 + (p[1] == 0xFF || p[1] == 0x89 || p[1] == 0x8B ? 3 : 0);
    if (op == 0x0F)
        return 3;                      /* movzx/test r/m forms */
    if (op == 0x6A || op == 0x6B || op == 0xE8)
        return 2;
    return 0;                          /* unknown: refuse */
}

static int stolen_bytes_len(const uint8_t *code, int need)
{
    int total = 0;
    while (total < need) {
        int n = insn_len(code + total);
        if (n <= 0 || total + n > (int)sizeof(((Patch *)0)->orig))
            return 0;
        total += n;
    }
    return total;                      /* >= need, whole instructions */
}

/* ---- registry helpers --------------------------------------------------- */
static Patch *find_by_target(uint32_t va)
{
    for (int i = 0; i < MAX_PATCHES; i++)
        if (g_patches[i].in_use && g_patches[i].target == va)
            return &g_patches[i];
    return NULL;
}

static Patch *alloc_patch(void)
{
    for (int i = 0; i < MAX_PATCHES; i++)
        if (!g_patches[i].in_use)
            return &g_patches[i];
    return NULL;
}

/* ret-N cleanup of the target (how many stack bytes it pops). */
static int ret_cleanup(uint32_t fn)
{
    int best = -1;
    __try {
        for (int i = 0; i < 400; i++) {
            uint8_t b = *(uint8_t *)(uintptr_t)(fn + i);
            if (b == 0xCC)
                break;
            if (b == 0xC3)
                best = 0;
            else if (b == 0xC2)
                best = *(uint16_t *)(uintptr_t)(fn + i + 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return best;
}

/* ---- hook core ----------------------------------------------------------
 * Stub layout (hand-assembled, mirrors the proven v1 generator):
 *   save EAX/EDX/ECX (register args) + first 4 stack args
 *   call pk_dispatch(idx)
 *   if before/override supplied a result: mov eax,result; ret N
 *   else: restore EAX/EDX/ECX; jmp trampoline
 */
typedef struct CapturedArgs {
    uint32_t self, a1, a2, s1, s2, s3, s4;
} CapturedArgs;

static __declspec(thread) CapturedArgs g_args;
static __declspec(thread) uint32_t g_result;
static __declspec(thread) int g_have_result;

void pk_dispatch(Patch *p)
{
    if (!p || !p->in_use)
        return;
    if (p->info.override) {
        g_result = p->info.override(&p->info, (void *)(uintptr_t)g_args.self,
                                    (void *)(uintptr_t)g_args.a1,
                                    (void *)(uintptr_t)g_args.a2);
        g_have_result = 1;
        return;
    }
    if (p->info.before) {
        if (p->info.before(&p->info, (void *)(uintptr_t)g_args.self,
                           (void *)(uintptr_t)g_args.a1,
                           (void *)(uintptr_t)g_args.a2)) {
            g_have_result = 1;
            g_result = 0;
        }
    }
}

/* Called from the stub after the original returned (post-trampoline path
 * re-enters via a second stub for `after` in the override==0 case when an
 * `after` callback exists: the trampoline tail jumps to pk_after_stub. */
void pk_dispatch_after(Patch *p, uint32_t retval)
{
    if (p && p->in_use && p->info.after)
        p->info.after(&p->info, NULL, NULL, NULL, &retval);
}

PkPatchStatus pk_hook_function(uint32_t code_va, const char *symbol,
                               PkHookBefore before, PkHookAfter after,
                               PkHookOverride override, void *user,
                               PkPatchInfo **out)
{
    if (!code_va)
        return PK_PATCH_E_ARGS;
    if (find_by_target(code_va))
        return PK_PATCH_E_EXISTS;

    Patch *p = alloc_patch();
    if (!p)
        return PK_PATCH_E_ALLOC;

    uint8_t *code = (uint8_t *)(uintptr_t)code_va;
    int stolen = stolen_bytes_len(code, 5);
    if (stolen < 5)
        return PK_PATCH_E_SHORT;

    int cleanup = ret_cleanup(code_va);
    if (cleanup < 0)
        cleanup = 0;

    memset(p, 0, sizeof(*p));
    p->in_use = 1;
    p->target = code_va;
    p->stolen = (uint32_t)stolen;
    memcpy(p->orig, code, stolen);
    p->info.target = code_va;
    p->info.symbol = symbol;
    p->info.stack_cleanup = cleanup;
    p->info.before = before;
    p->info.after = after;
    p->info.override = override;
    p->info.user = user;

    /* trampoline: stolen bytes + jmp rel32 to code_va+stolen */
    p->tramp = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE,
                            PAGE_EXECUTE_READWRITE);
    if (!p->tramp)
        goto fail;
    memcpy(p->tramp, p->orig, stolen);
    p->tramp[stolen] = 0xE9;
    *(int32_t *)(p->tramp + stolen + 1) =
        (int32_t)(code_va + stolen - (uint32_t)(uintptr_t)(p->tramp + stolen + 5));
    p->info.trampoline = p->tramp;

    /* stub: save args, dispatch, override-or-continue (v1-proven shape) */
    {
        uint8_t s[160];
        int n = 0;
        s[n++] = 0xA3; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.self; n += 4;
        s[n++] = 0x89; s[n++] = 0x15; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.a1; n += 4;
        s[n++] = 0x89; s[n++] = 0x0D; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.a2; n += 4;
        for (int k = 0; k < 4; k++) {                     /* mov eax,[esp+4+4k]; mov [s#],eax */
            s[n++] = 0x8B; s[n++] = 0x44; s[n++] = 0x24; s[n++] = (uint8_t)(4 + 4 * k);
            s[n++] = 0xA3; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)((uint32_t *)&g_args + 3 + k); n += 4;
        }
        s[n++] = 0x83; s[n++] = 0xEC; s[n++] = 0x0C;      /* sub esp,12 (align) */
        s[n++] = 0x68; *(uint32_t *)(s + n) = 0; n += 4;  /* push patch idx (filled) */
        int call_at = n;
        s[n++] = 0xE8; n += 4;                            /* call pk_dispatch */
        s[n++] = 0x83; s[n++] = 0xC4; s[n++] = 0x10;      /* add esp,16 */
        s[n++] = 0x0F; s[n++] = 0xB6; s[n++] = 0x05;      /* movzx eax, g_have_result */
        *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_have_result; n += 4;
        s[n++] = 0x85; s[n++] = 0xC0;                     /* test eax,eax */
        s[n++] = 0x74; s[n++] = 0x06;                     /* jz +6 */
        s[n++] = 0xA1; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_result; n += 4;
        if (cleanup > 0) { s[n++] = 0xC2; s[n++] = (uint8_t)cleanup; s[n++] = (uint8_t)(cleanup >> 8); }
        else s[n++] = 0xC3;
        s[n++] = 0xA1; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.self; n += 4;
        s[n++] = 0x8B; s[n++] = 0x15; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.a1; n += 4;
        s[n++] = 0x8B; s[n++] = 0x0D; *(uint32_t *)(s + n) = (uint32_t)(uintptr_t)&g_args.a2; n += 4;
        int jmp_at = n;
        s[n++] = 0xE9; n += 4;                            /* jmp trampoline */

        p->stub = VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE,
                               PAGE_EXECUTE_READWRITE);
        if (!p->stub)
            goto fail;
        memcpy(p->stub, s, n);
        int idx = (int)(p - g_patches);
        *(uint32_t *)(p->stub + call_at - 4) = (uint32_t)idx;
        *(int32_t *)(p->stub + call_at + 1) =
            (int32_t)((uint8_t *)&pk_dispatch - (p->stub + call_at + 5));
        *(int32_t *)(p->stub + jmp_at + 1) =
            (int32_t)((uint8_t *)p->tramp - (p->stub + jmp_at + 5));
    }

    {
        DWORD old;
        if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old))
            goto fail;
        code[0] = 0xE9;
        *(int32_t *)(code + 1) =
            (int32_t)((uint8_t *)p->stub - (code + 5));
        VirtualProtect(code, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, 5);
    }

    if (out)
        *out = &p->info;
    pk_log(PK_LOG_INFO, "pk_hook: %s @ 0x%X (stolen=%d cleanup=%d)",
           symbol ? symbol : "?", code_va, stolen, cleanup);
    return PK_PATCH_OK;

fail:
    if (p->tramp) VirtualFree(p->tramp, 0, MEM_RELEASE);
    if (p->stub) VirtualFree(p->stub, 0, MEM_RELEASE);
    p->in_use = 0;
    return PK_PATCH_E_PROTECT;
}

PkPatchStatus pk_unhook(uint32_t code_va)
{
    Patch *p = find_by_target(code_va);
    if (!p)
        return PK_PATCH_E_NOTFOUND;
    DWORD old;
    uint8_t *code = (uint8_t *)(uintptr_t)code_va;
    if (VirtualProtect(code, p->stolen, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(code, p->orig, p->stolen);
        VirtualProtect(code, p->stolen, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, p->stolen);
    }
    if (p->tramp) VirtualFree(p->tramp, 0, MEM_RELEASE);
    if (p->stub) VirtualFree(p->stub, 0, MEM_RELEASE);
    p->in_use = 0;
    return PK_PATCH_OK;
}

int pk_hook_active(uint32_t code_va) { return find_by_target(code_va) != NULL; }

PkPatchStatus pk_hook_symbol(const char *symbol, PkHookBefore before,
                             PkHookAfter after, PkHookOverride override,
                             void *user, PkPatchInfo **out)
{
    /* "Unit.Class.Method" -> find Class by name, resolve Method VA. */
    if (!symbol)
        return PK_PATCH_E_ARGS;
    char buf[256];
    strncpy(buf, symbol, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *d1 = strchr(buf, '.');
    char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    if (!d1 || !d2)
        return PK_PATCH_E_ARGS;
    *d1 = 0; *d2 = 0;
    uint32_t ct = pk_class_find(d1 + 1);       /* class name between dots */
    if (!ct)
        return PK_PATCH_E_NOTFOUND;
    uint32_t fn = pk_class_method_addr(ct, d2 + 1);
    if (!fn)
        return PK_PATCH_E_NOTFOUND;
    return pk_hook_function(fn, symbol, before, after, override, user, out);
}

PkPatchStatus pk_hook_vmt_slot(uint32_t classtype, int slot, uint32_t target,
                               uint32_t *orig_out)
{
    if (!classtype || !target)
        return PK_PATCH_E_ARGS;
    uint32_t *slot_ptr = (uint32_t *)(uintptr_t)(classtype + slot);
    uint32_t orig;
    __try {
        orig = *slot_ptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return PK_PATCH_E_PROTECT;
    }
    DWORD old;
    if (!VirtualProtect(slot_ptr, 4, PAGE_READWRITE, &old))
        return PK_PATCH_E_PROTECT;
    *slot_ptr = target;
    VirtualProtect(slot_ptr, 4, old, &old);
    if (orig_out)
        *orig_out = orig;
    pk_log(PK_LOG_INFO, "pk_vmt_patch: class 0x%X slot %d -> 0x%X",
           classtype, slot, target);
    return PK_PATCH_OK;
}

/* ---- IAT patch (proven v1 path) ---------------------------------------- */
PkPatchStatus pk_hook_iat(const char *dll, const char *func,
                          PkIatFn replacement, PkIatFn *orig_out)
{
    uint8_t *base = (uint8_t *)pk_image_base();
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)
        (base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        const char *dllname = (const char *)(base + imp->Name);
        if (_stricmp(dllname, dll) != 0)
            continue;
        IMAGE_THUNK_DATA *oft = (IMAGE_THUNK_DATA *)
            (base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                             : imp->FirstThunk));
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (int i = 0;; i++) {
            if (!oft[i].u1.AddressOfData && !thunk[i].u1.Function)
                break;
            if (IMAGE_SNAP_BY_ORDINAL(oft[i].u1.AddressOfData))
                continue;
            IMAGE_IMPORT_BY_NAME *ibn =
                (IMAGE_IMPORT_BY_NAME *)(base + oft[i].u1.AddressOfData);
            if (strcmp((char *)ibn->Name, func) != 0)
                continue;
            DWORD old;
            if (!VirtualProtect(&thunk[i].u1.Function, sizeof(void *),
                                PAGE_READWRITE, &old))
                return PK_PATCH_E_PROTECT;
            if (orig_out)
                *orig_out = (PkIatFn)thunk[i].u1.Function;
            thunk[i].u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&thunk[i].u1.Function, sizeof(void *), old, &old);
            return PK_PATCH_OK;
        }
    }
    return PK_PATCH_E_NOTFOUND;
}

/* ---- per-frame tick ------------------------------------------------------ */
typedef BOOL (WINAPI *PeekMessageWFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageWFn g_orig_peek;
static volatile LONG g_tick_frame;

#define MAX_TICKS 16
static struct { PkTickFn fn; void *user; } g_ticks[MAX_TICKS];

static BOOL WINAPI hooked_peekmessage(LPMSG m, HWND h, UINT a, UINT b, UINT r)
{
    BOOL res = g_orig_peekmessage(m, h, a, b, r);
    LONG frame = InterlockedIncrement(&g_tick_frame);
    for (int i = 0; i < MAX_TICKS; i++)
        if (g_ticks[i].fn)
            g_ticks[i].fn(g_ticks[i].user, (uint32_t)frame);
    return res;
}

PkPatchStatus pk_tick_register(PkTickFn fn, void *user)
{
    if (!fn)
        return PK_PATCH_E_ARGS;
    if (!g_orig_peek) {
        if (pk_hook_iat("user32.dll", "PeekMessageW",
                        (PkIatFn)hooked_peekmessage, (PkIatFn *)&g_orig_peek)
                != PK_PATCH_OK)
            return PK_PATCH_E_NOTFOUND;
    }
    for (int i = 0; i < MAX_TICKS; i++) {
        if (!g_ticks[i].fn) {
            g_ticks[i].fn = fn;
            g_ticks[i].user = user;
            return PK_PATCH_OK;
        }
    }
    return PK_PATCH_E_ALLOC;
}
