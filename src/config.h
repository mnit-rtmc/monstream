#ifndef CONFIG_H
#define CONFIG_H

#include "nstr.h"

void config_init(void);
void config_destroy(void);
nstr_t config_load(const char *name, nstr_t str);
nstr_t config_load_cache(uint64_t hash, nstr_t str);
ssize_t config_store(const char *name, nstr_t str);
ssize_t config_store_cache(uint64_t hash, nstr_t str);
void config_test();

#endif
