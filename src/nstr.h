#ifndef NSTR_H
#define NSTR_H

#include <stdbool.h>
#include <stdint.h>

struct nstr {
	char		*buf;		/* string buffer */
	uint32_t	buf_len;	/* buffer length */
	uint32_t	len;		/* string length */
};
typedef struct nstr nstr_t;

nstr_t nstr_make(char *buf, uint32_t buf_len, uint32_t len);
nstr_t nstr_make_cpy(char *buf, uint32_t buf_len, uint32_t len, nstr_t src);
nstr_t nstr_dup(nstr_t o);
uint32_t nstr_len(nstr_t str);
bool nstr_cat(nstr_t *dst, nstr_t src);
bool nstr_cpy(nstr_t *dst, nstr_t src);
bool nstr_cat_z(nstr_t *dst, const char *src);
const char *nstr_z(nstr_t str);
nstr_t nstr_split(nstr_t *str, char c);
nstr_t nstr_chop(nstr_t str, char c);
bool nstr_cmp_z(nstr_t str, const char *buf);
bool nstr_wrap(char *buf, size_t n, nstr_t str);
int32_t nstr_parse_u32(nstr_t str);
int32_t nstr_parse_hex(nstr_t hex);

#endif
