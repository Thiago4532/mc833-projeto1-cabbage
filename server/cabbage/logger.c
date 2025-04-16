#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define LOG_BUFFER_SIZE 4096

static int log_fd = -1;

// TODO: Essa função podem ser unificada com o server.c
static void Movie_free(Movie* movie) {
    if (!movie) return;
    free(movie->title);
    free(movie->genres);
    free(movie->director);
    free(movie->release_year);
    free(movie);
}

// TODO: Essa função podem ser unificada com o server.c
static int genre_exists(const char* genres, const char* genre) {
    if (!genres || !genre) return 0;
    size_t len = strlen(genre);
    const char* current = genres;
    while ((current = strstr(current, genre)) != NULL) {
        int start_ok = (current == genres || *(current - 1) == ',');
        int end_ok = (*(current + len) == '\0' || *(current + len) == ',');
        if (start_ok && end_ok) {
            return 1;
        }
        current += 1;
    }
    return 0;
}

int log_init(const char* filename) {
    log_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        perror("log_init: open failed");
        return -1;
    }
    return 0;
}

void log_close(void) {
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}

static int write_log_entry(const char* entry_buffer, size_t length) {
    if (log_fd < 0) {
        fprintf(stderr, "Logger not initialized.\n");
        return -1;
    }
    ssize_t written = write(log_fd, entry_buffer, length);
    if (written < 0) {
        perror("write_log_entry: write failed");
        return -1;
    }
    if ((size_t)written != length) {
        fprintf(stderr, "WARNING: Partial write to log file (%zd / %zu bytes).\n", written, length);
        return -1;
    }
    return 0;
}

int log_add_movie(const Movie* movie) {
    if (!movie) {
        fprintf(stderr, "log_add_movie: received NULL movie pointer.\n");
        return -1;
    }
    char buffer[LOG_BUFFER_SIZE];
    int len = snprintf(buffer, LOG_BUFFER_SIZE, "ADD %u %s|%s|%s|%s\n",
            movie->id,
            movie->title ? movie->title : "",
            movie->genres ? movie->genres : "",
            movie->director ? movie->director : "",
            movie->release_year ? movie->release_year : "");

    if (len < 0 || len >= LOG_BUFFER_SIZE) {
        fprintf(stderr, "log_add_movie: snprintf error or buffer too small.\n");
        return -1;
    }
    return write_log_entry(buffer, len);
}

int log_add_genre(uint32_t movie_id, const char* genre) {
    char buffer[LOG_BUFFER_SIZE];
    int len = snprintf(buffer, LOG_BUFFER_SIZE, "ADDGENRE %u %s\n",
            movie_id, genre ? genre : "");

    if (len < 0 || len >= LOG_BUFFER_SIZE) {
        fprintf(stderr, "log_add_genre: snprintf error or buffer too small.\n");
        return -1;
    }
    return write_log_entry(buffer, len);
}

int log_remove_movie(uint32_t movie_id) {
    char buffer[LOG_BUFFER_SIZE];
    int len = snprintf(buffer, LOG_BUFFER_SIZE, "REM %u\n", movie_id);

    if (len < 0 || len >= LOG_BUFFER_SIZE) {
        fprintf(stderr, "log_remove_movie: snprintf error or buffer too small.\n");
        return -1;
    }
    return write_log_entry(buffer, len);
}

int log_restore(const char* filename,
        MovieEntry* entries,
        size_t max_entries,
        atomic_uint* movie_count_ptr,
        atomic_uint* next_id_ptr)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        if (errno == ENOENT) {
            atomic_store(movie_count_ptr, 0);
            atomic_store(next_id_ptr, 1);
            return 0;
        } else {
            perror("log_restore: fopen failed");
            return -1;
        }
    }

    char line[LOG_BUFFER_SIZE];
    uint32_t max_id_seen = 0;
    uint32_t current_count = 0;
    int parse_errors = 0;

    while (fgets(line, sizeof(line), f)) {
        // Considera tanto CRLF quanto LF como terminadores de linha
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "ADD ", 4) == 0) {
            uint32_t id;
            char* data_part = line + 4;
            char* title_part = data_part;
            char* genres_part = strchr(title_part, '|');
            char* director_part = genres_part ? strchr(genres_part + 1, '|') : NULL;
            char* year_part = director_part ? strchr(director_part + 1, '|') : NULL;

            if (!genres_part || !director_part || !year_part) {
                fprintf(stderr, "Log Restore Error (ADD): Malformed line: %s\n", line);
                parse_errors++;
                continue;
            }

            *genres_part = '\0';
            *director_part = '\0';
            *year_part = '\0';

            if (sscanf(title_part, "%u ", &id) != 1) {
                fprintf(stderr, "Log Restore Error (ADD): Failed ID parse: %s\n", line);
                *genres_part = '|'; *director_part = '|'; *year_part = '|';
                parse_errors++;
                continue;
            }
            char* title_start = strchr(title_part, ' ');
            if (!title_start) {
                fprintf(stderr, "Log Restore Error (ADD): Malformed title/ID: %s\n", line);
                *genres_part = '|'; *director_part = '|'; *year_part = '|';
                parse_errors++;
                continue;
            }
            title_start++;

            int idx = -1;
            for (size_t i = 0; i < max_entries; ++i) {
                if (entries[i].movie == NULL) {
                    idx = i;
                    break;
                }
            }

            if (idx == -1) {
                fprintf(stderr, "Log Restore Error (ADD): No free slots for ID %u.\n", id);
                *genres_part = '|'; *director_part = '|'; *year_part = '|';
                parse_errors++;
                continue;
            }

            Movie* movie = malloc(sizeof(Movie));
            if (!movie) {
                perror("Log Restore Error (ADD): malloc failed");
                *genres_part = '|'; *director_part = '|'; *year_part = '|';
                parse_errors++;
                break;
            }
            memset(movie, 0, sizeof(Movie));

            movie->id = id;
            movie->title = strdup(title_start);
            movie->genres = strdup(genres_part + 1);
            movie->director = strdup(director_part + 1);
            movie->release_year = strdup(year_part + 1);

            *genres_part = '|'; *director_part = '|'; *year_part = '|';

            if (!movie->title || !movie->genres || !movie->director || !movie->release_year) {
                fprintf(stderr, "Log Restore Error (ADD): strdup failed for ID %u\n", id);
                Movie_free(movie);
                parse_errors++;
                continue;
            }

            entries[idx].movie = movie;
            current_count++;
            if (id > max_id_seen) max_id_seen = id;

        } else if (strncmp(line, "REM ", 4) == 0) {
            uint32_t id;
            if (sscanf(line + 4, "%u", &id) == 1) {
                int found = 0;
                for (size_t i = 0; i < max_entries; ++i) {
                    if (entries[i].movie && entries[i].movie->id == id) {
                        Movie_free(entries[i].movie);
                        entries[i].movie = NULL;
                        current_count--;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "Log Restore Warning (REM): ID %u not found.\n", id);
                }
            } else {
                fprintf(stderr, "Log Restore Error (REM): Malformed line: %s\n", line);
                parse_errors++;
            }
        } else if (strncmp(line, "ADDGENRE ", 9) == 0) {
            uint32_t id;
            char genre[LOG_BUFFER_SIZE];

            if (sscanf(line + 9, "%u %s", &id, genre) == 2) {
                int found = 0;
                for (size_t i = 0; i < max_entries; ++i) {
                    if (entries[i].movie && entries[i].movie->id == id) {
                        if (genre_exists(entries[i].movie->genres, genre)) {
                            found = 1;
                            break;
                        }

                        char* old_genres = entries[i].movie->genres;
                        size_t old_len = old_genres ? strlen(old_genres) : 0;
                        size_t add_len = strlen(genre);
                        size_t new_len = old_len + (old_len > 0 ? 1 : 0) + add_len + 1;
                        char* temp_genres = realloc(old_genres, new_len);

                        if (!temp_genres) {
                            perror("Log Restore Error (ADDGENRE): realloc failed");
                            parse_errors++;
                            found = 1;
                            break;
                        }

                        entries[i].movie->genres = temp_genres;

                        if (old_len > 0) {
                            strcat(entries[i].movie->genres, ",");
                        } else {
                            entries[i].movie->genres[0] = '\0';
                        }
                        strcat(entries[i].movie->genres, genre);
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "Log Restore Warning (ADDGENRE): ID %u not found for genre '%s'.\n", id, genre);
                }
            } else {
                fprintf(stderr, "Log Restore Error (ADDGENRE): Malformed line: %s\n", line);
                parse_errors++;
            }
        } else if (strlen(line) > 0) {
            fprintf(stderr, "Log Restore Warning: Unknown line type: %s\n", line);
            parse_errors++;
        }
    }

    fclose(f);

    atomic_store(movie_count_ptr, current_count);
    atomic_store(next_id_ptr, max_id_seen + 1);

    if (parse_errors > 0) {
        fprintf(stderr, "Log Restore finished with %d parsing errors.\n", parse_errors);
        return -1;
    }

    return 1;
}
