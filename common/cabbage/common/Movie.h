#ifndef _CABBAGE_MOVIE_H
#define _CABBAGE_MOVIE_H

#include "cabbage/common/types.h"

typedef struct
{
    u32 id;
    char* title;
    char* genres;
    char* director;
    char *release_year;
} Movie;

#endif // _CABBAGE_MOVIE_H
