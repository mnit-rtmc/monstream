#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>
#include <string.h>
#include <gst/gst.h>
#include "lock.h"

#define MAX_ELEMS	(16)

struct stream {
	struct lock	*lock;
	guintptr	handle;
	gboolean	aspect;
	char		crop[6];         /* crop code */
	char		cam_id[20];      /* camera ID */
	char		location[128];
	char		description[64];
	char		encoding[8];
	char		sprops[64];
	uint64_t	loc_hash;
	uint32_t	latency;
	uint32_t	font_sz;
	uint32_t	hgap;
	uint32_t	vgap;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*elem[MAX_ELEMS];
	GstElement	*jitter;
	guint64		lost;
	guint64		late;
	uint32_t	n_starts;
	void		(*do_stop)	(struct stream *st);
	void		(*ack_started)	(struct stream *st);
};

void stream_init(struct stream *st, uint32_t idx, struct lock *lock);
void stream_destroy(struct stream *st);
void stream_set_handle(struct stream *st, guintptr handle);
void stream_set_aspect(struct stream *st, bool aspect);
void stream_set_id(struct stream *st, const char *cam_id);
void stream_set_font_size(struct stream *st, uint32_t sz);
void stream_set_crop(struct stream *st, const char *crop, uint32_t hgap,
	uint32_t vgap);
void stream_set_params(struct stream *st, const char *cam_id, const char *loc,
	const char *desc, const char *encoding, uint32_t latency);
bool stream_stats(struct stream *st);
void stream_start(struct stream *st);
void stream_stop(struct stream *st);

#endif
