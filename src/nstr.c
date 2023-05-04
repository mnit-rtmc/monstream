/*
 * Copyright (C) 2017-2023  Minnesota Department of Transportation
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
 * @param buf     Buffer for nstr to own.
 * @param buf_len Length of nstr buffer.
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
 * @param buf     Buffer for nstr to own.
 * @param buf_len Length of nstr buffer.
 * @param len     Length of data in buffer.
 * @return Initialized nstr.
 */
nstr_t nstr_init_n(char *buf, uint32_t buf_len, uint32_t len) {
	nstr_t str;
	str.buf = buf;
	str.buf_len = buf_len;
	str.len = (len < buf_len) ? len : buf_len;
	return str;
}

/** Get the length of an nstr.
 *
 * @param str The string.
 * @return Length (bytes).
 */
uint32_t nstr_len(nstr_t str) {
	return str.len;
}

/** Append a string to another string.
 *
 * @param dst IN/OUT nstr to append to.
 * @param src nstr to append.
 * @return true if string was truncated.
 */
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

/** Append a C string (zero-terminated) to an nstr.
 *
 * @param dst IN/OUT nstr to append to.
 * @param src C string to append.
 * @return true if string was truncated.
 */
bool nstr_cat_z(nstr_t *dst, const char *src) {
	uint32_t len = dst->len;
	uint32_t n = dst->buf_len - len;
	for (uint32_t i = 0; i < n; i++) {
		if ('\0' == src[i]) {
			dst->len += i;
			return false;
		}
		dst->buf[len + i] = src[i];
	}
	dst->len = dst->buf_len;
	return (src[n] != '\0');
}

/** Append a char to an nstr.
 *
 * @param dst IN/OUT nstr to append to.
 * @param c   Character to append.
 * @return true if string was truncated.
 */
bool nstr_cat_c(nstr_t *dst, char c) {
	uint32_t len = dst->len;
	if (dst->buf_len > len) {
		dst->buf[len] = c;
		dst->len++;
		return false;
	} else
		return true;
}

/** Find a character within a string.
 *
 * @param str The string to search.
 * @param c   Character to find.
 * @return Offset of the character, or the string length if not found.
 */
static uint32_t nstr_find(nstr_t str, char c) {
	for (uint32_t i = 0; i < str.len; i++) {
		if (str.buf[i] == c)
			return i;
	}
	return str.len;
}

/** Split a string on the first occurrence of a character.
 *
 * @param str IN/OUT string to split (updated to remainder of string).
 * @param c   Character to split on.
 * @return String ending before the character.
 */
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

/** Truncate a string on the first occurrence of a character.
 *
 * @param str String to truncate.
 * @param c   Character to truncate on.
 * @return String ending before the character.
 */
nstr_t nstr_chop(nstr_t str, char c) {
	uint32_t i = nstr_find(str, c);
	return nstr_init_n(str.buf, i + 1, i);
}

/** Check if a string is equal to a C string.
 *
 * @param str String to compare.
 * @param buf C string to compare.
 * @return true if strings are equal.
 */
bool nstr_cmp_z(nstr_t str, const char *buf) {
	for (uint32_t i = 0; i < str.len; i++) {
		if (str.buf[i] != buf[i])
			return false;
	}
	return buf[str.len] == '\0';
}

/** Check if two strings are equal.
 *
 * @param a First string.
 * @param b Second string.
 * @return true if strings are equal.
 */
bool nstr_equals(nstr_t a, nstr_t b) {
	if (a.len == b.len) {
		for (uint32_t i = 0; i < a.len; i++) {
			if (a.buf[i] != b.buf[i])
				return false;
		}
		return true;
	}
	return false;
}

/** Check if a string has a C string prefix.
 *
 * @param str String to check.
 * @param buf C string prefix.
 * @return true if string starts with the prefix.
 */
bool nstr_starts_with(nstr_t str, const char *buf) {
	for (uint32_t i = 0; i < str.len; i++) {
		if ('\0' == buf[i])
			return true;
		if (str.buf[i] != buf[i])
			return false;
	}
	// buf longer than str
	return false;
}

/** Check if a string contains a C string.
 *
 * @param str String to check.
 * @param buf C string.
 * @return true if string contains the C string.
 */
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
 * @param dst IN/OUT Destination buffer.
 * @param n   Length of destination buffer.
 * @param src String to copy.
 * @return true if string was truncated.
 */
bool nstr_to_cstr(char *dst, size_t n, nstr_t src) {
	bool trunc;
	nstr_t tmp = nstr_init(dst, n);
	trunc = nstr_cat(&tmp, src);
	nstr_z(tmp);
	return trunc;
}

/** Parse a string as an unsigned integer */
int32_t nstr_parse_u32(nstr_t str) {
	char buf[16];
	if (!nstr_to_cstr(buf, sizeof(buf), str))
		return atoi(buf);
	else
		return -1;
}

/** Parse a hexadecimal digit */
static int32_t parse_digit(char d) {
	if (d >= '0' && d <= '9')
		return d - '0';
	else if (d >= 'A' && d <= 'F')
		return 10 + d - 'A';
	else if (d >= 'a' && d <= 'f')
		return 10 + d - 'a';
	else
		return -1;
}

/** Parse a string as a hexadecimal integer */
int32_t nstr_parse_hex(nstr_t hex) {
	int32_t v = 0;
	if (hex.len > 8)
		return -1;
	for (uint32_t i = 0; i < hex.len; i++) {
		int32_t d = parse_digit(hex.buf[i]);
		if (d < 0)
			return -1;
		v <<= 4;
		v |= d;
	}
	return v;
}

/** Calculate an FNV hash of a string */
uint64_t nstr_hash_fnv(nstr_t str) {
	const void *key = str.buf;
	const uint8_t *p = key;
	uint64_t h = 14695981039346656037UL;
	for (uint32_t i = 0; i < str.len; i++) {
		h = (h * 1099511628211UL) ^ p[i];
	}
	return h;
}
