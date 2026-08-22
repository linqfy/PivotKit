/* pk_api.h - typed mod-facing surface. Offsets are RE-derived for 5.2.11
 * (include/pivot/pivot_5_2_11.h); all accessors are SEH-guarded. */
#ifndef PK_API_H
#define PK_API_H

#include "pk_core.h"
#include "pivot_5_2_11.h"   /* generated bindings */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Application / forms ----------------------------------------------- */
PkObj pk_main_form(void);          /* TMainForm instance (VMT-validated scan) */
uint32_t pk_frame_count(void);     /* length of TFrameSequence */

/* ---- Frame model (TFigures) -------------------------------------------- */
typedef struct PkFrameView {
    PkObj self;                    /* TFigures instance */
} PkFrameView;

PkFrameView pk_frame(int index);               /* 0-based; .self = NULL if OOB */
int   pk_frame_valid(PkFrameView f);
int   pk_frame_figure_count(PkFrameView f);    /* FNumFigures @ +0x04 */
PkObj pk_frame_figure(PkFrameView f, int i);   /* TFigure instance or NULL */
uint16_t pk_frame_tween(PkFrameView f);        /* FFrameTween @ +0x0C */
void  pk_frame_set_tween(PkFrameView f, uint16_t v);
uint16_t pk_frame_background(PkFrameView f);   /* FBackground @ +0x06 */
void  pk_frame_set_background(PkFrameView f, uint16_t v);

/* ---- Figures (TFigure) -------------------------------------------------- */
float    pk_figure_scale(PkObj fig);           /* FScale @ +0x2C */
void     pk_figure_set_scale(PkObj fig, float v);
uint32_t pk_figure_color(PkObj fig);           /* FColor @ +0x24 */
void     pk_figure_set_color(PkObj fig, uint32_t argb);
uint8_t  pk_figure_transparency(PkObj fig);    /* FTrans @ +0x28 */
void     pk_figure_set_transparency(PkObj fig, uint8_t v);
uint16_t pk_figure_type_idx(PkObj fig);        /* FFigureType @ +0x30 */
uint8_t  pk_figure_flipped(PkObj fig);         /* Flipped @ +0x3C */

/* Vertex array of a figure (TFigVertex[], 16 B each). */
typedef struct { float x, y; int32_t num_attach; void *attach; } PkFigVertexV;
int pk_figure_vertex_count(PkObj fig);
int pk_figure_vertex(PkObj fig, int i, PkFigVertexV *out);  /* 0 on success */

/* ---- Camera (TFigures+0x38 -> TCamera) ---------------------------------- */
typedef struct { float x, y; double angle; float scale; } PkCameraV;
int pk_frame_camera(PkFrameView f, PkCameraV *out);
int pk_frame_set_camera(PkFrameView f, const PkCameraV *c);

/* ---- Events ------------------------------------------------------------- */
typedef void (*PkFrameChangeFn)(void *user, int frame_idx);
void pk_on_frame_change(PkFrameChangeFn fn, void *user);  /* via SelectFrame hook */

#ifdef __cplusplus
}
#endif
#endif /* PK_API_H */
