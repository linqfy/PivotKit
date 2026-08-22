/* pk_rtti.c - Delphi RTTI navigator.
 * Hardened port of the runtime-proven logic from legacy src/pivotkit.cpp,
 * reorganized as a standalone module. All memory access is SEH-guarded.
 */
#include "pk_rtti.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#define TI_KIND_CLASS 7

static uint32_t ru16(uint32_t va) { return *(uint16_t *)(uintptr_t)va; }
static uint32_t ru32(uint32_t va) { return *(uint32_t *)(uintptr_t)va; }

static int safe_read(uint32_t va, void *buf, uint32_t len)
{
    __try {
        memcpy(buf, (void *)(uintptr_t)va, len);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

/* Find tkClass TypeInfo by length-prefixed Pascal name, then validate the
 * class name at the VMT. Returns ClassType VA (0 on failure). */
uint32_t pk_class_find(const char *class_name)
{
    uint8_t nb[256];
    size_t len = strlen(class_name);
    if (len == 0 || len > 250)
        return 0;

    uint8_t *base = (uint8_t *)pk_image_base();
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    uint32_t image_size = nt->OptionalHeader.SizeOfImage;

    uint8_t probe[2] = { TI_KIND_CLASS, (uint8_t)len };
    for (uint32_t off = 0; off + 2 + len + 4 < image_size; off++) {
        if (memcmp(base + off, probe, 2) != 0)
            continue;
        if (memcmp(base + off + 2, class_name, len) != 0)
            continue;
        uint32_t ct;
        if (!safe_read((uint32_t)(uintptr_t)(base + off + 2 + len), &ct, 4))
            continue;
        if (ct < (uint32_t)(uintptr_t)base ||
            ct >= (uint32_t)(uintptr_t)base + image_size)
            continue;
        /* validate: vmtClassName at ct-56 must round-trip */
        uint32_t cn;
        if (!safe_read(ct + PK_VMT_CLASS_NAME, &cn, 4) || !cn)
            continue;
        uint8_t clen;
        if (!safe_read(cn, &clen, 1) || clen != len)
            continue;
        if (safe_read(cn + 1, nb, (uint32_t)len) &&
            memcmp(nb, class_name, len) == 0)
            return ct;
    }
    return 0;
}

/* ---- published method table -------------------------------------------- */
static uint32_t find_method_in_table(uint32_t table_va, const char *name)
{
    uint16_t count;
    if (!table_va)
        return 0;
    if (!safe_read(table_va, &count, 2) || !count || count > 4096)
        return 0;
    uint32_t p = table_va + 2;
    size_t want = strlen(name);
    for (int i = 0; i < count; i++) {
        uint8_t hdr[7];
        if (!safe_read(p, hdr, 7))
            return 0;
        uint8_t nlen = hdr[6];
        if (nlen == want) {
            char nbuf[256];
            if (safe_read(p + 7, nbuf, nlen) && memcmp(nbuf, name, nlen) == 0)
                return ru32(p + 2);
        }
        p += 7 + nlen;
    }
    return 0;
}

static long find_field_in_table(uint32_t table_va, const char *name)
{
    uint16_t count;
    if (!table_va)
        return -1;
    if (!safe_read(table_va, &count, 2) || !count || count > 4096)
        return -1;
    uint32_t p = table_va + 2 + 4;   /* skip count + parent table ptr */
    size_t want = strlen(name);
    for (int i = 0; i < count; i++) {
        uint8_t hdr[7];
        if (!safe_read(p, hdr, 7))
            return -1;
        uint32_t off;
        if (!safe_read(p, &off, 4))
            return -1;
        uint8_t nlen = hdr[6];
        if (nlen == want) {
            char nbuf[256];
            if (safe_read(p + 7, nbuf, nlen) && memcmp(nbuf, name, nlen) == 0)
                return (long)off;
        }
        p += 7 + nlen;
    }
    return -1;
}

static int valid_field_table(uint32_t ft)
{
    uint16_t c = 0;
    uint32_t base = (uint32_t)(uintptr_t)pk_image_base();
    if (!ft || ft < base || ft >= base + 0x1000000)
        return 0;
    if (!safe_read(ft, &c, 2))
        return 0;
    return c > 0 && c <= 4096;
}

uint32_t pk_class_method_addr(uint32_t classtype, const char *method)
{
    if (!classtype)
        return 0;
    return find_method_in_table(ru32(classtype + PK_VMT_METHOD_TABLE), method);
}

uint32_t pk_method_addr(PkObj obj, const char *method)
{
    uint32_t ct;
    if (!obj || !safe_read((uint32_t)(uintptr_t)obj, &ct, 4))
        return 0;
    return pk_class_method_addr(ct, method);
}

long pk_class_field_offset(uint32_t classtype, const char *field)
{
    if (!classtype)
        return -1;
    uint32_t ft = ru32(classtype + PK_VMT_FIELD_TABLE);
    for (int depth = 0; ft && valid_field_table(ft) && depth < 16; depth++) {
        long off = find_field_in_table(ft, field);
        if (off >= 0)
            return off;
        ft = ru32(ft + 2);          /* parent table */
    }
    return -1;
}

long pk_field_offset(PkObj obj, const char *field)
{
    uint32_t ct;
    if (!obj || !safe_read((uint32_t)(uintptr_t)obj, &ct, 4))
        return -1;
    return pk_class_field_offset(ct, field);
}

void pk_class_name(PkObj obj, char *buf, size_t bufsz)
{
    buf[0] = 0;
    uint32_t ct, cn;
    if (!obj || bufsz < 2)
        return;
    if (!safe_read((uint32_t)(uintptr_t)obj, &ct, 4) || !ct)
        return;
    if (!safe_read(ct + PK_VMT_CLASS_NAME, &cn, 4) || !cn)
        return;
    uint8_t clen;
    if (!safe_read(cn, &clen, 1) || clen < 1 || clen > 64 || clen >= bufsz)
        return;
    if (safe_read(cn + 1, buf, clen))
        buf[clen] = 0;
}

int pk_is_object(PkObj p)
{
    uint32_t ct, isize;
    uint32_t base = (uint32_t)(uintptr_t)pk_image_base();
    if (!p || ((uintptr_t)p & 3))
        return 0;
    if (!safe_read((uint32_t)(uintptr_t)p, &ct, 4) || !ct)
        return 0;
    if (ct < base || ct >= base + 0x1000000)
        return 0;
    if (!safe_read(ct + PK_VMT_INSTANCE_SIZE, &isize, 4))
        return 0;
    return isize >= 0x20 && isize <= 0x20000;
}

/* ---- Delphi string bridge ---------------------------------------------- */
PkStr pk_str_make(const char *utf8)
{
    if (!utf8)
        utf8 = "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0) - 1;
    if (wlen < 0)
        wlen = 0;
    uint8_t *block = HeapAlloc(GetProcessHeap(), 0, 12 + (size_t)(wlen + 1) * 2);
    if (!block)
        return NULL;
    *(uint16_t *)(block + 0) = 1200;   /* CP_UTF16 */
    *(uint16_t *)(block + 2) = 2;      /* element size */
    *(int32_t *)(block + 4) = -1;      /* constant string: never freed */
    *(int32_t *)(block + 8) = wlen;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, (WCHAR *)(block + 12), wlen + 1);
    return block + 12;
}

void pk_str_read(PkStr s, char *buf, size_t bufsz)
{
    buf[0] = 0;
    if (!s || bufsz < 2)
        return;
    __try {
        int32_t len = *(int32_t *)((uint8_t *)s - 4);
        if (len < 0)
            len = 0;
        if (len > (int32_t)(bufsz / 2))
            len = (int32_t)(bufsz / 2);
        int n = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)s, len,
                                    buf, (int)bufsz - 1, NULL, NULL);
        buf[n > 0 ? n : 0] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = 0;
    }
}

/* ---- register-convention invoker ---------------------------------------
 * EAX=self, EDX=a1, ECX=a2, stack args pushed right-to-left; the callee's
 * `ret N` cleans its own stack args, so the stub must copy the caller's
 * stack args before the call. Proven shape from legacy delphi_call_n. */
__declspec(naked) static void *pk_call_asm(void *self, uint32_t fn, void *a1,
                                           void *a2, const uint32_t *more,
                                           int n_more)
{
    __asm {
        mov  eax, [esp+4]     /* self   */
        mov  ebx, [esp+8]     /* fn     */
        mov  edx, [esp+12]    /* a1     */
        mov  ecx, [esp+16]    /* a2     */
        mov  esi, [esp+20]    /* more   */
        mov  edi, [esp+24]    /* n_more */
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

void *pk_call(PkObj self, uint32_t fn, void *a1, void *a2,
              const uint32_t *more, int n_more)
{
    if (!self || !fn)
        return NULL;
    return pk_call_asm(self, fn, a1, a2, more, n_more);
}
