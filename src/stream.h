#ifndef STREAM_H
#define STREAM_H

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <gst/gst.h>

#define MAX_ELEMS	(16)

struct stream {
	pthread_mutex_t mutex;
	guintptr	handle;
	gboolean	aspect;
	char		location[128];
	char		encoding[8];
	uint32_t	latency;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*elem[MAX_ELEMS];
	void		(*stop)		(struct stream *st);
	void		(*ack_started)	(struct stream *st);
};

void stream_init(struct stream *st, uint32_t idx);
void stream_destroy(struct stream *st);
void stream_lock(struct stream *st);
void stream_unlock(struct stream *st);
void stream_set_handle(struct stream *st, guintptr handle);
void stream_set_aspect(struct stream *st, gboolean aspect);
void stream_set_location(struct stream *st, const char *loc);
void stream_set_encoding(struct stream *st, const char *encoding);
void stream_set_latency(struct stream *st, uint32_t latency);
void stream_start_pipeline(struct stream *st);
void stream_stop_pipeline(struct stream *st);
void stream_start_blank(struct stream *st);

#endif
