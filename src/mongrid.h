#ifndef MONGRID_H
#define MONGRID_H

#include "nstr.h"

void mongrid_create(bool gui, bool stats, bool modebar);
int32_t mongrid_init(uint32_t num, pthread_t tid);
void mongrid_run(void);
void mongrid_restart(void);
void mongrid_reset(void);
void mongrid_destroy(void);
void mongrid_set_mon(uint32_t idx, nstr_t mid, uint32_t accent, bool aspect,
	uint32_t font_sz, nstr_t crop, uint32_t hgap, uint32_t vgap);
void mongrid_play_stream(uint32_t idx, nstr_t cam_id, nstr_t loc, nstr_t desc,
	nstr_t encoding, uint32_t latency);
bool mongrid_mon_selected(void);
nstr_t mongrid_status(nstr_t str);
void mongrid_display(nstr_t mon, nstr_t cam, nstr_t seq);
void mongrid_set_pan(int16_t pan);
void mongrid_set_tilt(int16_t tilt);
void mongrid_set_zoom(int16_t zoom);

#endif
