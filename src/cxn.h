#ifndef CXN_H
#define CXN_H

#include "nstr.h"

struct cxn *cxn_create(void);
bool cxn_established(struct cxn *cxn);
void cxn_bind(struct cxn *cxn, const char *service);
bool cxn_send(struct cxn *cxn, nstr_t str);
nstr_t cxn_recv(struct cxn *cxn, nstr_t str);
void cxn_destroy(struct cxn *cxn);

#endif
