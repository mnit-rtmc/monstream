#ifndef MONGRID_H
#define MONGRID_H

#include "nstr.h"

void mongrid_create(bool gui, bool stats);
int32_t mongrid_init(uint32_t num);
void mongrid_run(void);
void mongrid_restart(void);
void mongrid_reset(void);
void mongrid_destroy(void);
void mongrid_set_mon(uint32_t idx, const char *mid, const char *accent,
	bool aspect, uint32_t font_sz, const char *crop, uint32_t hgap,
	uint32_t vgap);
void mongrid_play_stream(uint32_t idx, const char *cam_id, const char *loc,
	const char *desc, const char *encoding, uint32_t latency);
nstr_t mongrid_status(nstr_t str);

#endif
