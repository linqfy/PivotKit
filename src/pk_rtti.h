/* pk_rtti.h - runtime Delphi RTTI navigation: class lookup by name,
 * published method/field resolution. Static private-field offsets live in
 * include/pivot/ (generated from docs/research/). */
#ifndef PK_RTTI_H
#define PK_RTTI_H

#include "pk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Class references (VMT addresses) - no hardcoded VAs, resolved by name. */
uint32_t pk_class_find(const char *class_name);       /* ClassType VA or 0 */

/* Published member resolution on any object/class. */
uint32_t pk_method_addr(PkObj obj, const char *method);  /* code VA or 0 */
long     pk_field_offset(PkObj obj, const char *field);  /* offset or -1 */

/* Same, from a ClassType VA directly (no instance needed). */
uint32_t pk_class_method_addr(uint32_t classtype, const char *method);
long     pk_class_field_offset(uint32_t classtype, const char *field);

/* Introspection helpers. */
void pk_class_name(PkObj obj, char *buf, size_t bufsz);
int  pk_is_object(PkObj p);

/* Delphi string bridge. UnicodeString: length(chars) at ptr-4, header-12. */
PkStr pk_str_make(const char *utf8);       /* constant (non-owning) string */
void pk_str_read(PkStr s, char *buf, size_t bufsz);

/* Delphi register-convention invoker (EAX=self, EDX=a1, ECX=a2, stack rest).
 * Returns EAX. `more` holds args 3..n pushed right-to-left. The callee's
 * ret-N cleanup is honored by the stub. */
void *pk_call(PkObj self, uint32_t fn, void *a1, void *a2,
              const uint32_t *more, int n_more);

#ifdef __cplusplus
}
#endif
#endif /* PK_RTTI_H */
