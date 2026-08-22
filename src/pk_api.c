/* pk_api.c - typed accessors over the 5.2.11 RE offsets. */
#include "pk_api.h"
#include "pk_hooks.h"
#include <windows.h>
#include <string.h>

/* TMainForm */
#define MF_FRAMESEQ_OFF   0x654   /* TFrameSequence (TArray<TFigures>) */
/* TFigures */
#define FR_NUMFIG_OFF     0x004
#define FR_BG_OFF         0x006
#define FR_TWEEN_OFF      0x00C
#define FR_FIGURES_OFF    0x010   /* dynamic array of TFigure */
#define FR_CAMERA_OFF     0x038   /* TCamera inline */
/* TFigure */
#define FG_COLOR_OFF      0x024
#define FG_TRANS_OFF      0x028
#define FG_SCALE_OFF      0x02C
#define FG_VERTS_OFF      0x034   /* TArray<TFigVertex> */
/* TFigVertex (16 B) */
#define FV_X_OFF          0x00
#define FV_Y_OFF          0x04
#define FV_NUMATTACH_OFF  0x08

static uint32_t ru32(uint32_t va) { return *(uint32_t *)(uintptr_t)va; }
static uint16_t ru16(uint32_t va) { return *(uint16_t *)(uintptr_t)va; }
static int rd16f(uint32_t va, uint16_t *out);
static uint32_t frame_change_va(void);

static int rd32(uint32_t va, uint32_t *out)
{
    __try { *out = ru32(va); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int wr32(uint32_t va, uint32_t v)
{
    __try { *(uint32_t *)(uintptr_t)va = v; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- main form ---------------------------------------------------------- */
static PkObj g_main_form;

static PkObj scan_main_form(uint32_t classtype)
{
    /* VMT-validated heap scan for the TMainForm instance; identical
     * strategy to the proven legacy scan_for_vmt_instance. */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint32_t addr = (uint32_t)(uintptr_t)si.lpMinimumApplicationAddress;
    uint32_t maxa = (uint32_t)(uintptr_t)si.lpMaximumApplicationAddress;
    while (addr < maxa) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)(uintptr_t)addr, &mbi, sizeof(mbi)) == 0)
            break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect & 0xFF) != PAGE_EXECUTE &&
            (mbi.Protect & 0xFF) != PAGE_EXECUTE_READ) {
            uint8_t *p = (uint8_t *)mbi.BaseAddress;
            for (size_t i = 0; i + 4 <= mbi.RegionSize; i += 4) {
                uint32_t v = 0;
                __try { v = *(uint32_t *)(p + i); }
                __except (EXCEPTION_EXECUTE_HANDLER) { break; }
                if (v == classtype && i >= 0x40) {
                    /* MainMenu1 @ +0x2F8 must look like an object ptr */
                    uint32_t mm = 0;
                    if (rd32((uint32_t)(uintptr_t)(p + i) + 0x2F8, &mm) &&
                        mm && !(mm & 3)) {
                        uint32_t mvmt = 0;
                        if (rd32(mm, &mvmt) && mvmt >= (uint32_t)(uintptr_t)pk_image_base())
                            return (PkObj)(p + i);
                    }
                }
            }
        }
        addr += mbi.RegionSize ? (uint32_t)mbi.RegionSize : 0x1000;
    }
    return NULL;
}

PkObj pk_main_form(void)
{
    if (g_main_form)
        return g_main_form;
    uint32_t ct = pk_class_find("TMainForm");
    if (!ct)
        return NULL;
    g_main_form = scan_main_form(ct);
    return g_main_form;
}

uint32_t pk_frame_count(void)
{
    PkObj mf = pk_main_form();
    uint32_t arr = 0;
    if (!mf || !rd32((uint32_t)(uintptr_t)mf + MF_FRAMESEQ_OFF, &arr) || !arr)
        return 0;
    uint32_t n = 0;
    rd32(arr - 4, &n);      /* dynarray length at ptr-4 (elements) */
    return n;
}

/* ---- frames ------------------------------------------------------------- */
PkFrameView pk_frame(int index)
{
    PkFrameView v = { NULL };
    PkObj mf = pk_main_form();
    uint32_t arr = 0;
    if (!mf || !rd32((uint32_t)(uintptr_t)mf + MF_FRAMESEQ_OFF, &arr) || !arr)
        return v;
    uint32_t n = 0;
    rd32(arr - 4, &n);
    if (index < 0 || (uint32_t)index >= n)
        return v;
    uint32_t figs = 0;
    if (rd32(arr + 4 * index, &figs) && figs)
        v.self = (PkObj)(uintptr_t)figs;
    return v;
}

int pk_frame_valid(PkFrameView f) { return f.self != NULL; }

int pk_frame_figure_count(PkFrameView f)
{
    uint16_t n = 0;
    if (!f.self || !rd16f((uint32_t)(uintptr_t)f.self + FR_NUMFIG_OFF, &n))
        return 0;
    return n;
}

PkObj pk_frame_figure(PkFrameView f, int i)
{
    uint32_t arr = 0;
    if (!f.self || !rd32((uint32_t)(uintptr_t)f.self + FR_FIGURES_OFF, &arr) || !arr)
        return NULL;
    uint32_t n = 0;
    rd32(arr - 4, &n);
    if (i < 0 || (uint32_t)i >= n)
        return NULL;
    uint32_t o = 0;
    if (rd32(arr + 4 * i, &o) && o)
        return (PkObj)(uintptr_t)o;
    return NULL;
}

uint16_t pk_frame_tween(PkFrameView f)
{
    uint16_t v = 0;
    if (f.self) rd16f((uint32_t)(uintptr_t)f.self + FR_TWEEN_OFF, &v);
    return v;
}

void pk_frame_set_tween(PkFrameView f, uint16_t v)
{
    if (f.self)
        __try { *(uint16_t *)(uintptr_t)((uint32_t)(uintptr_t)f.self + FR_TWEEN_OFF) = v; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
}

uint16_t pk_frame_background(PkFrameView f)
{
    uint16_t v = 0;
    if (f.self) rd16f((uint32_t)(uintptr_t)f.self + FR_BG_OFF, &v);
    return v;
}

void pk_frame_set_background(PkFrameView f, uint16_t v)
{
    if (f.self)
        __try { *(uint16_t *)(uintptr_t)((uint32_t)(uintptr_t)f.self + FR_BG_OFF) = v; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int rd16f(uint32_t va, uint16_t *out)
{
    __try { *out = ru16(va); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- figures ------------------------------------------------------------ */
static float rdf(uint32_t va)
{
    float f = 0;
    __try { f = *(float *)(uintptr_t)va; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return f;
}

float pk_figure_scale(PkObj fig) { return fig ? rdf((uint32_t)(uintptr_t)fig + FG_SCALE_OFF) : 0; }

void pk_figure_set_scale(PkObj fig, float v)
{
    if (!fig) return;
    __try { *(float *)(uintptr_t)((uint32_t)(uintptr_t)fig + FG_SCALE_OFF) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

uint32_t pk_figure_color(PkObj fig)
{
    uint32_t v = 0;
    if (fig) rd32((uint32_t)(uintptr_t)fig + FG_COLOR_OFF, &v);
    return v;
}

void pk_figure_set_color(PkObj fig, uint32_t argb)
{
    if (fig) wr32((uint32_t)(uintptr_t)fig + FG_COLOR_OFF, argb);
}

uint8_t pk_figure_transparency(PkObj fig)
{
    uint8_t v = 0;
    if (fig) __try { v = *(uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)fig + FG_TRANS_OFF); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}

void pk_figure_set_transparency(PkObj fig, uint8_t v)
{
    if (!fig) return;
    __try { *(uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)fig + FG_TRANS_OFF) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

uint16_t pk_figure_type_idx(PkObj fig)
{
    uint16_t v = 0;
    if (fig) rd16f((uint32_t)(uintptr_t)fig + 0x30, &v);
    return v;
}

uint8_t pk_figure_flipped(PkObj fig)
{
    uint8_t v = 0;
    if (fig) __try { v = *(uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)fig + 0x3C); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}

int pk_figure_vertex_count(PkObj fig)
{
    uint32_t arr = 0;
    if (!fig || !rd32((uint32_t)(uintptr_t)fig + FG_VERTS_OFF, &arr) || !arr)
        return 0;
    uint32_t n = 0;
    rd32(arr - 4, &n);
    return (int)n;
}

int pk_figure_vertex(PkObj fig, int i, PkFigVertexV *out)
{
    if (!fig || !out)
        return 0;
    uint32_t arr = 0;
    if (!rd32((uint32_t)(uintptr_t)fig + FG_VERTS_OFF, &arr) || !arr)
        return 0;
    uint32_t n = 0;
    rd32(arr - 4, &n);
    if (i < 0 || (uint32_t)i >= n)
        return 0;
    uint32_t v = arr + 16 * (uint32_t)i;
    __try {
        out->x = *(float *)(uintptr_t)(v + FV_X_OFF);
        out->y = *(float *)(uintptr_t)(v + FV_Y_OFF);
        out->num_attach = *(int32_t *)(uintptr_t)(v + FV_NUMATTACH_OFF);
        out->attach = *(void **)(uintptr_t)(v + 0x0C);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

/* ---- camera ------------------------------------------------------------- */
int pk_frame_camera(PkFrameView f, PkCameraV *out)
{
    if (!f.self || !out)
        return 0;
    uint32_t c = (uint32_t)(uintptr_t)f.self + FR_CAMERA_OFF;
    __try {
        out->x = *(float *)(uintptr_t)(c + 0);
        out->y = *(float *)(uintptr_t)(c + 4);
        out->angle = *(double *)(uintptr_t)(c + 8);
        out->scale = *(float *)(uintptr_t)(c + 16);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int pk_frame_set_camera(PkFrameView f, const PkCameraV *cam)
{
    if (!f.self || !cam)
        return 0;
    uint32_t c = (uint32_t)(uintptr_t)f.self + FR_CAMERA_OFF;
    __try {
        *(float *)(uintptr_t)(c + 0) = cam->x;
        *(float *)(uintptr_t)(c + 4) = cam->y;
        *(double *)(uintptr_t)(c + 8) = cam->angle;
        *(float *)(uintptr_t)(c + 16) = cam->scale;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

/* ---- events ------------------------------------------------------------- */
static PkFrameChangeFn g_frame_change_cb;
static void *g_frame_change_user;

static int frame_change_before(PkPatchInfo *p, void *self, void *a1, void *a2)
{
    (void)p; (void)self; (void)a2;
    if (g_frame_change_cb)
        g_frame_change_cb(g_frame_change_user, (int)(uintptr_t)a1);
    return 0;   /* let the original run */
}

void pk_on_frame_change(PkFrameChangeFn fn, void *user)
{
    g_frame_change_cb = fn;
    g_frame_change_user = user;
    if (!fn || pk_hook_active(frame_change_va()))
        return;
    pk_hook_function(frame_change_va(), "MainUnit.TMainForm.SelectFrame",
                     frame_change_before, NULL, NULL, NULL, NULL);
}

static uint32_t frame_change_va(void)
{
    /* resolved once via RTTI (SelectFrame is a published TMainForm method) */
    static uint32_t va;
    if (!va) {
        PkObj mf = pk_main_form();
        if (mf)
            va = pk_method_addr(mf, "SelectFrame");
    }
    return va;
}
