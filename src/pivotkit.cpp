/*
 * pivotkit.dll - Lua mod loader for Pivot Animator 5.2.11 (32-bit, Delphi 11 / FMX)
 *
 * Injected by pivotkit-loader.exe. On load it:
 *   1. Locates the TMainForm instance at runtime via Delphi published RTTI.
 *   2. Hooks the Win32 message loop for a per-frame tick.
 *   3. Embeds Lua 5.4 and runs every .lua file in the pivotkit/mods/ folder.
 *
 * Exposed as the Lua module `pivot` — see docs/MOD_API.md.
 */

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#pragma comment(lib, "ws2_32.lib")

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

/* Version anchors (pivot.exe 5.2.11, ImageBase 0x400000). The VMT layout
 * offsets are Delphi-11 specific and were pinned empirically. */
#define IMAGEBASE           0x400000
#define VMT_OFFSET          12           /* vmt base = classType - 12      */
#define OFF_VMT_METHODTABLE 0x34         /* method table ptr @ vmt base-52 */
#define OFF_VMT_FIELDTABLE  0x38         /* field table ptr  @ vmt base-56 */
#define OFF_VMT_CLASSNAME   0x2C         /* class name ptr   @ vmt base-44 */
#define OFF_VMT_INSTANCESIZE 0x28

static HMODULE    g_appBase  = NULL;
static DWORD_PTR  g_vmtBase  = 0;
static void*      g_mainForm = NULL;
static lua_State* g_L        = NULL;
static volatile LONG g_frame = 0;
static FILE*      g_log      = NULL;

static void console_process_cmds(void);
static void bridge_process(void);
static void console_write(const char* s);

/* Log unhandled exceptions (access violations) so crashes are diagnosable. */
static LONG WINAPI crash_filter(struct _EXCEPTION_POINTERS* ep)
{
    if (g_log) {
        fprintf(g_log, "\n=== CRASH 0x%08X at 0x%p ===\n",
                ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
        fflush(g_log);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD_PTR rebase(DWORD_PTR anchor) { return (DWORD_PTR)g_appBase + (anchor - IMAGEBASE); }
static uint16_t  ru16(DWORD_PTR va) { return *(uint16_t*)va; }
static uint32_t  ru32(DWORD_PTR va) { return *(uint32_t*)va; }

/* Find TypeInfo for a class by name: scans the image for the Pascal
 * shortstring (length-prefixed) preceded by the tkClass kind byte (7). */
static DWORD_PTR find_class_typeinfo(const char* name)
{
    size_t len = strlen(name);
    if (len == 0 || len > 250) return 0;
    BYTE* base = (BYTE*)g_appBase;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_appBase;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)g_appBase + dos->e_lfanew);
    size_t imageSize = nt->OptionalHeader.SizeOfImage;
    for (size_t off = 0; off + 2 + len < imageSize; off++) {
        if (base[off] == 0x07 && base[off + 1] == (BYTE)len &&
            memcmp(base + off + 2, name, len) == 0)
            return (DWORD_PTR)(base + off);
    }
    return 0;
}

/* Delphi published method table: [u16 count][{u16 size,u32 addr,shortstr name}] */
static DWORD_PTR find_method_in_table(DWORD_PTR tableVa, const char* name)
{
    if (!tableVa) return 0;
    uint16_t count = ru16(tableVa);
    DWORD_PTR p = tableVa + 2;
    size_t want = strlen(name);
    for (int i = 0; i < count; i++) {
        uint8_t nlen = *(uint8_t*)(p + 6);
        const char* n = (const char*)(p + 7);
        if (nlen == want && memcmp(n, name, nlen) == 0)
            return ru32(p + 2);
        p += 6 + 1 + nlen;
    }
    return 0;
}

/* A plausible published field table: pointer inside the image and a sane
 * count. Guards the parent-chain walk against corrupt/garbage RTTI pointers
 * (TMainForm's inherited tables trail off into junk that would otherwise be
 * read as a huge entry count and hang the enumeration). */
static int valid_field_table(DWORD_PTR ft)
{
    DWORD_PTR base = (DWORD_PTR)g_appBase;
    if (!ft || ft < base || ft >= base + 0x1000000) return 0;
    uint16_t c = 0;
    __try { c = *(uint16_t*)ft; } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return c > 0 && c <= 4096;
}

/* Delphi published field table: [u16 count][u32 parentTable][{u32 off,u16 idx,shortstr name}] */
static long find_field_in_table(DWORD_PTR tableVa, const char* name)
{
    if (!tableVa) return -1;
    uint16_t count = ru16(tableVa);
    DWORD_PTR p = tableVa + 2 + 4;      /* skip count + parent table pointer */
    size_t want = strlen(name);
    for (int i = 0; i < count; i++) {
        uint32_t offset  = ru32(p);
        uint8_t  nlen    = *(uint8_t*)(p + 6);
        const char* n    = (const char*)(p + 7);
        if (nlen == want && memcmp(n, name, nlen) == 0)
            return (long)offset;
        p += 6 + 1 + nlen;
    }
    return -1;
}

/* An FMX/Delphi object's first dword is its VMT (classType); the VMT base
 * that the published tables hang off is classType - 12. */
static DWORD_PTR obj_vmt_base(void* obj)
{
    if (!obj) return 0;
    DWORD_PTR ct = 0;
    __try { ct = *(DWORD_PTR*)obj; } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return ct ? ct - VMT_OFFSET : 0;
}

static DWORD_PTR find_method_generic(void* obj, const char* name)
{
    DWORD_PTR vb = obj_vmt_base(obj);
    if (!vb) return 0;
    return find_method_in_table(ru32(vb - OFF_VMT_METHODTABLE), name);
}

/* Walks the parent field-table chain too, so inherited fields resolve. */
static long find_field_generic(void* obj, const char* name)
{
    DWORD_PTR vb = obj_vmt_base(obj);
    if (!vb) return -1;
    DWORD_PTR ft = ru32(vb - OFF_VMT_FIELDTABLE);
    for (int depth = 0; ft && valid_field_table(ft) && depth < 16; depth++) {
        long off = find_field_in_table(ft, name);
        if (off >= 0) return off;
        ft = ru32(ft + 2);
    }
    return -1;
}

/* Best-effort class name of an object. FMX framework classes don't always
 * put the name at the same VMT slot, so try a few candidate offsets. */
static void obj_classname(void* obj, char* buf, size_t bufsz)
{
    buf[0] = 0;
    if (!obj) return;
    DWORD_PTR ct = 0;
    __try { ct = *(DWORD_PTR*)obj; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!ct) return;
    DWORD_PTR vb = ct - VMT_OFFSET;
    static const int candOffsets[] = { OFF_VMT_CLASSNAME, 0x30, 0x28, 0x34 };
    for (int i = 0; i < 4; i++) {
        DWORD_PTR cn = 0;
        __try { cn = ru32(vb - candOffsets[i]); } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!cn) continue;
        uint8_t clen = 0;
        __try { clen = *(uint8_t*)cn; } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (clen < 2 || clen > 64) continue;
        int ok = 1;
        for (int b = 0; b < clen; b++) {
            char c = ((char*)cn)[1 + b];
            if (!(c >= 0x20 && c < 0x7f)) { ok = 0; break; }
        }
        if (!ok) continue;
        memcpy(buf, (void*)(cn + 1), clen);
        buf[clen] = 0;
        return;
    }
}

static void collect_class_instances(DWORD_PTR vmt, void** out, int max);

/* Heap-scan for the first live instance of a class by its VMT pointer. */
static void* scan_for_class_instance(DWORD_PTR vmt)
{
    void* out = NULL;
    collect_class_instances(vmt, &out, 1);
    return out;
}

/* Heap-scan collecting up to `max` live instances of a class (same plausibility
 * checks as scan_for_class_instance). */
static void collect_class_instances(DWORD_PTR vmt, void** out, int max)
{
    if (!vmt || !out || max <= 0) return;
    int found = 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    DWORD_PTR addr = (DWORD_PTR)si.lpMinimumApplicationAddress;
    DWORD_PTR maxA = (DWORD_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxA && found < max) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
            DWORD prot = mbi.Protect;
            if (!(prot & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (prot & 0xFF) != PAGE_EXECUTE &&
                (prot & 0xFF) != PAGE_EXECUTE_READ &&
                (prot & 0xFF) != PAGE_EXECUTE_READWRITE) {
                uint8_t* p = (uint8_t*)mbi.BaseAddress;
                size_t size = mbi.RegionSize;
                size_t i = 0;
                while (i + sizeof(void*) <= size && found < max) {
                    DWORD_PTR val = 0;
                    __try { val = *(DWORD_PTR*)(p + i); }
                    __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                    if (val == vmt) {
                        DWORD_PTR vb = vmt - VMT_OFFSET;
                        uint32_t isize = 0;
                        __try { isize = *(uint32_t*)(vb - OFF_VMT_INSTANCESIZE); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { isize = 0; }
                        if (isize >= 0x20 && isize <= 0x20000) {
                            if ((size - i) >= isize + 0x40 && i >= 0x40) {
                                out[found++] = (void*)(p + i);
                            }
                        }
                    }
                    i += 4;
                }
            }
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
}

/* The TMainForm anchor is found dynamically (image scan), so no hardcoded
 * TypeInfo address is needed. */
static int init_rtti(void)
{
    DWORD_PTR ti = find_class_typeinfo("TMainForm");    if (!ti) return 0;
    if (*(uint8_t*)ti != 7) return 0;
    uint8_t nlen = *(uint8_t*)(ti + 1);
    uint32_t classType = ru32(ti + 2 + nlen);
    g_vmtBase = (DWORD_PTR)classType - VMT_OFFSET;
    DWORD_PTR cn = ru32(g_vmtBase - OFF_VMT_CLASSNAME);
    if (!cn) return 0;
    uint8_t clen = *(uint8_t*)cn;
    if (clen == 9 && memcmp((void*)(cn + 1), "TMainForm", 9) == 0)
        return 1;
    return 0;
}

/* Heap-scan for TMainForm. The extra check on MainMenu1 (offset 0x2F8)
 * distinguishes the real form from class-reference list entries, which also
 * begin with the TMainForm VMT. */
static void* scan_for_vmt_instance(void)
{
    DWORD_PTR vmt = g_vmtBase + VMT_OFFSET;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    DWORD_PTR addr = (DWORD_PTR)si.lpMinimumApplicationAddress;
    DWORD_PTR maxA = (DWORD_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxA) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
            DWORD prot = mbi.Protect;
            if (!(prot & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (prot & 0xFF) != PAGE_EXECUTE &&
                (prot & 0xFF) != PAGE_EXECUTE_READ &&
                (prot & 0xFF) != PAGE_EXECUTE_READWRITE) {
                uint8_t* p = (uint8_t*)mbi.BaseAddress;
                size_t size = mbi.RegionSize;
                size_t i = 0;
                while (i + sizeof(void*) <= size) {
                    DWORD_PTR val = 0;
                    __try { val = *(DWORD_PTR*)(p + i); }
                    __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                    if (val == vmt) {
                        void* cand = (void*)(p + i);
                        DWORD_PTR cn = 0;
                        __try { cn = *(DWORD_PTR*)(g_vmtBase - OFF_VMT_CLASSNAME); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { cn = 0; }
                        if (!cn) { i += 4; continue; }
                        uint8_t clen = 0;
                        __try { clen = *(uint8_t*)cn; }
                        __except(EXCEPTION_EXECUTE_HANDLER) { clen = 0; }
                        if (clen != 9 || *(uint64_t*)(cn+1) != *(uint64_t*)"TMainFor") {
                            i += 4; continue;
                        }
                        uint32_t isize = 0;
                        __try { isize = *(uint32_t*)(g_vmtBase - OFF_VMT_INSTANCESIZE); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { isize = 0x724; }
                        if (isize < 0x200) isize = 0x724;
                        size_t room = size - i;
                        if (room < isize + 0x40 || i < 0x40) { i += 4; continue; }
                        /* A real form's MainMenu1 (off 0x2F8) points to a live
                           object whose VMT is inside the pivot.exe image. */
                        DWORD_PTR mm = 0;
                        __try { mm = *(uint32_t*)((char*)cand + 0x2F8); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { mm = 0; }
                        if (mm && (mm & 3) == 0) {
                            DWORD_PTR mmvmt = 0;
                            __try { mmvmt = *(uint32_t*)mm; }
                            __except(EXCEPTION_EXECUTE_HANDLER) { mmvmt = 0; }
                            if (mmvmt >= (DWORD_PTR)g_appBase &&
                                mmvmt < (DWORD_PTR)g_appBase + 0x1000000)
                                return cand;
                        }
                        i += 4;
                    }
                    i += 4;
                }
            }
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    return NULL;
}

/* Delphi register calling convention: EAX=Self, EDX=arg1, ECX=arg2, the rest
 * on the stack (pushed right-to-left). The callee cleans stack args via ret N.
 * We call via `call ebx` so the method's ret N pops our stack args + the
 * return address; the __cdecl caller (api_call) then cleans our 6 args. */
__declspec(naked) static void* __cdecl delphi_call_n(void* self, void* fn,
        void* a1, void* a2, const uint32_t* more, int nMore)
{
    __asm {
        mov  eax, [esp+4]    /* self  */
        mov  ebx, [esp+8]    /* fn    */
        mov  edx, [esp+12]   /* a1    */
        mov  ecx, [esp+16]   /* a2    */
        mov  esi, [esp+20]   /* more  */
        mov  edi, [esp+24]   /* nMore */
        test edi, edi
        jz   push_done
        lea  esi, [esi + edi*4]
    push_loop:
        sub  esi, 4
        push dword ptr [esi]
        dec  edi
        jnz  push_loop
    push_done:
        call ebx
        ret
    }
}

static void* delphi_call(void* self, void* fn, void* a1, void* a2)
{
    return delphi_call_n(self, fn, a1, a2, NULL, 0);
}

/* --------------------------------------------------------------------------
 * Delphi UnicodeString helpers
 *
 * A 32-bit Delphi `string` (UnicodeString) is a pointer to the first wide
 * character; 12 bytes before it sits the header:
 *   [0] u16 CodePage (1200=UTF-16)  [2] u16 ElemSize (2)
 *   [4] int RefCount               [8] int Length (chars)
 * `make_delphi_string` builds a string whose RefCount is -1, which Delphi
 * treats as a CONSTANT string (like a string literal): it will never try to
 * free it, so there is no risk of the RTL memory manager touching our
 * HeapAlloc'd block. `call_string` frees it itself after the synchronous
 * call; strings written into fields are leaked by design (the app owns them
 * for display and will never release them).
 * ------------------------------------------------------------------------ */
static void* make_delphi_string(const char* utf8)
{
    if (!utf8) utf8 = "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0) - 1; /* drop NUL */
    if (wlen < 0) wlen = 0;
    size_t bytes = 12 + (size_t)(wlen + 1) * 2;
    BYTE* block = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!block) return NULL;
    *(uint16_t*)(block + 0) = 1200;    /* CP_UTF16 */
    *(uint16_t*)(block + 2) = 2;       /* elem size */
    *(int32_t*)(block + 4) = -1;       /* constant string: app never frees it */
    *(int32_t*)(block + 8) = wlen;     /* char length */
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (WCHAR*)(block + 12), wlen + 1);
    else
        *(uint16_t*)(block + 12) = 0;
    return block + 12;                 /* the string data pointer */
}

static void free_delphi_string(void* data)
{
    if (!data) return;
    HeapFree(GetProcessHeap(), 0, (BYTE*)data - 12);
}

static void read_delphi_string(void* data, char* buf, size_t bufsz)
{
    buf[0] = 0;
    if (!data || bufsz < 2) return;
    __try {
        int32_t len = *(int32_t*)((BYTE*)data - 4);
        if (len < 0) len = 0;
        if (len > (int)(bufsz / 2)) len = (int)(bufsz / 2);
        int wlen = WideCharToMultiByte(CP_UTF8, 0, (WCHAR*)data, len,
                                       buf, (int)bufsz - 1, NULL, NULL);
        if (wlen < 0) wlen = 0;
        buf[wlen] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { buf[0] = 0; }
}

/* Best-effort: does `p` look like a live Delphi object (its first dword is a
 * VMT inside pivot.exe and its instance size is plausible)? Used by the Lua
 * abstraction layer to decide whether a field value is an object to wrap. */
static int is_object_ptr(void* p)
{
    if (!p || ((DWORD_PTR)p & 3)) return 0;
    DWORD_PTR vmt = 0;
    __try { vmt = *(DWORD_PTR*)p; } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!vmt) return 0;
    DWORD_PTR base = (DWORD_PTR)g_appBase;
    if (vmt < base || vmt >= base + 0x1000000) return 0;
    DWORD_PTR vb = vmt - VMT_OFFSET;
    uint32_t isize = 0;
    __try { isize = *(uint32_t*)(vb - OFF_VMT_INSTANCESIZE); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return isize >= 0x20 && isize <= 0x20000;
}

/* Best-effort: does `p` look like a Delphi UnicodeString (header 12 bytes
 * before the data)? Used by hooks to pass string arguments to Lua directly. */
static int is_delphi_string(void* p)
{
    if (!p || ((DWORD_PTR)p & 1)) return 0;
    __try {
        int32_t len = *(int32_t*)((BYTE*)p - 4);
        uint16_t esz = *(uint16_t*)((BYTE*)p - 10);
        uint16_t cp  = *(uint16_t*)((BYTE*)p - 12);
        if (len < 0 || len > 2048) return 0;
        if (esz != 2) return 0;
        if (cp != 1200 && cp != 0) return 0;
        volatile uint16_t c = *(volatile uint16_t*)p; (void)c;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* Push one hook argument: Delphi strings become Lua strings, everything else
 * becomes an integer (raw register/stack value). */
static void push_hook_arg(lua_State* L, void* v)
{
    if (is_delphi_string(v)) {
        char buf[1024];
        read_delphi_string(v, buf, sizeof(buf));
        lua_pushstring(L, buf);
    } else {
        lua_pushinteger(L, (lua_Integer)(DWORD_PTR)v);
    }
}

/* Find the bytes cleaned by a method's epilogue (`ret N`), so an overridden
 * hook can clean the stack args just like the real method would. Scans the
 * method body for the last C3 (ret) / C2 xx xx (ret imm16) before the int3
 * padding that typically follows a compiled function. */
static int method_ret_cleanup(DWORD_PTR fn)
{
    int best = -1;
    __try {
        for (int i = 0; i < 400; i++) {
            BYTE b = *(BYTE*)(fn + i);
            if (b == 0xCC) break;                      /* padding -> function end */
            if (b == 0xC3) best = 0;
            else if (b == 0xC2) best = *(uint16_t*)(fn + i + 1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { best = -1; }
    return best;
}

/* --------------------------------------------------------------------------
 * Method hooking (inline detour)
 *
 * A published method's entry is patched with a 5-byte `E9 rel32` jump to a
 * per-hook stub. The stub saves the register args to globals, calls a C
 * callback that runs the Lua hook, then either:
 *   - returns the Lua value (override; original is skipped), or
 *   - reloads the args and jumps into a trampoline that replays the original
 *     prologue bytes and continues at method+6.
 *
 * The trampoline copies 6 bytes (not 5) so we never split a multi-byte
 * instruction from the original prologue.
 * ------------------------------------------------------------------------ */

#define MAX_HOOKS 32
typedef struct {
    DWORD_PTR method;
    BYTE      orig[6];     /* original prologue bytes */
    BYTE*     trampMem;    /* executable trampoline */
    int       luaRef;
    int       cleanupBytes; /* bytes the original method's `ret N` pops */
    int       inUse;
} HookRec;

static HookRec g_hooks[MAX_HOOKS];
static BYTE*  g_hookStubs[MAX_HOOKS];
static DWORD_PTR g_hookResult;
static DWORD_PTR g_saveSelf, g_saveA1, g_saveA2, g_saveS1, g_saveS2, g_saveS3, g_saveS4;

static int hook_callback(int idx)
{
    HookRec* h = &g_hooks[idx];
    if (!g_L || h->luaRef == LUA_NOREF) return 0;
    __try {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, h->luaRef);
        if (!lua_isfunction(g_L, -1)) { lua_pop(g_L, 1); return 0; }
        lua_pushlightuserdata(g_L, (void*)g_saveSelf);
        push_hook_arg(g_L, (void*)g_saveA1);
        push_hook_arg(g_L, (void*)g_saveA2);
        push_hook_arg(g_L, (void*)g_saveS1);
        push_hook_arg(g_L, (void*)g_saveS2);
        push_hook_arg(g_L, (void*)g_saveS3);
        push_hook_arg(g_L, (void*)g_saveS4);
        if (lua_pcall(g_L, 7, 1, 0) != LUA_OK) {
            if (g_log) { fprintf(g_log, "hook err: %s\n", lua_tostring(g_L, -1)); fflush(g_log); }
            lua_pop(g_L, 1);
            return 0;
        }
        int overrode = !lua_isnil(g_L, -1);
        if (overrode) {
            if (lua_isstring(g_L, -1)) {
                /* Override with a string: build a constant Delphi string (never
                 * freed by the app) and hand its pointer back in EAX. */
                void* s = make_delphi_string(lua_tostring(g_L, -1));
                g_hookResult = (DWORD_PTR)s;
            } else {
                g_hookResult = (DWORD_PTR)lua_tointeger(g_L, -1);
            }
        }
        lua_pop(g_L, 1);
        return overrode;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (g_log) { fprintf(g_log, "hook callback caught AV\n"); fflush(g_log); }
        return 0;
    }
}

static int install_hook(void* obj, const char* methodName, int luaRef)
{
    DWORD_PTR fn = find_method_generic(obj, methodName);
    if (!fn) return 0;

    int idx = -1;
    for (int i = 0; i < MAX_HOOKS; i++) if (!g_hooks[i].inUse) { idx = i; break; }
    if (idx < 0) return 0;

    HookRec* h = &g_hooks[idx];
    memset(h, 0, sizeof(*h));
    h->method = fn;
    h->luaRef = luaRef;
    h->inUse = 1;
    h->cleanupBytes = method_ret_cleanup(fn);
    if (h->cleanupBytes < 0) h->cleanupBytes = 0;

    memcpy(h->orig, (void*)fn, 6);

    h->trampMem = (BYTE*)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!h->trampMem) { h->inUse = 0; return 0; }
    memcpy(h->trampMem, h->orig, 6);
    h->trampMem[6] = 0xE9;
    *(int32_t*)(h->trampMem + 7) = (int32_t)((BYTE*)(fn + 6) - (h->trampMem + 11));

    /* Hand-assembled per-hook stub. Saves the register args (EAX/EDX/ECX) and
     * the first four stack args (arg3..arg6), calls hook_callback(idx), then
     * either returns g_hookResult cleaning the original method's stack args
     * (override) or reloads the register args and jumps into the trampoline. */
    BYTE stub[160];
    int p = 0;
    stub[p++] = 0xA3;                                    /* mov [g_saveSelf], eax */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveSelf; p += 4;
    stub[p++] = 0x89; stub[p++] = 0x15;                  /* mov [g_saveA1], edx */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveA1; p += 4;
    stub[p++] = 0x89; stub[p++] = 0x0D;                  /* mov [g_saveA2], ecx */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveA2; p += 4;
    for (int s = 0; s < 4; s++) {                        /* mov eax,[esp+4+4s] */
        stub[p++] = 0x8B; stub[p++] = 0x44; stub[p++] = 0x24;
        stub[p++] = 0x04 + 4 * (BYTE)s;
        stub[p++] = 0xA3;                                /* mov [g_saveS#], eax */
        *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)(&g_saveS1 + s); p += 4;
    }
    /* call hook_callback(idx); keep the stack 16-byte aligned. */
    stub[p++] = 0x83; stub[p++] = 0xEC; stub[p++] = 0x0C; /* sub esp,12 */
    stub[p++] = 0x68; *(uint32_t*)(stub + p) = (uint32_t)idx; p += 4;  /* push idx */
    int callInstr = p;
    stub[p++] = 0xE8;
    int callDisp = p;
    stub[p++] = 0x00; stub[p++] = 0x00; stub[p++] = 0x00; stub[p++] = 0x00;
    stub[p++] = 0x83; stub[p++] = 0xC4; stub[p++] = 0x10; /* add esp,16 */
    stub[p++] = 0x85; stub[p++] = 0xC0;                  /* test eax,eax */
    stub[p++] = 0x74; stub[p++] = 0x06;                  /* je +6 -> call_original */
    stub[p++] = 0xA1;                                    /* mov eax,[g_hookResult] */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_hookResult; p += 4;
    if (h->cleanupBytes > 0) {                           /* ret N  (stack args) */
        stub[p++] = 0xC2;
        stub[p++] = (BYTE)(h->cleanupBytes & 0xFF);
        stub[p++] = (BYTE)((h->cleanupBytes >> 8) & 0xFF);
    } else {
        stub[p++] = 0xC3;                                /* ret */
    }
    stub[p++] = 0xA1;                                    /* mov eax,[g_saveSelf] */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveSelf; p += 4;
    stub[p++] = 0x8B; stub[p++] = 0x15;                  /* mov edx,[g_saveA1] */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveA1; p += 4;
    stub[p++] = 0x8B; stub[p++] = 0x0D;                  /* mov ecx,[g_saveA2] */
    *(uint32_t*)(stub + p) = (uint32_t)(DWORD_PTR)&g_saveA2; p += 4;
    int jmpInstr = p;
    stub[p++] = 0xE9;
    int jmpDisp = p;
    stub[p++] = 0x00; stub[p++] = 0x00; stub[p++] = 0x00; stub[p++] = 0x00;

    BYTE* stubMem = (BYTE*)VirtualAlloc(NULL, p, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stubMem) { h->inUse = 0; return 0; }
    memcpy(stubMem, stub, p);
    g_hookStubs[idx] = stubMem;

    /* disp = target - (address after the instruction) */
    *(int32_t*)(stubMem + callDisp) = (int32_t)((BYTE*)hook_callback - (stubMem + callInstr + 5));
    *(int32_t*)(stubMem + jmpDisp) = (int32_t)(h->trampMem - (stubMem + jmpInstr + 5));

    DWORD old;
    if (!VirtualProtect((void*)fn, 5, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(stubMem, 0, MEM_RELEASE); h->inUse = 0; return 0;
    }
    *(BYTE*)fn = 0xE9;
    *(int32_t*)(fn + 1) = (int32_t)(stubMem - ((BYTE*)fn + 5));
    VirtualProtect((void*)fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)fn, 5);

    if (g_log) { fprintf(g_log, "pivotkit: hooked %s @ %p (cleanup=%d)\n", methodName, (void*)fn, h->cleanupBytes); fflush(g_log); }
    return 1;
}

static int unhook_hookidx(int hookIdx)
{
    HookRec* h = &g_hooks[hookIdx];
    if (!h->inUse) return 0;
    DWORD old;
    if (VirtualProtect((void*)h->method, 5, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)h->method, h->orig, 5);
        VirtualProtect((void*)h->method, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)h->method, 5);
    }
    if (g_hookStubs[hookIdx]) VirtualFree(g_hookStubs[hookIdx], 0, MEM_RELEASE);
    if (h->trampMem) VirtualFree(h->trampMem, 0, MEM_RELEASE);
    if (g_L && h->luaRef != LUA_NOREF) luaL_unref(g_L, LUA_REGISTRYINDEX, h->luaRef);
    h->inUse = 0;
    return 1;
}

/* --------------------------------------------------------------------------
 * Generic sprite overlays + floating menu button
 *
 * `pivot.sprite(path)` loads an image into a layered always-on-top window and
 * returns a Lua object you can move, give a velocity (px/frame), bounce off
 * the Pivot window edges, show/hide, and destroy. `pivot.add_menu_button`
 * puts a small button on the Pivot window that calls a Lua function.
 * ------------------------------------------------------------------------ */

#define MAX_SPRITES 16

typedef struct {
    HWND     hwnd;
    HBITMAP  bmp;
    int      w, h;
    double   x, y, vx, vy;
    int      bounce;
    int      alive;
} Sprite;

static Sprite    g_sprites[MAX_SPRITES];
static HWND      g_btnHwnd = NULL;
static WNDPROC   g_btnProc = NULL;
static int       g_btnLuaRef = LUA_NOREF;
static ULONG_PTR g_gdiplusToken = 0;

/* Find the main Pivot window: the LARGEST visible top-level window of this
 * process with a title. FMX also creates hidden/zero-size helper windows, so
 * "first visible" is unreliable — the main form is the biggest one. */
struct MainWinSearch { LONG area; HWND hwnd; };

static BOOL CALLBACK main_win_enum(HWND h, LPARAM l)
{
    MainWinSearch* s = (MainWinSearch*)l;
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindowTextLengthW(h) < 1) return TRUE;
    RECT rc;
    GetWindowRect(h, &rc);
    LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > s->area) { s->area = area; s->hwnd = h; }
    return TRUE;
}

static HWND find_main_hwnd(void)
{
    /* Cache to keep EnumWindows off the hot path (input polling calls
     * window_rect every frame). Re-enumerate every 2s or on invalid hwnd. */
    static HWND cached = NULL;
    static DWORD lastCheck = 0;
    DWORD now = GetTickCount();
    if (cached && IsWindow(cached) && (now - lastCheck) < 2000)
        return cached;
    MainWinSearch st = { 0, NULL };
    EnumWindows(main_win_enum, (LPARAM)&st);
    lastCheck = now;
    if (st.hwnd) cached = st.hwnd;
    return st.hwnd ? st.hwnd : cached;
}

static void resolve_path(char* out, size_t outsz, const char* rel)
{
    if (rel[0] == '\\' || (rel[0] != 0 && rel[1] == ':')) {
        strncpy(out, rel, outsz - 1);
        out[outsz - 1] = 0;
        return;
    }
    GetModuleFileNameA((HMODULE)g_appBase, out, (DWORD)outsz);
    char* slash = strrchr(out, '\\');
    if (slash) *slash = '\0';
    strncat(out, "\\", outsz - strlen(out) - 1);
    strncat(out, rel, outsz - strlen(out) - 1);
}

/* Main Pivot window's client rect in SCREEN coordinates. Layered windows are
 * positioned in screen space, so sprites must be tracked in screen space too. */
static void get_main_client_screen_rect(RECT* out)
{
    HWND main = find_main_hwnd();
    RECT rc = {0, 0, 800, 600};
    if (main) GetClientRect(main, &rc);
    POINT tl = {0, 0};
    if (main) ClientToScreen(main, &tl);
    out->left   = tl.x;
    out->top    = tl.y;
    out->right  = tl.x + rc.right;
    out->bottom = tl.y + rc.bottom;
}

static LRESULT CALLBACK sprite_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;   /* clicks pass through */
    return DefWindowProcW(h, msg, wp, lp);
}

static void sprite_register_class(void)
{
    static bool done = false;
    if (done) return;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = sprite_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PivotKitSprite";
    RegisterClassW(&wc);
    done = true;
}

static int api_sprite(lua_State* L)
{
    const char* rel = luaL_checkstring(L, 1);
    char path[MAX_PATH];
    resolve_path(path, sizeof(path), rel);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return luaL_error(L, "pivot.sprite: no such file: %s", path);

    int slot = -1;
    for (int i = 0; i < MAX_SPRITES; i++) if (!g_sprites[i].alive) { slot = i; break; }
    if (slot < 0) return luaL_error(L, "pivot.sprite: too many sprites (max %d)", MAX_SPRITES);

    if (g_gdiplusToken == 0) {
        GdiplusStartupInput in;
        if (GdiplusStartup(&g_gdiplusToken, &in, NULL) != Ok)
            return luaL_error(L, "pivot.sprite: GDI+ init failed");
    }

    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
    Bitmap* img = Bitmap::FromFile(wpath);
    if (img->GetLastStatus() != Ok) {
        delete img;
        return luaL_error(L, "pivot.sprite: failed to decode %s", path);
    }
    int w = (int)img->GetWidth();
    int h = (int)img->GetHeight();
    if (w < 1 || h < 1 || w > 2048 || h > 2048) {
        delete img;
        return luaL_error(L, "pivot.sprite: bad image size");
    }

    /* Render into a 32-bit DIB so the layered window can use it. */
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;              /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dibDC = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dibDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP old = (HBITMAP)SelectObject(dibDC, bmp);
    {
        Graphics g(dibDC);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(img, 0, 0, w, h);
    }
    delete img;

    sprite_register_class();
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
        L"PivotKitSprite", L"", WS_POPUP,
        0, 0, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd) {
        SelectObject(dibDC, old);
        DeleteObject(bmp);
        DeleteDC(dibDC);
        return luaL_error(L, "pivot.sprite: CreateWindowEx failed");
    }

    Sprite* s = &g_sprites[slot];
    s->hwnd = hwnd;
    s->bmp = bmp;
    s->w = w;
    s->h = h;
    s->x = 0;
    s->y = 0;
    s->vx = 0;
    s->vy = 0;
    s->bounce = 0;
    s->alive = 1;

    HDC screen = GetDC(NULL);
    SIZE sz = { w, h };
    POINT dst = { 0, 0 };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screen, &dst, &sz, dibDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
    SelectObject(dibDC, old);
    DeleteDC(dibDC);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    lua_pushinteger(L, slot + 1);    /* 1-based sprite handle */
    return 1;
}

/* Destroy a sprite by handle. Returns 1 if found. */
static int sprite_destroy_handle(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive)
        return 0;
    Sprite* s = &g_sprites[slot];
    DestroyWindow(s->hwnd);
    DeleteObject(s->bmp);
    memset(s, 0, sizeof(*s));
    return 1;
}

static int api_sprite_move(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive)
        return 0;
    Sprite* s = &g_sprites[slot];
    s->x = luaL_checknumber(L, 2);
    s->y = luaL_checknumber(L, 3);
    SetWindowPos(s->hwnd, HWND_TOPMOST, (int)s->x, (int)s->y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    return 0;
}

static int api_sprite_vel(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive)
        return 0;
    g_sprites[slot].vx = luaL_checknumber(L, 2);
    g_sprites[slot].vy = luaL_checknumber(L, 3);
    return 0;
}

static int api_sprite_bounce(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive)
        return 0;
    g_sprites[slot].bounce = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}

static int api_sprite_show(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive) return 0;
    ShowWindow(g_sprites[slot].hwnd, SW_SHOWNOACTIVATE);
    return 0;
}

static int api_sprite_hide(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive) return 0;
    ShowWindow(g_sprites[slot].hwnd, SW_HIDE);
    return 0;
}

static LRESULT CALLBACK btn_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_LBUTTONUP && g_L && g_btnLuaRef != LUA_NOREF) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_btnLuaRef);
        if (lua_isfunction(g_L, -1)) {
            if (lua_pcall(g_L, 0, 0, 0) != LUA_OK) {
                if (g_log) { fprintf(g_log, "pivotkit: button err: %s\n", lua_tostring(g_L, -1)); fflush(g_log); }
                lua_pop(g_L, 1);
            }
        } else lua_pop(g_L, 1);
    }
    return CallWindowProcW(g_btnProc, h, msg, wp, lp);
}

/* Create (or retitle) the floating button on the main window. */
static int api_add_menu_button(lua_State* L)
{
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (g_btnLuaRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, g_btnLuaRef);
    lua_pushvalue(L, 2);
    g_btnLuaRef = luaL_ref(L, LUA_REGISTRYINDEX);

    HWND main = find_main_hwnd();
    if (!main) return luaL_error(L, "pivot.add_menu_button: main window not found");

    if (!g_btnHwnd) {
        g_btnHwnd = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 110, 24, main, NULL, GetModuleHandleW(NULL), NULL);
        if (g_btnHwnd)
            g_btnProc = (WNDPROC)SetWindowLongPtrW(g_btnHwnd, GWLP_WNDPROC, (LONG_PTR)btn_wndproc);
    }
    SetWindowTextA(g_btnHwnd, label);

    /* Pin it to the top-right of the window; re-anchored on every tick. */
    RECT rc; GetClientRect(main, &rc);
    MoveWindow(g_btnHwnd, rc.right - 122, 8, 110, 24, TRUE);
    lua_pushboolean(L, 1);
    return 1;
}

static int api_remove_menu_button(lua_State* L)
{
    if (g_btnHwnd) { DestroyWindow(g_btnHwnd); g_btnHwnd = NULL; }
    if (g_btnLuaRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, g_btnLuaRef);
    g_btnLuaRef = LUA_NOREF;
    return 0;
}

/* --------------------------------------------------------------------------
 * Canvas overlay (HUD)
 *
 * A transparent always-on-top layered window covering the Pivot window, drawn
 * with GDI+ from a small primitive list. Mods drive it every frame:
 *   pivot.overlay_begin()  -> clear the frame
 *   pivot.overlay_text(x,y,str,size,argb) / overlay_line / overlay_rect /
 *     overlay_circle
 *   pivot.overlay_commit() -> blit to screen
 * Coordinates are in Pivot-window pixels. This draws over Pivot's own canvas
 * without touching its FMX internals.
 * ------------------------------------------------------------------------ */

#define MAX_OVERLAY_PRIMS 512
#define OV_TEXT 1
#define OV_LINE 2
#define OV_RECT 3
#define OV_CIRCLE 4

typedef struct {
    int    type;
    float  x1, y1, x2, y2;
    int    size;
    float  width;
    DWORD  argb;
    char   text[256];
} OverlayPrim;

typedef struct {
    HWND  hwnd;
    int   w, h;
    int   nprims;
    int   alive;
    OverlayPrim prims[MAX_OVERLAY_PRIMS];
} Overlay;

static Overlay g_overlay;

static LRESULT CALLBACK overlay_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

static void overlay_ensure_gdiplus(void)
{
    if (g_gdiplusToken == 0) {
        GdiplusStartupInput in;
        if (GdiplusStartup(&g_gdiplusToken, &in, NULL) != Ok) g_gdiplusToken = 0;
    }
}

static int api_overlay_begin(lua_State* L)
{
    (void)L;
    g_overlay.nprims = 0;
    return 0;
}

static void overlay_add_prim(int type, float x1, float y1, float x2, float y2,
                             int size, float width, DWORD argb, const char* text)
{
    if (!g_overlay.alive) return;
    if (g_overlay.nprims >= MAX_OVERLAY_PRIMS) return;
    OverlayPrim* p = &g_overlay.prims[g_overlay.nprims++];
    p->type = type;
    p->x1 = x1; p->y1 = y1; p->x2 = x2; p->y2 = y2;
    p->size = size; p->width = width; p->argb = argb;
    if (text) { strncpy(p->text, text, sizeof(p->text) - 1); p->text[sizeof(p->text) - 1] = 0; }
    else p->text[0] = 0;
}

static int api_overlay_text(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    const char* s = luaL_checkstring(L, 3);
    int size = lua_isnoneornil(L, 4) ? 12 : (int)luaL_checkinteger(L, 4);
    DWORD argb = lua_isnoneornil(L, 5) ? 0xFFFFFFFF : (DWORD)luaL_checkinteger(L, 5);
    overlay_add_prim(OV_TEXT, x, y, 0, 0, size, 0, argb, s);
    return 0;
}

static int api_overlay_line(lua_State* L)
{
    float x1 = (float)luaL_checknumber(L, 1), y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3), y2 = (float)luaL_checknumber(L, 4);
    DWORD argb = lua_isnoneornil(L, 5) ? 0xFFFFFFFF : (DWORD)luaL_checkinteger(L, 5);
    float w = lua_isnoneornil(L, 6) ? 1.0f : (float)luaL_checknumber(L, 6);
    overlay_add_prim(OV_LINE, x1, y1, x2, y2, 0, w, argb, NULL);
    return 0;
}

static int api_overlay_rect(lua_State* L)
{
    float x1 = (float)luaL_checknumber(L, 1), y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3), y2 = (float)luaL_checknumber(L, 4);
    DWORD argb = lua_isnoneornil(L, 5) ? 0xFFFFFFFF : (DWORD)luaL_checkinteger(L, 5);
    overlay_add_prim(OV_RECT, x1, y1, x2, y2, 0, 1.0f, argb, NULL);
    return 0;
}

static int api_overlay_circle(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    DWORD argb = lua_isnoneornil(L, 4) ? 0xFFFFFFFF : (DWORD)luaL_checkinteger(L, 4);
    overlay_add_prim(OV_CIRCLE, x - r, y - r, x + r, y + r, 0, 1.0f, argb, NULL);
    return 0;
}

static void overlay_destroy_window(void)
{
    if (g_overlay.hwnd) { DestroyWindow(g_overlay.hwnd); g_overlay.hwnd = NULL; }
    g_overlay.w = g_overlay.h = 0;
}

static int api_overlay_create(lua_State* L)
{
    (void)L;
    overlay_ensure_gdiplus();
    if (!g_gdiplusToken) return luaL_error(L, "pivot.overlay_create: GDI+ init failed");
    static bool clsDone = false;
    if (!clsDone) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = overlay_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"PivotKitOverlay";
        RegisterClassW(&wc);
        clsDone = true;
    }
    overlay_destroy_window();
    g_overlay.nprims = 0;
    g_overlay.alive = 1;
    lua_pushboolean(L, 1);
    return 1;
}

static int api_overlay_destroy(lua_State* L)
{
    overlay_destroy_window();
    g_overlay.alive = 0;
    return 0;
}

static int api_overlay_commit(lua_State* L)
{
    (void)L;
    if (!g_overlay.alive || !g_gdiplusToken) return 0;

    HWND main = find_main_hwnd();
    RECT wr;
    GetWindowRect(main ? main : GetDesktopWindow(), &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (w < 1 || h < 1) return 0;

    if (!g_overlay.hwnd || g_overlay.w != w || g_overlay.h != h) {
        overlay_destroy_window();
        g_overlay.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
            L"PivotKitOverlay", L"", WS_POPUP,
            wr.left, wr.top, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!g_overlay.hwnd) return 0;
        g_overlay.w = w; g_overlay.h = h;
    }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dibDC = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dibDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP old = (HBITMAP)SelectObject(dibDC, bmp);
    {
        Graphics g(dibDC);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        for (int i = 0; i < g_overlay.nprims; i++) {
            OverlayPrim* p = &g_overlay.prims[i];
            Color c((BYTE)(p->argb >> 24), (BYTE)(p->argb >> 16),
                    (BYTE)(p->argb >> 8), (BYTE)(p->argb));
            if (p->type == OV_TEXT) {
                WCHAR wtext[256];
                MultiByteToWideChar(CP_UTF8, 0, p->text, -1, wtext, 256);
                FontFamily ff(L"Arial");
                Font f(&ff, (REAL)(p->size > 0 ? p->size : 12), FontStyleRegular, UnitPixel);
                SolidBrush br(c);
                g.DrawString(wtext, -1, &f, PointF(p->x1, p->y1), &br);
            } else if (p->type == OV_LINE) {
                Pen pen(c, p->width > 0 ? p->width : 1.0f);
                g.DrawLine(&pen, p->x1, p->y1, p->x2, p->y2);
            } else if (p->type == OV_RECT) {
                Pen pen(c, 1.0f);
                g.DrawRectangle(&pen, p->x1, p->y1, p->x2 - p->x1, p->y2 - p->y1);
            } else if (p->type == OV_CIRCLE) {
                Pen pen(c, 1.0f);
                g.DrawEllipse(&pen, p->x1, p->y1, p->x2 - p->x1, p->y2 - p->y1);
            }
        }
    }
    HDC screen = GetDC(NULL);
    SIZE sz = { w, h };
    POINT dst = { wr.left, wr.top };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_overlay.hwnd, screen, &dst, &sz, dibDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
    SelectObject(dibDC, old);
    DeleteDC(dibDC);
    DeleteObject(bmp);
    return 0;
}

static int api_sprite_pos(lua_State* L)
{
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot >= MAX_SPRITES || !g_sprites[slot].alive) {
        lua_pushnumber(L, 0); lua_pushnumber(L, 0);
        return 2;
    }
    lua_pushnumber(L, g_sprites[slot].x);
    lua_pushnumber(L, g_sprites[slot].y);
    return 2;
}

/* Main window client area in screen coords: left, top, right, bottom. */
static int api_window_rect(lua_State* L)
{
    RECT rc;
    get_main_client_screen_rect(&rc);
    lua_pushinteger(L, rc.left);
    lua_pushinteger(L, rc.top);
    lua_pushinteger(L, rc.right);
    lua_pushinteger(L, rc.bottom);
    return 4;
}

/* Cursor position in screen coords: x, y. */
static int api_cursor_pos(lua_State* L)
{
    POINT p;
    if (!GetCursorPos(&p)) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 2; }
    lua_pushinteger(L, p.x);
    lua_pushinteger(L, p.y);
    return 2;
}

/* Stepped each frame: move sprites with velocity and bounce them. */
static void sprites_tick(void)
{
    for (int i = 0; i < MAX_SPRITES; i++) {
        Sprite* s = &g_sprites[i];
        if (!s->alive || (s->vx == 0 && s->vy == 0)) continue;

        s->x += s->vx;
        s->y += s->vy;

        if (s->bounce) {
            RECT rc;
            get_main_client_screen_rect(&rc);
            double minX = rc.left, minY = rc.top;
            double maxX = rc.right - s->w, maxY = rc.bottom - s->h;
            if (s->x < minX)         { s->x = minX; s->vx = -s->vx; }
            if (s->y < minY)         { s->y = minY; s->vy = -s->vy; }
            if (s->x > maxX)         { s->x = maxX; s->vx = -s->vx; }
            if (s->y > maxY)         { s->y = maxY; s->vy = -s->vy; }
        }

        SetWindowPos(s->hwnd, HWND_TOPMOST, (int)s->x, (int)s->y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }

    /* Keep the button pinned to the top-right of the window (client coords). */
    HWND main = find_main_hwnd();
    if (main && g_btnHwnd) {
        RECT cr;
        GetClientRect(main, &cr);
        MoveWindow(g_btnHwnd, cr.right - 122, 8, 110, 24, FALSE);
    }
}

/* Per-frame Lua tick via a PeekMessageW IAT hook. */
typedef BOOL (WINAPI *PeekMessageWFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageWFn g_origPeekMessageW = NULL;

static volatile LONG g_pendingLoad = 0;
static char g_modDir[MAX_PATH] = {0};
static LONG g_loadFrameDelay = 0;

static void load_mods(const char* modDir);

static BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMin,
                                      UINT wMax, UINT wRemove)
{
    BOOL r = g_origPeekMessageW(lpMsg, hWnd, wMin, wMax, wRemove);

    LONG frame = InterlockedIncrement(&g_frame);

    /* Load mods once, a few frames after startup so the FMX form (and its
       fields) are fully built. Runs on the main thread, so mod code may call
       Delphi methods safely. */
    if (g_pendingLoad) {
        if (g_loadFrameDelay > 0) {
            g_loadFrameDelay--;
        } else if (InterlockedCompareExchange(&g_pendingLoad, 0, 1) == 1) {
            void* fresh = scan_for_vmt_instance();
            if (fresh) g_mainForm = fresh;
            if (g_log) { fprintf(g_log, "pivotkit: mainForm (final) = %p\n", g_mainForm); fflush(g_log); }
            if (g_L) load_mods(g_modDir);
            if (g_log) { fprintf(g_log, "pivotkit: ready\n"); fflush(g_log); }
        }
    }

    if (g_L) {
        lua_getglobal(g_L, "__pivot_update__");
        if (lua_isfunction(g_L, -1)) {
            lua_pushinteger(g_L, (lua_Integer)frame);
            if (lua_pcall(g_L, 1, 0, 0) != LUA_OK) {
                if (g_log) { fprintf(g_log, "update err: %s\n", lua_tostring(g_L, -1)); fflush(g_log); }
                lua_pop(g_L, 1);
            }
        } else lua_pop(g_L, 1);
    }

    /* Step sprites (bouncing demo) and keep the button anchored. */
    sprites_tick();

    /* Run console + bridge commands on the main thread (Delphi-safe). */
    console_process_cmds();
    bridge_process();

    return r;
}

/* Patch pivot.exe's IAT slot for user32!PeekMessageW. */
static int hook_peek_message(void)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_appBase;
    IMAGE_NT_HEADERS* nt  = (IMAGE_NT_HEADERS*)((BYTE*)g_appBase + dos->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)
        ((BYTE*)g_appBase + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        const char* dll;
        __try { dll = (const char*)g_appBase + imp->Name; }
        __except(EXCEPTION_EXECUTE_HANDLER) { break; }
        if (_stricmp(dll, "user32.dll") != 0) continue;
        IMAGE_THUNK_DATA *oft, *thunk;
        __try {
            oft = (IMAGE_THUNK_DATA*)((BYTE*)g_appBase +
                      (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            thunk = (IMAGE_THUNK_DATA*)((BYTE*)g_appBase + imp->FirstThunk);
        } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
        for (int idx = 0; ; idx++) {
            DWORD_PTR ofVal, iatVal, addrOfData;
            __try {
                ofVal = oft[idx].u1.AddressOfData;
                iatVal = thunk[idx].u1.Function;
            } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
            if (ofVal == 0 && iatVal == 0) break;
            if (IMAGE_SNAP_BY_ORDINAL(ofVal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn;
            __try { ibn = (IMAGE_IMPORT_BY_NAME*)((BYTE*)g_appBase + ofVal); }
            __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
            __try {
                if (strcmp((char*)ibn->Name, "PeekMessageW") == 0) {
                    DWORD old;
                    g_origPeekMessageW = (PeekMessageWFn)iatVal;
                    if (VirtualProtect(&thunk[idx].u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk[idx].u1.Function = (ULONG_PTR)HookedPeekMessageW;
                        VirtualProtect(&thunk[idx].u1.Function, sizeof(void*), old, &old);
                        return 1;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Console (BepInEx-style) + TCP bridge
 *
 * Console: a real Windows console window ("pivotkit console") that receives
 * all pivot.log output and lets you type Lua / pivotlib commands. Enabled by
 * the -console loader flag (PIVOTKIT_CONSOLE env) or at runtime via
 * pivot.console(true). Commands run on the main thread (Delphi-safe).
 *
 * Bridge: a loopback TCP server on 127.0.0.1:50077 (enabled by default) that
 * accepts one text command per connection, runs it on the main thread, and
 * replies with the result — this is what tools/pivotctl.py talks to.
 * ------------------------------------------------------------------------ */

#define MAX_CMD_LINE 512
#define MAX_CMD_QUEUE 256

static HANDLE g_consoleOut = NULL;
static HANDLE g_consoleIn  = NULL;
static int    g_consoleCreated = 0;
static volatile LONG g_cmdHead = 0, g_cmdTail = 0;
static char g_cmdQueue[MAX_CMD_QUEUE][MAX_CMD_LINE];

static void queue_cmd(const char* cmd);
static DWORD WINAPI console_input_thread(LPVOID param);
static void run_lua_command(const char* cmd, char* out, size_t outsz);

static void queue_cmd(const char* cmd)
{
    LONG tail = g_cmdTail;
    LONG next = (tail + 1) % MAX_CMD_QUEUE;
    if (next == g_cmdHead) return;                 /* full */
    strncpy(g_cmdQueue[tail], cmd, MAX_CMD_LINE - 1);
    g_cmdQueue[tail][MAX_CMD_LINE - 1] = 0;
    g_cmdTail = next;
}

static void console_write(const char* s)
{
    if (!g_consoleOut || !s || !*s) return;
    DWORD n = 0;
    WriteConsoleA(g_consoleOut, s, (DWORD)strlen(s), &n, NULL);
}

static int console_create(void)
{
    if (g_consoleCreated) return 1;
    if (!AllocConsole()) return 0;
    g_consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_consoleIn  = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleTitleW(L"pivotkit console — Pivot Animator 5.2.11");
    SetConsoleTextAttribute(g_consoleOut, 0x0F);
    g_consoleCreated = 1;
    console_write("pivotkit console ready. Type Lua/pivotlib commands.\n");
    CreateThread(NULL, 0, console_input_thread, NULL, 0, NULL);
    return 1;
}

static DWORD WINAPI console_input_thread(LPVOID param)
{
    (void)param;
    char line[MAX_CMD_LINE];
    for (;;) {
        DWORD n = 0;
        if (!g_consoleIn || !ReadConsoleA(g_consoleIn, line, MAX_CMD_LINE - 1, &n, NULL))
            break;
        line[n] = 0;
        for (int i = (int)n - 1; i >= 0 && (line[i] == '\n' || line[i] == '\r'); i--)
            line[i] = 0;
        if (line[0]) {
            console_write("> ");
            console_write(line);
            console_write("\n");
            queue_cmd(line);
        }
    }
    return 0;
}

/* Execute one command string. Prefers the Lua-side __pivot_console__ handler
 * (installed by pivotlib) so bare command names work; falls back to a raw
 * dofile-style eval. Writes any result/error into `out`. */
static void run_lua_command(const char* cmd, char* out, size_t outsz)
{
    out[0] = 0;
    if (!g_L) { _snprintf(out, outsz, "error: lua not ready"); return; }
    lua_getglobal(g_L, "__pivot_console__");
    if (lua_isfunction(g_L, -1)) {
        lua_pushstring(g_L, cmd);
        if (lua_pcall(g_L, 1, 1, 0) != LUA_OK) {
            _snprintf(out, outsz, "error: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
            return;
        }
        if (!lua_isnil(g_L, -1))
            _snprintf(out, outsz, "%s", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
    } else {
        lua_pop(g_L, 1);
        if (luaL_dostring(g_L, cmd) != LUA_OK) {
            _snprintf(out, outsz, "error: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    }
}

/* Drain queued console commands on the main thread (called each tick). */
static void console_process_cmds(void)
{
    while (g_cmdHead != g_cmdTail) {
        char cmd[MAX_CMD_LINE];
        strcpy(cmd, g_cmdQueue[g_cmdHead]);
        g_cmdHead = (g_cmdHead + 1) % MAX_CMD_QUEUE;
        char out[1024];
        run_lua_command(cmd, out, sizeof(out));
        if (out[0]) { console_write(out); console_write("\n"); }
    }
}

static int api_console(lua_State* L)
{
    if (lua_isboolean(L, 1)) {
        if (lua_toboolean(L, 1)) {
            if (!g_consoleCreated && !console_create())
                return luaL_error(L, "pivot.console: AllocConsole failed");
            if (GetConsoleWindow()) ShowWindow(GetConsoleWindow(), SW_SHOW);
        } else {
            if (g_consoleCreated && GetConsoleWindow())
                ShowWindow(GetConsoleWindow(), SW_HIDE);
        }
    } else if (!lua_isnoneornil(L, 1)) {
        return luaL_error(L, "pivot.console: expected true/false/nil");
    }
    lua_pushboolean(L, g_consoleCreated && GetConsoleWindow() &&
                        IsWindowVisible(GetConsoleWindow()));
    return 1;
}

/* --------------------------------------------------------------------------
 * TCP bridge
 * ------------------------------------------------------------------------ */

#define BRIDGE_PORT 50077
#define MAX_BRIDGE_REQS 64

typedef struct {
    char  cmd[MAX_CMD_LINE];
    char  result[1024];
    HANDLE done;
} BridgeReq;

static SOCKET g_listenSock = INVALID_SOCKET;
static BridgeReq* g_bridgeReqs[MAX_BRIDGE_REQS];
static volatile LONG g_bridgeHead = 0, g_bridgeTail = 0;

static void bridge_enqueue(BridgeReq* r)
{
    LONG tail = g_bridgeTail;
    LONG next = (tail + 1) % MAX_BRIDGE_REQS;
    if (next == g_bridgeHead) return;
    g_bridgeReqs[tail] = r;
    g_bridgeTail = next;
}

static DWORD WINAPI bridge_conn_thread(LPVOID sockp)
{
    SOCKET s = (SOCKET)(DWORD_PTR)sockp;
    char line[MAX_CMD_LINE];
    int n = recv(s, line, MAX_CMD_LINE - 1, 0);
    if (n > 0) {
        line[n] = 0;
        for (int i = n - 1; i >= 0 && (line[i] == '\n' || line[i] == '\r'); i--)
            line[i] = 0;
        BridgeReq* r = (BridgeReq*)malloc(sizeof(BridgeReq));
        if (r) {
            strncpy(r->cmd, line, MAX_CMD_LINE - 1);
            r->cmd[MAX_CMD_LINE - 1] = 0;
            r->result[0] = 0;
            r->done = CreateEventW(NULL, TRUE, FALSE, NULL);
            bridge_enqueue(r);
            if (WaitForSingleObject(r->done, 8000) == WAIT_OBJECT_0) {
                send(s, r->result, (int)strlen(r->result), 0);
            }
            /* Deliberately leak r (never free / never close the event): the main
             * thread may still be processing it. A couple hundred bytes per
             * command is nothing for a dev tool, and it removes the
             * use-after-free race that was crashing pivot.exe. */
        }
    }
    closesocket(s);
    return 0;
}

static DWORD WINAPI bridge_accept_thread(LPVOID param)
{
    (void)param;
    for (;;) {
        SOCKET c = accept(g_listenSock, NULL, NULL);
        if (c == INVALID_SOCKET) break;
        HANDLE h = CreateThread(NULL, 0, bridge_conn_thread,
                                (void*)(DWORD_PTR)c, 0, NULL);
        if (h) CloseHandle(h); else closesocket(c);
    }
    return 0;
}

static int bridge_start(void)
{
    if (g_listenSock != INVALID_SOCKET) return 1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSock == INVALID_SOCKET) return 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)BRIDGE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);      /* loopback only */
    if (bind(g_listenSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_listenSock); g_listenSock = INVALID_SOCKET;
        return 0;
    }
    listen(g_listenSock, 4);
    CreateThread(NULL, 0, bridge_accept_thread, NULL, 0, NULL);
    return 1;
}

/* Drain queued bridge commands on the main thread (called each tick). */
static void bridge_process(void)
{
    while (g_bridgeHead != g_bridgeTail) {
        BridgeReq* r = g_bridgeReqs[g_bridgeHead];
        g_bridgeHead = (g_bridgeHead + 1) % MAX_BRIDGE_REQS;
        if (r) {
            run_lua_command(r->cmd, r->result, sizeof(r->result));
            SetEvent(r->done);
        }
    }
}

/* --------------------------------------------------------------------------
 * Raw peek: read a typed value at (obj + offset) — for pinning private field
 * offsets of classes like TFigure that expose no published members.
 * ------------------------------------------------------------------------ */

static int api_peek(lua_State* L)
{
    DWORD_PTR addr = 0;
    if (lua_isuserdata(L, 1)) addr = (DWORD_PTR)lua_touserdata(L, 1);
    else if (lua_isnumber(L, 1)) addr = (DWORD_PTR)lua_tointeger(L, 1);
    else return luaL_error(L, "pivot.peek: bad object");
    int off = (int)luaL_checkinteger(L, 2);
    const char* kind = lua_isnoneornil(L, 3) ? "u32" : luaL_checkstring(L, 3);
    void* p = (void*)(addr + off);
    int isFloat = 0;
    uint64_t v = 0;
    __try {
        if      (strcmp(kind, "u8")  == 0) v = *(uint8_t*)p;
        else if (strcmp(kind, "i8")  == 0) v = (int8_t)*(int8_t*)p;
        else if (strcmp(kind, "u16") == 0) v = *(uint16_t*)p;
        else if (strcmp(kind, "i16") == 0) v = (int16_t)*(int16_t*)p;
        else if (strcmp(kind, "u32") == 0) v = *(uint32_t*)p;
        else if (strcmp(kind, "i32") == 0) v = (int32_t)*(int32_t*)p;
        else if (strcmp(kind, "ptr") == 0) v = *(DWORD_PTR*)p;
        else if (strcmp(kind, "f32") == 0) { uint32_t u; float f = *(float*)p; memcpy(&u, &f, 4); v = u; isFloat = 1; }
        else if (strcmp(kind, "f64") == 0) { uint64_t u; double d = *(double*)p; memcpy(&u, &d, 8); v = u; isFloat = 1; }
        else if (strcmp(kind, "str") == 0) {
            void* s = *(void**)p;
            char buf[1024];
            read_delphi_string(s, buf, sizeof(buf));
            lua_pushstring(L, buf);
            return 1;
        } else {
            return luaL_error(L, "pivot.peek: unknown kind '%s'", kind);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    if (isFloat) {
        if (strcmp(kind, "f32") == 0) { float f; uint32_t u = (uint32_t)v; memcpy(&f, &u, 4); lua_pushnumber(L, f); }
        else { double d; memcpy(&d, &v, 8); lua_pushnumber(L, d); }
    } else {
        lua_pushinteger(L, (lua_Integer)(int64_t)v);
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * Lua API
 * ------------------------------------------------------------------------ */

static int api_log(lua_State* L)
{
    char line[1024];
    size_t pos = 0;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        size_t len; const char* s = lua_tolstring(L, i, &len);
        if (g_log) { fwrite(s, 1, len, g_log); if (i < n) fputc('\t', g_log); fflush(g_log); }
        for (size_t k = 0; k < len && pos < sizeof(line) - 2; k++) line[pos++] = s[k];
        if (i < n) line[pos++] = '\t';
    }
    if (g_log) fputc('\n', g_log);
    line[pos++] = '\n';
    line[pos] = 0;
    if (pos > 1) console_write(line);
    return 0;
}

static int api_get_main_form(lua_State* L)
{
    if (!g_mainForm) g_mainForm = scan_for_vmt_instance();
    lua_pushlightuserdata(L, g_mainForm);
    return 1;
}

static int api_call(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* method = luaL_checkstring(L, 2);
    int n = lua_gettop(L) - 2;
    if (n < 0) n = 0;
    if (n > 32) n = 32;
    if (!self) return luaL_error(L, "pivot.call: nil object");

    DWORD_PTR fn = find_method_generic(self, method);
    if (!fn) return luaL_error(L, "pivot.call: method '%s' not found on %p", method, self);

    uint32_t a1 = 0, a2 = 0;
    uint32_t more[30];
    int nMore = 0;
    if (n >= 1) a1 = (uint32_t)luaL_checkinteger(L, 3);
    if (n >= 2) a2 = (uint32_t)luaL_checkinteger(L, 4);
    for (int i = 3; i <= n; i++) {
        more[nMore++] = (uint32_t)luaL_checkinteger(L, i + 2);
    }

    void* r = NULL;
    unsigned code = 0;
    DWORD_PTR crashAddr = 0;
    __try {
        r = delphi_call_n(self, (void*)fn, (void*)(DWORD_PTR)a1, (void*)(DWORD_PTR)a2,
                          more, nMore);
    }
    __except((crashAddr = (DWORD_PTR)GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
              code = (unsigned)GetExceptionCode(),
              EXCEPTION_EXECUTE_HANDLER)) {
        char msg[160];
        if (g_log) {
            fprintf(g_log, "pivotkit: exception (code 0x%08X at 0x%p) from %s\n",
                    code, (void*)crashAddr, method);
            fflush(g_log);
        }
        _snprintf(msg, sizeof(msg), "pivot.call: Delphi exception 0x%08X at 0x%p from '%s'",
                  code, (void*)crashAddr, method);
        return luaL_error(L, "%s", msg);
    }
    lua_pushinteger(L, (lua_Integer)(DWORD_PTR)r);
    return 1;
}

static int api_get_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    uint32_t v = 0;
    __try { v = *(uint32_t*)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

/* --------------------------------------------------------------------------
 * Object & pointer bridging
 * ------------------------------------------------------------------------ */

static int api_ptr(lua_State* L)
{
    DWORD_PTR v = (DWORD_PTR)luaL_checkinteger(L, 1);
    lua_pushlightuserdata(L, (void*)v);
    return 1;
}

static int api_address(lua_State* L)
{
    if (lua_isuserdata(L, 1)) { lua_pushinteger(L, (lua_Integer)(DWORD_PTR)lua_touserdata(L, 1)); return 1; }
    if (lua_isnumber(L, 1))   { lua_pushinteger(L, lua_tointeger(L, 1)); return 1; }
    lua_pushnil(L);
    return 1;
}

static int api_is_object(lua_State* L)
{
    void* v = NULL;
    if (lua_isuserdata(L, 1)) v = lua_touserdata(L, 1);
    else if (lua_isnumber(L, 1)) v = (void*)(DWORD_PTR)lua_tointeger(L, 1);
    lua_pushboolean(L, is_object_ptr(v) ? 1 : 0);
    return 1;
}

static int api_is_string(lua_State* L)
{
    void* v = NULL;
    if (lua_isuserdata(L, 1)) v = lua_touserdata(L, 1);
    else if (lua_isnumber(L, 1)) v = (void*)(DWORD_PTR)lua_tointeger(L, 1);
    lua_pushboolean(L, is_delphi_string(v) ? 1 : 0);
    return 1;
}

static int api_get_ptr_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_ptr_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    uint32_t v = 0;
    __try { v = *(uint32_t*)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushlightuserdata(L, (void*)(DWORD_PTR)v);
    return 1;
}

/* --------------------------------------------------------------------------
 * Runtime introspection: list published methods / fields by name
 * ------------------------------------------------------------------------ */

static int api_enum_methods(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    if (!self) return luaL_error(L, "pivot.enum_methods: nil object");
    DWORD_PTR vb = obj_vmt_base(self);
    DWORD_PTR mt = vb ? ru32(vb - OFF_VMT_METHODTABLE) : 0;
    lua_newtable(L);
    if (mt) {
        uint16_t count = 0;
        __try { count = ru16(mt); } __except(EXCEPTION_EXECUTE_HANDLER) { return 1; }
        if (count > 4096) count = 4096;
        DWORD_PTR p = mt + 2;
        __try {
            for (int i = 0; i < count; i++) {
                uint8_t nlen = *(uint8_t*)(p + 6);
                if (nlen > 250) nlen = 250;
                lua_pushlstring(L, (const char*)(p + 7), nlen);
                lua_rawseti(L, -2, i + 1);
                p += 6 + 1 + nlen;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return 1; }
    }
    return 1;
}

static int api_enum_fields(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    if (!self) return luaL_error(L, "pivot.enum_fields: nil object");
    DWORD_PTR vb = obj_vmt_base(self);
    lua_newtable(L);
    if (!vb) return 1;
    DWORD_PTR ft = ru32(vb - OFF_VMT_FIELDTABLE);
    int idx = 1;
    for (int depth = 0; ft && valid_field_table(ft) && depth < 16; depth++) {
        uint16_t count = ru16(ft);
        DWORD_PTR p = ft + 2 + 4;             /* skip count + parent table ptr */
        __try {
            for (int i = 0; i < count; i++) {
                uint8_t nlen = *(uint8_t*)(p + 6);
                if (nlen > 250) nlen = 250;
                lua_pushlstring(L, (const char*)(p + 7), nlen);
                lua_rawseti(L, -2, idx++);
                p += 6 + 1 + nlen;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return 1; }
        ft = ru32(ft + 2);                     /* walk to parent table */
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * Typed field accessors (string / float / double / bool)
 * ------------------------------------------------------------------------ */

static int api_get_string_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_string_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    void* p = NULL;
    __try { p = *(void**)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    char buf[1024];
    read_delphi_string(p, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

static int api_set_string_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const char* str = luaL_checkstring(L, 3);
    if (!self) return luaL_error(L, "pivot.set_string_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) return luaL_error(L, "pivot.set_string_field: field '%s' not found", name);
    void* s = make_delphi_string(str);
    if (!s) return luaL_error(L, "pivot.set_string_field: alloc failed");
    __try {
        *(void**)((char*)self + off) = s;      /* old value deliberately leaked */
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        free_delphi_string(s);
        return luaL_error(L, "pivot.set_string_field: AV writing '%s'", name);
    }
    return 0;
}

static int api_get_single_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_single_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    uint32_t v = 0;
    __try { v = *(uint32_t*)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    float f = 0; memcpy(&f, &v, 4);
    lua_pushnumber(L, f);
    return 1;
}

static int api_set_single_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float f = (float)luaL_checknumber(L, 3);
    if (!self) return luaL_error(L, "pivot.set_single_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) return luaL_error(L, "pivot.set_single_field: field '%s' not found", name);
    uint32_t v = 0; memcpy(&v, &f, 4);
    __try { *(uint32_t*)((char*)self + off) = v; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return luaL_error(L, "pivot.set_single_field: AV writing '%s'", name); }
    return 0;
}

static int api_get_double_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_double_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    uint64_t v = 0;
    __try { v = *(uint64_t*)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    double d = 0; memcpy(&d, &v, 8);
    lua_pushnumber(L, d);
    return 1;
}

static int api_set_double_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    double d = luaL_checknumber(L, 3);
    if (!self) return luaL_error(L, "pivot.set_double_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) return luaL_error(L, "pivot.set_double_field: field '%s' not found", name);
    uint64_t v = 0; memcpy(&v, &d, 8);
    __try { *(uint64_t*)((char*)self + off) = v; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return luaL_error(L, "pivot.set_double_field: AV writing '%s'", name); }
    return 0;
}

static int api_get_bool_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!self) return luaL_error(L, "pivot.get_bool_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    uint8_t v = 0;
    __try { v = *(uint8_t*)((char*)self + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, v != 0);
    return 1;
}

static int api_set_bool_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    int b = lua_toboolean(L, 3);
    if (!self) return luaL_error(L, "pivot.set_bool_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) return luaL_error(L, "pivot.set_bool_field: field '%s' not found", name);
    __try { *(uint8_t*)((char*)self + off) = b ? 1 : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return luaL_error(L, "pivot.set_bool_field: AV writing '%s'", name); }
    return 0;
}

/* --------------------------------------------------------------------------
 * String-aware method calls
 *
 * `call_string` passes a temporary Delphi string as arg1 (EDX) and frees it
 * after the (assumed synchronous) call. `call_string_ret` calls a method whose
 * *result* is a Delphi string pointer in EAX and converts it to a Lua string.
 * ------------------------------------------------------------------------ */

static int api_call_string(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* method = luaL_checkstring(L, 2);
    const char* str = luaL_checkstring(L, 3);
    int n = lua_gettop(L) - 3;
    if (n < 0) n = 0;
    if (n > 30) n = 30;
    if (!self) return luaL_error(L, "pivot.call_string: nil object");

    DWORD_PTR fn = find_method_generic(self, method);
    if (!fn) return luaL_error(L, "pivot.call_string: method '%s' not found on %p", method, self);

    void* s = make_delphi_string(str);
    if (!s) return luaL_error(L, "pivot.call_string: alloc failed");

    uint32_t a2 = 0, more[30];
    int nMore = 0;
    if (n >= 1) a2 = (uint32_t)luaL_checkinteger(L, 4);
    for (int i = 2; i <= n; i++)
        more[nMore++] = (uint32_t)luaL_checkinteger(L, i + 3);

    void* r = NULL;
    __try {
        r = delphi_call_n(self, (void*)fn, s, (void*)(DWORD_PTR)a2, more, nMore);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        free_delphi_string(s);
        char msg[160];
        _snprintf(msg, sizeof(msg), "pivot.call_string: Delphi exception from '%s'", method);
        return luaL_error(L, "%s", msg);
    }
    free_delphi_string(s);
    lua_pushinteger(L, (lua_Integer)(DWORD_PTR)r);
    return 1;
}

static int api_call_string_ret(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* method = luaL_checkstring(L, 2);
    int n = lua_gettop(L) - 2;
    if (n < 0) n = 0;
    if (n > 32) n = 32;
    if (!self) return luaL_error(L, "pivot.call_string_ret: nil object");

    DWORD_PTR fn = find_method_generic(self, method);
    if (!fn) return luaL_error(L, "pivot.call_string_ret: method '%s' not found on %p", method, self);

    uint32_t a1 = 0, a2 = 0, more[30];
    int nMore = 0;
    if (n >= 1) a1 = (uint32_t)luaL_checkinteger(L, 3);
    if (n >= 2) a2 = (uint32_t)luaL_checkinteger(L, 4);
    for (int i = 3; i <= n; i++)
        more[nMore++] = (uint32_t)luaL_checkinteger(L, i + 2);

    void* r = NULL;
    __try {
        r = delphi_call_n(self, (void*)fn, (void*)(DWORD_PTR)a1, (void*)(DWORD_PTR)a2, more, nMore);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char msg[160];
        _snprintf(msg, sizeof(msg), "pivot.call_string_ret: Delphi exception from '%s'", method);
        return luaL_error(L, "%s", msg);
    }
    char buf[1024];
    read_delphi_string(r, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

static int api_set_field(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    lua_Integer v = luaL_checkinteger(L, 3);
    if (!self) return luaL_error(L, "pivot.set_field: nil object");
    long off = find_field_generic(self, name);
    if (off < 0) return luaL_error(L, "pivot.set_field: field '%s' not found", name);
    __try { *(uint32_t*)((char*)self + off) = (uint32_t)v; }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return luaL_error(L, "pivot.set_field: AV writing '%s'", name);
    }
    return 0;
}

static int api_on_update(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_setglobal(L, "__pivot_update__");
    return 0;
}

static int api_frame_number(lua_State* L)
{
    lua_pushinteger(L, (lua_Integer)g_frame);
    return 1;
}

static int api_read_u32(lua_State* L)
{
    DWORD_PTR addr = (DWORD_PTR)luaL_checkinteger(L, 1);
    uint32_t v = 0;
    __try { v = *(uint32_t*)addr; } __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int api_write_u32(lua_State* L)
{
    DWORD_PTR addr = (DWORD_PTR)luaL_checkinteger(L, 1);
    uint32_t v = (uint32_t)luaL_checkinteger(L, 2);
    DWORD old;
    __try {
        if (VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *(uint32_t*)addr = v;
            VirtualProtect((void*)addr, 4, old, &old);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return luaL_error(L, "pivot.write_u32: AV at %p", (void*)addr); }
    return 0;
}

static int api_read_ptr(lua_State* L)
{
    DWORD_PTR addr = (DWORD_PTR)luaL_checkinteger(L, 1);
    DWORD_PTR v = 0;
    __try { v = *(DWORD_PTR*)addr; } __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int api_read_string(lua_State* L)
{
    DWORD_PTR addr = (DWORD_PTR)luaL_checkinteger(L, 1);
    char buf[512];
    __try {
        uint8_t l = *(uint8_t*)addr;          /* Delphi shortstring: len byte, then bytes */
        if (l > 511) l = 511;
        memcpy(buf, (void*)(addr + 1), l);
        buf[l] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_pushstring(L, buf);
    return 1;
}

static int api_classname(lua_State* L)
{
    void* obj = lua_touserdata(L, 1);
    char buf[64];
    obj_classname(obj, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

static int api_find_instance(lua_State* L)
{
    DWORD_PTR vmt = 0;
    if (lua_isuserdata(L, 1)) {
        void* sample = lua_touserdata(L, 1);
        DWORD_PTR vb = obj_vmt_base(sample);
        vmt = vb ? vb + VMT_OFFSET : 0;
    } else {
        vmt = (DWORD_PTR)luaL_checkinteger(L, 1);
    }
    void* inst = scan_for_class_instance(vmt);
    lua_pushlightuserdata(L, inst);
    return 1;
}

static int api_find_instances(lua_State* L)
{
    DWORD_PTR vmt = 0;
    if (lua_isuserdata(L, 1)) {
        void* sample = lua_touserdata(L, 1);
        DWORD_PTR vb = obj_vmt_base(sample);
        vmt = vb ? vb + VMT_OFFSET : 0;
    } else {
        vmt = (DWORD_PTR)luaL_checkinteger(L, 1);
    }
    int max = lua_isnoneornil(L, 2) ? 16 : (int)luaL_checkinteger(L, 2);
    if (max < 1) max = 1;
    if (max > 4096) max = 4096;

    void* buf[4096];
    collect_class_instances(vmt, buf, max);
    lua_newtable(L);
    for (int i = 0; i < max; i++) {
        if (!buf[i]) break;
        lua_pushlightuserdata(L, buf[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* Enumerate every class found in the image (tkClass TypeInfo records whose VMT
 * class-name matches), returning their names as an array. Used to discover
 * classes beyond TMainForm (figures, frames, ...). */
static int api_enum_classes(lua_State* L)
{
    (void)L;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_appBase;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)g_appBase + dos->e_lfanew);
    size_t imageSize = nt->OptionalHeader.SizeOfImage;

    lua_newtable(L);
    int idx = 1;
    size_t off = 0;
    while (off + 8 < imageSize) {
        BYTE* p = (BYTE*)g_appBase + off;
        if (p[0] == 0x07) {
            size_t len = p[1];
            if (len >= 1 && len <= 250 && off + 2 + len + 4 < imageSize) {
                uint32_t classType = *(uint32_t*)(p + 2 + len);
                DWORD_PTR vb = classType - VMT_OFFSET;
                if (classType >= (DWORD_PTR)g_appBase && classType < (DWORD_PTR)g_appBase + imageSize) {
                    uint32_t cn = 0;
                    __try { cn = *(uint32_t*)(vb - OFF_VMT_CLASSNAME); }
                    __except(EXCEPTION_EXECUTE_HANDLER) { cn = 0; }
                    if (cn) {
                        uint8_t clen = 0;
                        __try { clen = *(uint8_t*)cn; }
                        __except(EXCEPTION_EXECUTE_HANDLER) { clen = 0; }
                        if (clen == len && memcmp((void*)(cn + 1), p + 2, len) == 0) {
                            lua_pushlstring(L, (const char*)(p + 2), len);
                            lua_rawseti(L, -2, idx++);
                        }
                    }
                }
                off += 2 + len + 4;
                continue;
            }
        }
        off++;
    }
    return 1;
}

/* Reload mods: all of them, or a single one by base name (`reload("foo")` loads
 * foo.lua). Hooks are NOT cleared here; pivotlib.unhook_all() handles that. */
static int api_reload_mods(lua_State* L)
{
    if (g_modDir[0] == 0) {
        GetModuleFileNameA(g_appBase, g_modDir, MAX_PATH);
        char* slash = strrchr(g_modDir, '\\');
        if (slash) *slash = '\0';
        strncat(g_modDir, "\\pivotkit\\mods", sizeof(g_modDir) - strlen(g_modDir) - 1);
    }
    if (lua_isnoneornil(L, 1)) {
        load_mods(g_modDir);
        return 0;
    }
    const char* name = luaL_checkstring(L, 1);
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s\\%s.lua", g_modDir, name);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return luaL_error(L, "pivot.reload: no mod '%s'", name);
    if (g_log) { fprintf(g_log, "pivotkit: reloading mod %s.lua\n", name); fflush(g_log); }
    if (luaL_dofile(g_L, path) != LUA_OK) {
        if (g_log) { fprintf(g_log, "pivotkit: mod error: %s\n", lua_tostring(g_L, -1)); fflush(g_log); }
        lua_pop(g_L, 1);
    }
    return 0;
}

static int api_field_offset(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    long off = find_field_generic(self, name);
    if (off < 0) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, off);
    return 1;
}

static int api_method_addr(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* name = luaL_checkstring(L, 2);
    DWORD_PTR fn = find_method_generic(self, name);
    if (!fn) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)fn);
    return 1;
}

static int api_key_press(lua_State* L)
{
    BYTE vk = (BYTE)luaL_checkinteger(L, 1);
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof(in));
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
    return 0;
}

static int api_key_down(lua_State* L)
{
    BYTE vk = (BYTE)luaL_checkinteger(L, 1);
    lua_pushboolean(L, (GetAsyncKeyState(vk) & 0x8000) != 0);
    return 1;
}

static int api_class(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    DWORD_PTR ti = find_class_typeinfo(name);
    if (!ti) { lua_pushnil(L); return 1; }
    uint8_t nlen = *(uint8_t*)(ti + 1);
    uint32_t classType = ru32(ti + 2 + nlen);
    lua_pushinteger(L, (lua_Integer)classType);
    return 1;
}

static int api_sleep(lua_State* L)
{
    Sleep((DWORD)luaL_checkinteger(L, 1));
    return 0;
}

static int api_hook(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* method = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!self) return luaL_error(L, "pivot.hook: nil object");
    lua_pushvalue(L, 3);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (!install_hook(self, method, ref)) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "pivot.hook: method '%s' not found", method);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int api_unhook(lua_State* L)
{
    void* self = lua_touserdata(L, 1);
    const char* method = luaL_checkstring(L, 2);
    DWORD_PTR fn = self ? find_method_generic(self, method) : 0;
    for (int i = 0; i < MAX_HOOKS; i++) {
        if (g_hooks[i].inUse && g_hooks[i].method == fn) {
            unhook_hookidx(i);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

/* Load every *.lua in the mods directory. */
static void load_mods(const char* modDir)
{
    char pattern[MAX_PATH];
    _snprintf(pattern, sizeof(pattern), "%s\\*.lua", modDir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "%s\\%s", modDir, fd.cFileName);
        if (g_log) { fprintf(g_log, "pivotkit: loading mod %s\n", fd.cFileName); fflush(g_log); }
        if (luaL_dofile(g_L, path) != LUA_OK) {
            if (g_log) { fprintf(g_log, "pivotkit: mod error: %s\n", lua_tostring(g_L, -1)); fflush(g_log); }
            lua_pop(g_L, 1);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* Worker thread: resolve RTTI, find the form, set up Lua, arm the hooks. */
static DWORD WINAPI loader_thread(LPVOID param)
{
    (void)param;

    if (!init_rtti()) {
        if (g_log) { fprintf(g_log, "pivotkit: RTTI init failed\n"); fflush(g_log); }
        return 0;
    }

    g_mainForm = scan_for_vmt_instance();
    if (g_log) fprintf(g_log, "pivotkit: mainForm=%p\n", g_mainForm);
    if (g_log) fflush(g_log);

    g_L = luaL_newstate();
    if (!g_L) return 0;
    luaL_openlibs(g_L);

    lua_newtable(g_L);
    lua_pushcfunction(g_L, api_log);           lua_setfield(g_L, -2, "log");
    lua_pushcfunction(g_L, api_get_main_form); lua_setfield(g_L, -2, "get_main_form");
    lua_pushcfunction(g_L, api_call);          lua_setfield(g_L, -2, "call");
    lua_pushcfunction(g_L, api_get_field);     lua_setfield(g_L, -2, "get_field");
    lua_pushcfunction(g_L, api_set_field);     lua_setfield(g_L, -2, "set_field");
    lua_pushcfunction(g_L, api_get_ptr_field); lua_setfield(g_L, -2, "get_ptr_field");
    lua_pushcfunction(g_L, api_get_string_field);  lua_setfield(g_L, -2, "get_string_field");
    lua_pushcfunction(g_L, api_set_string_field);  lua_setfield(g_L, -2, "set_string_field");
    lua_pushcfunction(g_L, api_get_single_field);  lua_setfield(g_L, -2, "get_single_field");
    lua_pushcfunction(g_L, api_set_single_field);  lua_setfield(g_L, -2, "set_single_field");
    lua_pushcfunction(g_L, api_get_double_field);  lua_setfield(g_L, -2, "get_double_field");
    lua_pushcfunction(g_L, api_set_double_field);  lua_setfield(g_L, -2, "set_double_field");
    lua_pushcfunction(g_L, api_get_bool_field);    lua_setfield(g_L, -2, "get_bool_field");
    lua_pushcfunction(g_L, api_set_bool_field);    lua_setfield(g_L, -2, "set_bool_field");
    lua_pushcfunction(g_L, api_enum_methods);  lua_setfield(g_L, -2, "enum_methods");
    lua_pushcfunction(g_L, api_enum_fields);   lua_setfield(g_L, -2, "enum_fields");
    lua_pushcfunction(g_L, api_call_string);   lua_setfield(g_L, -2, "call_string");
    lua_pushcfunction(g_L, api_call_string_ret); lua_setfield(g_L, -2, "call_string_ret");
    lua_pushcfunction(g_L, api_ptr);           lua_setfield(g_L, -2, "ptr");
    lua_pushcfunction(g_L, api_address);       lua_setfield(g_L, -2, "address");
    lua_pushcfunction(g_L, api_is_object);     lua_setfield(g_L, -2, "is_object");
    lua_pushcfunction(g_L, api_is_string);     lua_setfield(g_L, -2, "is_string");
    lua_pushcfunction(g_L, api_on_update);     lua_setfield(g_L, -2, "on_update");
    lua_pushcfunction(g_L, api_frame_number);  lua_setfield(g_L, -2, "frame_number");
    lua_pushcfunction(g_L, api_read_u32);      lua_setfield(g_L, -2, "read_u32");
    lua_pushcfunction(g_L, api_write_u32);     lua_setfield(g_L, -2, "write_u32");
    lua_pushcfunction(g_L, api_read_ptr);      lua_setfield(g_L, -2, "read_ptr");
    lua_pushcfunction(g_L, api_read_string);   lua_setfield(g_L, -2, "read_string");
    lua_pushcfunction(g_L, api_classname);     lua_setfield(g_L, -2, "classname");
    lua_pushcfunction(g_L, api_find_instance); lua_setfield(g_L, -2, "find_instance");
    lua_pushcfunction(g_L, api_find_instances); lua_setfield(g_L, -2, "find_instances");
    lua_pushcfunction(g_L, api_enum_classes);  lua_setfield(g_L, -2, "enum_classes");
    lua_pushcfunction(g_L, api_reload_mods);   lua_setfield(g_L, -2, "reload");
    lua_pushcfunction(g_L, api_field_offset);  lua_setfield(g_L, -2, "field_offset");
    lua_pushcfunction(g_L, api_method_addr);   lua_setfield(g_L, -2, "method_addr");
    lua_pushcfunction(g_L, api_class);         lua_setfield(g_L, -2, "class");
    lua_pushcfunction(g_L, api_key_press);     lua_setfield(g_L, -2, "key_press");
    lua_pushcfunction(g_L, api_key_down);      lua_setfield(g_L, -2, "key_down");
    lua_pushcfunction(g_L, api_sleep);         lua_setfield(g_L, -2, "sleep");
    lua_pushcfunction(g_L, api_hook);          lua_setfield(g_L, -2, "hook");
    lua_pushcfunction(g_L, api_unhook);        lua_setfield(g_L, -2, "unhook");
    lua_pushcfunction(g_L, api_add_menu_button); lua_setfield(g_L, -2, "add_menu_button");
    lua_pushcfunction(g_L, api_remove_menu_button); lua_setfield(g_L, -2, "remove_menu_button");
    lua_pushcfunction(g_L, api_sprite);         lua_setfield(g_L, -2, "sprite");
    lua_pushcfunction(g_L, sprite_destroy_handle); lua_setfield(g_L, -2, "sprite_destroy");
    lua_pushcfunction(g_L, api_sprite_move);    lua_setfield(g_L, -2, "sprite_move");
    lua_pushcfunction(g_L, api_sprite_vel);     lua_setfield(g_L, -2, "sprite_velocity");
    lua_pushcfunction(g_L, api_sprite_bounce);  lua_setfield(g_L, -2, "sprite_bounce");
    lua_pushcfunction(g_L, api_sprite_show);    lua_setfield(g_L, -2, "sprite_show");
    lua_pushcfunction(g_L, api_sprite_hide);    lua_setfield(g_L, -2, "sprite_hide");
    lua_pushcfunction(g_L, api_sprite_pos);     lua_setfield(g_L, -2, "sprite_pos");
    lua_pushcfunction(g_L, api_window_rect);    lua_setfield(g_L, -2, "window_rect");
    lua_pushcfunction(g_L, api_cursor_pos);     lua_setfield(g_L, -2, "cursor_pos");
    lua_pushcfunction(g_L, api_overlay_create); lua_setfield(g_L, -2, "overlay_create");
    lua_pushcfunction(g_L, api_overlay_destroy); lua_setfield(g_L, -2, "overlay_destroy");
    lua_pushcfunction(g_L, api_overlay_begin);  lua_setfield(g_L, -2, "overlay_begin");
    lua_pushcfunction(g_L, api_overlay_text);   lua_setfield(g_L, -2, "overlay_text");
    lua_pushcfunction(g_L, api_overlay_line);   lua_setfield(g_L, -2, "overlay_line");
    lua_pushcfunction(g_L, api_overlay_rect);   lua_setfield(g_L, -2, "overlay_rect");
    lua_pushcfunction(g_L, api_overlay_circle); lua_setfield(g_L, -2, "overlay_circle");
    lua_pushcfunction(g_L, api_overlay_commit); lua_setfield(g_L, -2, "overlay_commit");
    lua_pushcfunction(g_L, api_console);       lua_setfield(g_L, -2, "console");
    lua_pushcfunction(g_L, api_peek);          lua_setfield(g_L, -2, "peek");
    lua_setglobal(g_L, "pivot");
    lua_pushnil(g_L);
    lua_setglobal(g_L, "__pivot_update__");

    GetModuleFileNameA(g_appBase, g_modDir, MAX_PATH);
    {
        char* slash = strrchr(g_modDir, '\\');
        if (slash) *slash = '\0';
    }
    strncat(g_modDir, "\\pivotkit\\mods", sizeof(g_modDir) - strlen(g_modDir) - 1);

    if (hook_peek_message()) {
        g_loadFrameDelay = 120;              /* ~2 seconds at 60fps */
        InterlockedExchange(&g_pendingLoad, 1);
    } else {
        load_mods(g_modDir);
    }

    /* Console on the -console loader flag (PIVOTKIT_CONSOLE=1). */
    {
        char env[16];
        if (GetEnvironmentVariableA("PIVOTKIT_CONSOLE", env, sizeof(env)) > 0)
            console_create();
    }

    /* TCP bridge — enabled by default on 127.0.0.1. */
    if (bridge_start()) {
        if (g_log) { fprintf(g_log, "pivotkit: bridge listening on 127.0.0.1:%d\n", BRIDGE_PORT); fflush(g_log); }
        if (g_consoleOut) console_write("pivotkit bridge: 127.0.0.1:50077 (tools/pivotctl.py)\n");
    }

    if (g_log) {
        fprintf(g_log, "pivotkit: setup done. hook=%d\n", g_origPeekMessageW != NULL);
        fflush(g_log);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_appBase = (HMODULE)GetModuleHandleW(NULL);   /* pivot.exe base, not our DLL */
        char logPath[MAX_PATH];
        GetModuleFileNameA(hModule, logPath, MAX_PATH);
        {
            char* slash = strrchr(logPath, '\\');
            if (slash) *slash = '\0';
        }
        strncat(logPath, "\\\pivotkit.log", sizeof(logPath) - strlen(logPath) - 1);
        g_log = fopen(logPath, "a");
        if (g_log) {
            fprintf(g_log, "\n=== pivotkit loaded ===\n");
            fflush(g_log);
        }
        DisableThreadLibraryCalls(hModule);
        SetUnhandledExceptionFilter(crash_filter);
        CreateThread(NULL, 0, loader_thread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        if (g_L) lua_close(g_L);
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
