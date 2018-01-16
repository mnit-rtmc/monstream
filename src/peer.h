#ifndef PEER_H
#define PEER_H

#include "nstr.h"

bool peer_exists(void);
void peer_bind(const char *service);
void peer_send(nstr_t str);
nstr_t peer_recv(nstr_t str);
void peer_init(void);
void peer_destroy(void);

#endif
