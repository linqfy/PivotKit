/* pk_rtti.h - Delphi RTTI navigator (hardened port of the proven v1 code).
 *
 * Locates classes by name without hardcoded addresses, resolves published
 * methods (name -> VA) and published fields (name -> instance offset),
 * including inherited tables. The static database
 * (research/mappings/classes_full.json) provides compile-time offsets for
 * private fields; this header covers the runtime-side navigation.
 */
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
