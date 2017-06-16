#ifndef LOCK_H
#define LOCK_H

#define _MULTI_THREADED
#include <pthread.h>

struct lock {
	pthread_mutex_t		mutex;
};

void lock_init(struct lock *l);
void lock_destroy(struct lock *l);
void lock_acquire(struct lock *l);
void lock_release(struct lock *l);

#endif
