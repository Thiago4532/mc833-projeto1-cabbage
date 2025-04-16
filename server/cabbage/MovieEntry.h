#ifndef MOVIE_ENTRY_H
#define MOVIE_ENTRY_H

#include <pthread.h>
#include "cabbage/common/Movie.h"

typedef struct {
    Movie* movie;
    pthread_mutex_t mutex;
} MovieEntry;

int MovieEntry_init(MovieEntry* entry);
int MovieEntry_lock(MovieEntry* entry);
int MovieEntry_unlock(MovieEntry* entry);
int MovieEntry_free(MovieEntry* entry);

#endif
