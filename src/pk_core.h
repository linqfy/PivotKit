/* pk_core.h - PivotKit 2 core: base types, logging, version gate.
 *
 * Part of the PivotKit rewrite (branch: rewrite). See research/sdk-notes/
 * SDK-DESIGN.md for the architecture and research/ for the RE database this
 * code is built against (Pivot Animator 5.2.11, sha256 2c7911d3...).
 */
#ifndef PK_CORE_H
#define PK_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PK_IMAGE_BASE        0x400000u
#define PK_PIVOT_VERSION     "5.2.11"
#define PK_PIVOT_SHA256_16   "2c7911d3303cc28d"   /* first 16 hex chars */

typedef void *PkObj;          /* any Delphi object (VMT ptr at +0) */
typedef void *PkStr;          /* Delphi UnicodeString data ptr */
typedef void *PkDynArr;       /* Delphi dynamic array data ptr */

/* Delphi VMT slot offsets from ClassType (see research/architecture/overview.md) */
#define PK_VMT_SELF_PTR      (-88)
#define PK_VMT_INIT_TABLE    (-76)
#define PK_VMT_TYPE_INFO     (-72)
#define PK_VMT_FIELD_TABLE   (-68)
#define PK_VMT_METHOD_TABLE  (-64)
#define PK_VMT_CLASS_NAME    (-56)
#define PK_VMT_INSTANCE_SIZE (-52)
#define PK_VMT_PARENT        (-48)
#define PK_VMT_TOBJECT_FIRST (-44)   /* Equals */
#define PK_VMT_DESTROY       (-4)

/* ---- logging ----------------------------------------------------------- */
typedef enum {
    PK_LOG_DEBUG = 0, PK_LOG_INFO, PK_LOG_WARN, PK_LOG_ERROR
} PkLogLevel;

void pk_log(PkLogLevel level, const char *fmt, ...);
const char *pk_log_path(void);

/* ---- module lifecycle --------------------------------------------------- */
/* pk_core_init() validates the host binary; returns 0 on success.
 * pk_version_ok() is the cheap check mods can call. */
int  pk_core_init(void *pivot_image_base);
int  pk_version_ok(void);
void *pk_image_base(void);

#ifdef __cplusplus
}
#endif
#endif /* PK_CORE_H */
