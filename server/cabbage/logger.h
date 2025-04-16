#ifndef _CABBAGE_LOGGER_H
#define _CABBAGE_LOGGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "MovieEntry.h"
#include "cabbage/common/Movie.h"


int log_init(const char* filename);
void log_close(void);

int log_add_movie(const Movie* movie);
int log_add_genre(uint32_t movie_id, const char* genre);
int log_remove_movie(uint32_t movie_id);

int log_restore(const char* filename,
                MovieEntry* entries,
                size_t max_entries,
                atomic_uint* movie_count_ptr,
                atomic_uint* next_id_ptr);

#endif // _CABBAGE_LOGGER_H
