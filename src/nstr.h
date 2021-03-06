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

nstr_t nstr_init_empty(void);
nstr_t nstr_init(char *buf, uint32_t buf_len);
nstr_t nstr_init_n(char *buf, uint32_t buf_len, uint32_t len);
uint32_t nstr_len(nstr_t str);
bool nstr_cat(nstr_t *dst, nstr_t src);
bool nstr_cat_z(nstr_t *dst, const char *src);
bool nstr_cat_c(nstr_t *dst, char c);
nstr_t nstr_split(nstr_t *str, char c);
nstr_t nstr_chop(nstr_t str, char c);
bool nstr_cmp_z(nstr_t str, const char *buf);
bool nstr_equals(nstr_t a, nstr_t b);
bool nstr_starts_with(nstr_t str, const char *buf);
bool nstr_contains(nstr_t str, const char *buf);
const char *nstr_z(nstr_t str);
bool nstr_to_cstr(char *dst, size_t n, nstr_t src);
int32_t nstr_parse_u32(nstr_t str);
int32_t nstr_parse_hex(nstr_t hex);
uint64_t nstr_hash_fnv(nstr_t str);

#endif
