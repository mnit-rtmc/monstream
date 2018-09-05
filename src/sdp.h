#ifndef SDP_H
#define SDP_H

#include <stdbool.h>
#include <stdint.h>
#include "nstr.h"

struct sdp_data {
	char     cache_buf[1024];
	char     fetch_buf[1024];
	char     loc_buf[128];
	char     udp_buf[128];
	char     sprop_buf[64];
	nstr_t   cache;
	nstr_t   fetch;
	nstr_t   loc;
	nstr_t   udp;
	nstr_t   sprops;
	uint64_t loc_hash;
	bool     is_sdp;
};

void sdp_data_init(struct sdp_data *sdp, nstr_t loc);
bool sdp_data_cache(struct sdp_data *sdp);
bool sdp_data_fetch(struct sdp_data *sdp);

#endif
