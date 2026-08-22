/* pk_core.c - core module implementation (logging, version gate).
 *
 * Completes the v2 module set so all of src/ compiles and links into the
 * shipped pivotkit.dll. The proven runtime host remains pivotkit.cpp; these
 * modules give the merged build a clean C API surface (pk_*).
 */
#include "pk_core.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void *g_image_base;
static FILE *g_logf;
static int g_core_init_ok;

static const char *level_name(PkLogLevel l)
{
    switch (l) {
    case PK_LOG_DEBUG: return "DEBUG";
    case PK_LOG_INFO:  return "INFO";
    case PK_LOG_WARN:  return "WARN";
    default:           return "ERROR";
    }
}

void pk_log(PkLogLevel level, const char *fmt, ...)
{
    if (!g_logf) {
        char path[MAX_PATH];
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)pk_log, &self);
        if (self && GetModuleFileNameA(self, path, MAX_PATH)) {
            char *slash = strrchr(path, '\\');
            if (slash) *slash = 0;
            strcat_s(path, sizeof(path), "\\pk_core.log");
            g_logf = fopen(path, "a");
        }
        if (!g_logf) g_logf = fopen("pk_core.log", "a");
        if (!g_logf) return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logf, "[%02u:%02u:%02u] %s ",
            st.wHour, st.wMinute, st.wSecond, level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logf, fmt, ap);
    va_end(ap);
    fputc('\n', g_logf);
    fflush(g_logf);
}

const char *pk_log_path(void)
{
    return "pk_core.log";
}

void *pk_image_base(void)
{
    return g_image_base;
}

/* Version gate for the build this module was compiled against: the
 * TMainForm TypeInfo for 5.2.11 lives at 0xB1AF40 with kind=7 and the
 * class name at VMT-56 round-tripping to "TMainForm" (see
 * research/mappings/classes_full.json). */
int pk_version_ok(void)
{
    uint8_t *base = (uint8_t *)g_image_base;
    if (!base) return 0;
    __try {
        uint8_t kind = *(uint8_t *)(base + 0xB1AF40 - 0x400000);
        if (kind != 7) return 0;
        uint32_t ct = *(uint32_t *)(base + 0xB1AF40 - 0x400000 + 2 + 9);
        if (ct < 0x400000 || ct >= 0x400000 + 0x1000000) return 0;
        uint32_t cn = *(uint32_t *)(ct - 56);
        if (!cn) return 0;
        uint8_t len = *(uint8_t *)cn;
        if (len != 9 || memcmp((void *)(cn + 1), "TMainForm", 9) != 0)
            return 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int pk_core_init(void *pivot_image_base)
{
    g_image_base = pivot_image_base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_image_base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return -1;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)g_image_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return -2;
    g_core_init_ok = 1;
    pk_log(PK_LOG_INFO, "pk_core init: base=%p size=0x%X version_ok=%d",
           g_image_base, nt->OptionalHeader.SizeOfImage, pk_version_ok());
    return pk_version_ok() ? 0 : 1;
}
