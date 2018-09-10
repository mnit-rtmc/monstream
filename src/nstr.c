/*
 * Copyright (C) 2017-2018  Minnesota Department of Transportation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "nstr.h"

/** Initialize an empty nstr.
 *
 * @return Empty nstr.
 */
nstr_t nstr_init_empty(void) {
	nstr_t str;
	str.buf = NULL;
	str.buf_len = 0;
	str.len = 0;
	return str;
}

/** Initialize a new nstr.
 *
 * @param buf     [in] Buffer for nstr to own.
 * @param buf_len [in] Length of nstr buffer.
 * @return Initialized nstr.
 */
nstr_t nstr_init(char *buf, uint32_t buf_len) {
	nstr_t str;
	str.buf = buf;
	str.buf_len = buf_len;
	str.len = 0;
	return str;
}

/** Initialize a new nstr with an existing buffer.
 *
 * @param buf     [in] Buffer for nstr to own.
 * @param buf_len [in] Length of nstr buffer.
 * @param len     [in] Length of data in buffer.
 * @return Initialized nstr.
 */
nstr_t nstr_init_n(char *buf, uint32_t buf_len, uint32_t len) {
	nstr_t str;
	str.buf = buf;
	str.buf_len = buf_len;
	str.len = (len < buf_len) ? len : buf_len;
	return str;
}

nstr_t nstr_dup(nstr_t o) {
	nstr_t str;
	str.buf = o.buf;
	str.buf_len = o.buf_len;
	str.len = o.len;
	return str;
}

uint32_t nstr_len(nstr_t str) {
	return str.len;
}

bool nstr_cat(nstr_t *dst, nstr_t src) {
	uint32_t len = dst->len + src.len;
	bool trunc = dst->buf_len <= len;
	uint32_t n = (trunc)
	           ? (dst->buf_len - dst->len)
	           : src.len;
	if (n > 0)
		memcpy(dst->buf + dst->len, src.buf, n);
	dst->len += n;
	return trunc;
}

bool nstr_cat_z(nstr_t *dst, const char *src) {
	int len = dst->len;
	int n = dst->buf_len - len;
	for (int i = 0; i < n; i++) {
		if (src[i] == '\0') {
			dst->len += i;
			return false;
		}
		dst->buf[len + i] = src[i];
	}
	dst->len = dst->buf_len;
	return (src[n] != '\0');
}

static uint32_t nstr_find(nstr_t str, char c) {
	for (uint32_t i = 0; i < str.len; i++) {
		if (str.buf[i] == c)
			return i;
	}
	return str.len;
}

nstr_t nstr_split(nstr_t *str, char c) {
	char *buf = str->buf;
	uint32_t i = nstr_find(*str, c);
	if (i < str->len) {
		uint32_t j = i + 1;
		str->buf = buf + j;
		str->buf_len -= j;
		str->len -= j;
	} else
		str->len = 0;
	return nstr_init_n(buf, i + 1, i);
}

nstr_t nstr_chop(nstr_t str, char c) {
	uint32_t i = nstr_find(str, c);
	return nstr_init_n(str.buf, i + 1, i);
}

bool nstr_cmp_z(nstr_t str, const char *buf) {
	for (int i = 0; i < str.len; i++) {
		if (str.buf[i] != buf[i])
			return false;
	}
	return buf[str.len] == '\0';
}

bool nstr_equals(nstr_t a, nstr_t b) {
	if (a.len == b.len) {
		for (int i = 0; i < a.len; i++) {
			if (a.buf[i] != b.buf[i])
				return false;
		}
		return true;
	}
	return false;
}

bool nstr_starts_with(nstr_t str, const char *buf) {
	for (int i = 0; i < str.len; i++) {
		if ('\0' == buf[i])
			return true;
		if (str.buf[i] != buf[i])
			return false;
	}
	// buf longer than str
	return false;
}

bool nstr_contains(nstr_t str, const char *buf) {
	while (str.len > 0) {
		if (nstr_starts_with(str, buf))
			return true;
		str.buf++;
		str.buf_len--;
		str.len--;
	}
	return false;
}

/** Get nstr as a C string (zero-terminated).
 *
 * @param str String to retrieve.
 * @return C string pointer.
 */
const char *nstr_z(nstr_t str) {
	uint32_t n = (str.len < str.buf_len) ? str.len : str.buf_len - 1;
	// Need to check in case buf_len is zero
	if (n < str.buf_len) {
		str.buf[n] = '\0';
		return str.buf;
	} else
		return "";
}

/** Copy an nstr to a C string (zero-terminated).
 *
 * @param dst [in,out] Destination buffer.
 * @param n   [in]     Length of destination buffer.
 * @param src [in]     String to copy.
 * @return true if string was truncated.
 */
bool nstr_to_cstr(char *dst, size_t n, nstr_t src) {
	bool trunc;
	nstr_t tmp = nstr_init(dst, n);
	trunc = nstr_cat(&tmp, src);
	nstr_z(tmp);
	return trunc;
}

int32_t nstr_parse_u32(nstr_t str) {
	char buf[16];
	if (!nstr_to_cstr(buf, sizeof(buf), str))
		return atoi(buf);
	else
		return -1;
}

static int32_t parse_digit(char d) {
	if (d >= '0' && d <= '9')
		return d - '0';
	else if (d >= 'A' && d <= 'Z')
		return d - 'A';
	else if (d >= 'a' && d <= 'z')
		return d - 'a';
	else
		return -1;
}

int32_t nstr_parse_hex(nstr_t hex) {
	int32_t v = 0;
	if (hex.len > 8)
		return -1;
	for (int i = 0; i < hex.len; i++) {
		int32_t d = parse_digit(hex.buf[i]);
		if (d < 0)
			return -1;
		v <<= 4;
		v |= d;
	}
	return v;
}

uint64_t nstr_hash_fnv(nstr_t str) {
	const void *key = str.buf;
	const uint8_t *p = key;
	uint64_t h = 14695981039346656037UL;
	int i;
	for (i = 0; i < str.len; i++) {
		h = (h * 1099511628211UL) ^ p[i];
	}
	return h;
}
