/* test_pkbindings.c - regression: generated bindings match the RE database.
 *
 * Verifies, against the known-good 5.2.11 offsets (research/classes/):
 *   - field offsets inside the generated structs (offsetof)
 *   - the class registry rows (VMT VAs, instance sizes)
 *   - dynarray helper semantics
 * Build (x86, matches the DLL): cl /nologo /Iinclude\pivot tests\test_pkbindings.c /Fobin\test_pkbindings.obj && link ...
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "pivot_5_2_11.h"

static int fails;

#define CHECK(expr) do { \
    if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); fails++; } \
} while (0)

int main(void)
{
    /* TFigure (research/classes/TFigure.md) */
    CHECK(offsetof(TFigureS, FExtent)    == 0x04);
    CHECK(offsetof(TFigureS, FExtentF)   == 0x14);
    CHECK(offsetof(TFigureS, FColor)     == 0x24);
    CHECK(offsetof(TFigureS, FTrans)     == 0x28);
    CHECK(offsetof(TFigureS, FScale)     == 0x2C);
    CHECK(offsetof(TFigureS, FFigureType)== 0x30);
    CHECK(offsetof(TFigureS, FigVertices)== 0x34);
    CHECK(offsetof(TFigureS, FigSegments)== 0x38);
    CHECK(offsetof(TFigureS, Flipped)    == 0x3C);
    CHECK(offsetof(TFigureS, Attachment) == 0x42);
    CHECK(offsetof(TFigureS, DrawOrderIdx)== 0x4C);
    CHECK(offsetof(TFigureS, ID)         == 0x58);

    /* TFigures (research/classes/TFigures.md) */
    CHECK(offsetof(TFiguresS, FNumFigures)  == 0x04);
    CHECK(offsetof(TFiguresS, FBackground)  == 0x06);
    CHECK(offsetof(TFiguresS, FFrameTween)  == 0x0C);
    CHECK(offsetof(TFiguresS, Figures)      == 0x10);
    CHECK(offsetof(TFiguresS, DrawOrder)    == 0x14);
    CHECK(offsetof(TFiguresS, SelectedRect) == 0x24);
    CHECK(offsetof(TFiguresS, Camera)       == 0x38);

    /* records (research/classes/model-records.md) */
    CHECK(offsetof(TCameraRecS, Pos)   == 0x00);
    CHECK(offsetof(TCameraRecS, Angle) == 0x08);
    CHECK(offsetof(TCameraRecS, Scale) == 0x10);
    CHECK(sizeof(TCameraRecS)          == 24);
    CHECK(offsetof(TAttachmentRecS, Attached) == 0x00);
    CHECK(offsetof(TAttachmentRecS, Figure)   == 0x02);
    CHECK(offsetof(TAttachmentRecS, Vertex)   == 0x04);
    CHECK(sizeof(TAttachmentRecS)            == 6);
    CHECK(offsetof(TFigVertexRecS, Point)     == 0x00);
    CHECK(offsetof(TFigVertexRecS, NumAttach) == 0x08);
    CHECK(sizeof(TFigVertexRecS)              == 16);
    CHECK(offsetof(TSegmentRecS, Pivot)       == 0x00);
    CHECK(offsetof(TSegmentRecS, EndPoint)    == 0x02);
    CHECK(offsetof(TSegmentRecS, Len)         == 0x04);
    CHECK(offsetof(TSegmentRecS, Angle)       == 0x08);
    CHECK(offsetof(TSegmentRecS, Bend)        == 0x10);
    CHECK(offsetof(TSegmentRecS, Mirror)      == 0x2F);

    /* class registry vs STATUS.md quick-reference table */
    const PkClassInfo *mf = NULL, *fig = NULL, *fr = NULL, *ft = NULL;
    for (unsigned i = 0; i < PkClassCount; i++) {
        if (!strcmp(PkClasses[i].name, "TMainForm"))  mf = &PkClasses[i];
        if (!strcmp(PkClasses[i].name, "TFigure"))    fig = &PkClasses[i];
        if (!strcmp(PkClasses[i].name, "TFigures"))   fr = &PkClasses[i];
        if (!strcmp(PkClasses[i].name, "TFigureType"))ft = &PkClasses[i];
    }
    CHECK(mf && mf->vmt_va == 0xB14D68u && mf->instance_size == 0x724u);
    CHECK(fig && fig->vmt_va == 0x93AA04u && fig->instance_size == 0x64u);
    CHECK(fr  && fr->vmt_va == 0x946D9Cu  && fr->instance_size == 0x54u);
    CHECK(ft  && ft->vmt_va == 0xAF97A4u  && ft->instance_size == 0x3Cu);

    /* published-method macros (TMainForm surface) */
    CHECK(PK_TMainForm_SelectFrame_VA != 0);
    CHECK(PK_TMainForm_SetNumFrames_VA != 0);
    CHECK(PK_TMainForm_SetFrameTween_VA != 0);
    CHECK(PK_TFigureBuilderForm_FormCreate_VA != 0);

    /* dynarray helper semantics (mock buffer) */
    static unsigned char buf[16];
    void **data = (void **)(buf + 8);
    *(int32_t *)(buf + 4) = 7;   /* dynarray length lives at data-4 */
    *data = (void *)0x1234;
    PkDynArray a = { (void *)(buf + 8) };
    CHECK(pk_dynarray_len(&a) == 7);

    if (fails) { printf("%d BINDING CHECK(S) FAILED\n", fails); return 1; }
    printf("ALL BINDING CHECKS PASSED (%u classes in registry)\n", PkClassCount);
    return 0;
}
