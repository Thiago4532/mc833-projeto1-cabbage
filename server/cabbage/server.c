#ifndef _CABBAGE_SERVER_C
#define _CABBAGE_SERVER_C

// Esse é o código do servidor, usamos uma estrutura de dados bem simples, apenas um array de MovieEntry, onde cada MovieEntry
// é composta por um ponteiro de Movie e um mutex, a sincronização também é feita de forma simples, fazendo um lock sempre que tentar
// analisar um filme, essa operação pode se tornar ineficiente, mas é uma forma de sincronização simples e fácil de entender.
// Uma alternativa ao array seria usar Hash Tables ou B-Trees, mas preferi fazer da forma mais simples possível inicialmente.
//
// Para armazenar os filmes no sistema, guardamos o log de cada operação realizada, e reconstruímos o estado do sistema a partir desse log.
// Esse método é bem eficiente, pois se aproveita da atomicidade das operações de write() no sistema de arquivos, garantindo as transações.
// Claro que isso também implica que apenas um servidor por vez, e meu código não lida com isso, então por favor evite executar mais de um servidor com o mesmo arquivo de log.
//
// Também estou assumindo um máximo de MAX_ENTRIES filmes, que nesse caso ai é 65536.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>

#include "MovieEntry.h"
#include "cabbage/common/Packet.h"
#include "logger.h"

#define DEFAULT_PORT 12345
#define MAX_ENTRIES 65536
#define MAX_BACKLOG 128
#define LOG_FILE "cabbage.log"

MovieEntry movie_entries[MAX_ENTRIES];
atomic_uint next_movie_id;
atomic_uint movie_count;

// Apenas um DTO para passar o fd do cliente para a thread.
typedef struct {
    int client_fd;
} client_args_t;

static void Movie_free(Movie* movie) {
    if (!movie) return;
    free(movie->title);
    free(movie->genres);
    free(movie->director);
    free(movie->release_year);
    free(movie);
}

static void send_error_packet(int client_fd, const char* error_message) {
    fprintf(stderr, "Client %d Error: %s\n", client_fd, error_message);
    S2CPacket response;
    response.type = S2C_ERROR;
    response.data.error.message = strdup(error_message);
    if (!response.data.error.message) {
        perror("strdup failed for error message");
        return;
    }
    if (S2CPacket_send(client_fd, &response) < 0) {
        perror("Failed to send error packet");
    }
    S2CPacket_free(&response);
}

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

// Função de handle da Thread.
void* handle_client(void* arg) {
    client_args_t* args = (client_args_t*)arg;
    int client_fd = args->client_fd;
    free(args);

    printf("Client %d connected.\n", client_fd);

    C2SPacket request;
    S2CPacket response;

    while (C2SPacket_recv(client_fd, &request) >= 0) {
        memset(&response, 0, sizeof(S2CPacket));
        int requires_lock = 1;
        int answered;

        // Como eu disse, cada operação é feita usando lock/unlock. Um número atômico é usado para contar o número de filmes, e outro para o próximo ID disponível.
        // A ideia é que uma transação reserva um id antes de fazer a operação.
        switch (request.type) {
        case C2S_ADD_MOVIE:
            if (atomic_load(&movie_count) >= MAX_ENTRIES) {
                send_error_packet(client_fd, "Maximum number of movies reached");
                C2SPacket_free(&request);
                continue;
            }

            u32 new_id = atomic_fetch_add(&next_movie_id, 1);
            int movie_idx = -1;

            for (int i = 0; i < MAX_ENTRIES; ++i) {
                if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                if (movie_entries[i].movie == NULL) {
                    Movie* new_movie = malloc(sizeof(Movie));
                    if (!new_movie) {
                        MovieEntry_unlock(&movie_entries[i]);
                        perror("malloc failed for new Movie");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        movie_idx = -2; // um placeholder para indicar erro
                        break;
                    }
                    memset(new_movie, 0, sizeof(Movie));

                    new_movie->id = new_id;
                    new_movie->title = strdup(request.data.add_movie.title);
                    new_movie->genres = strdup(request.data.add_movie.genres);
                    new_movie->director = strdup(request.data.add_movie.director);
                    new_movie->release_year = strdup(request.data.add_movie.release_year);

                    if (!new_movie->title || !new_movie->genres || !new_movie->director || !new_movie->release_year) {
                        Movie_free(new_movie);
                        MovieEntry_unlock(&movie_entries[i]);
                        perror("strdup failed for movie data");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        movie_idx = -2; // um placeholder para indicar erro
                        break;
                    }

                    movie_entries[i].movie = new_movie;
                    atomic_fetch_add(&movie_count, 1);
                    movie_idx = i;
                    printf("Server: Added movie '%s' (ID: %u) at index %d\n", new_movie->title, new_id, i);
                    log_add_movie(new_movie);
                    MovieEntry_unlock(&movie_entries[i]);
                    break;
                }
                MovieEntry_unlock(&movie_entries[i]);
            }

            // Envia o filme de volta nas operações que precisam dele.
            if (movie_idx >= 0) {
                response.type = S2C_MOVIE;
                response.data.movie = *movie_entries[movie_idx].movie;
                response.data.movie.title = strdup(movie_entries[movie_idx].movie->title);
                response.data.movie.genres = strdup(movie_entries[movie_idx].movie->genres);
                response.data.movie.director = strdup(movie_entries[movie_idx].movie->director);
                response.data.movie.release_year = strdup(movie_entries[movie_idx].movie->release_year);
                if (S2CPacket_send(client_fd, &response) < 0) {
                    perror("Failed to send add movie confirmation");
                }
                S2CPacket_free(&response);
            } else if (movie_idx == -1) {
                send_error_packet(client_fd, "Internal server error: no free slots found (concurrent addition?)");
            }
            break;

        case C2S_ADD_GENRE_TO_MOVIE:
            // Assumimos que os gêneros são separados por vírgulas, então gêneros individuais não podem conter vírgulas.
            if (strchr(request.data.add_genre.genre, ',')) {
                send_error_packet(client_fd, "Genre cannot contain ','");
                break;
            }

            answered = 0;
            for (int i = 0; i < MAX_ENTRIES; ++i) {
                // Extremamente ineficiente, mas é o que temos.
                if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                if (movie_entries[i].movie && movie_entries[i].movie->id == request.data.add_genre.movie_id) {
                    if (genre_exists(movie_entries[i].movie->genres, request.data.add_genre.genre)) {
                        MovieEntry_unlock(&movie_entries[i]);
                        send_error_packet(client_fd, "Genre already exists for this movie");
                        answered = 1;
                        break;
                    }

                    char* old_genres = movie_entries[i].movie->genres;
                    size_t old_len = old_genres ? strlen(old_genres) : 0;
                    size_t add_len = strlen(request.data.add_genre.genre);
                    size_t new_len = old_len + (old_len > 0 ? 1 : 0) + add_len + 1; // +1 for comma, +1 for null
                    char* new_genres = (char*)realloc(old_genres, new_len);

                    if (!new_genres) {
                        movie_entries[i].movie->genres = old_genres; // Volta o ponteiro se falhar
                        MovieEntry_unlock(&movie_entries[i]);
                        perror("realloc failed for genres");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        answered = 1;
                        break;
                    }

                    if (old_len > 0) {
                        strcat(new_genres, ",");
                    } else {
                        new_genres[0] = '\0';
                    }
                    strcat(new_genres, request.data.add_genre.genre);
                    movie_entries[i].movie->genres = new_genres;
                    printf("Server: Added genre '%s' to movie ID %u\n", request.data.add_genre.genre, request.data.add_genre.movie_id);
                    log_add_genre(movie_entries[i].movie->id, request.data.add_genre.genre);

                    MovieEntry_unlock(&movie_entries[i]);
                    response.type = S2C_OK;

                    if (S2CPacket_send(client_fd, &response) < 0) {
                        perror("Failed to send add genre confirmation");
                    }
                    answered = 1;
                    break;
                }
                MovieEntry_unlock(&movie_entries[i]);
            }
            if (!answered) {
                send_error_packet(client_fd, "Movie ID not found");
            }
            break;

        case C2S_REMOVE_MOVIE:
            answered = 0;

            for (int i = 0; i < MAX_ENTRIES; ++i) {
                if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                if (movie_entries[i].movie && movie_entries[i].movie->id == request.data.remove_movie.movie_id) {
                    printf("Server: Removing movie '%s' (ID: %u) from index %d\n", movie_entries[i].movie->title, movie_entries[i].movie->id, i);
                    Movie_free(movie_entries[i].movie);
                    movie_entries[i].movie = NULL;
                    atomic_fetch_sub(&movie_count, 1);
                    log_remove_movie(request.data.remove_movie.movie_id);
                    MovieEntry_unlock(&movie_entries[i]);

                    response.type = S2C_OK;
                    if (S2CPacket_send(client_fd, &response) < 0) {
                        perror("Failed to send remove movie confirmation");
                    }

                    answered = 1;
                    break;
                }
                MovieEntry_unlock(&movie_entries[i]);
            }
            if (answered == 0) {
                send_error_packet(client_fd, "Movie ID not found for removal");
            }
            break;

        case C2S_LIST_MOVIES:
        case C2S_LIST_MOVIES_DETAILED: 
            { // tive que colocar um scope aqui, o switch do C é mt triste de lidar :(, odeio fallthrough
                u32 current_movie_count = atomic_load(&movie_count);
                if (current_movie_count == 0) {
                    response.type = (request.type == C2S_LIST_MOVIES) ? S2C_MOVIE_LIST : S2C_MOVIE_LIST_DETAILED;
                    response.data.movie_list.count = 0;
                    response.data.movie_list.movies = NULL;
                } else {
                    void* list_buffer = NULL;
                    size_t element_size = (request.type == C2S_LIST_MOVIES) ? sizeof(S2C_MovieIdTitle) : sizeof(Movie);
                    list_buffer = malloc(current_movie_count * element_size);

                    if (!list_buffer) {
                        perror("malloc failed for movie list");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        break;
                    }

                    u32 list_count = 0;
                    for (int i = 0; i < MAX_ENTRIES && list_count < current_movie_count; ++i) {
                        if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                        if (movie_entries[i].movie) {
                            if (request.type == C2S_LIST_MOVIES) {
                                S2C_MovieIdTitle* entry = &((S2C_MovieIdTitle*)list_buffer)[list_count];
                                entry->id = movie_entries[i].movie->id;
                                entry->title = strdup(movie_entries[i].movie->title);
                                if (!entry->title) {
                                    list_count = -1;
                                    MovieEntry_unlock(&movie_entries[i]);
                                    break;
                                }
                            } else { // DETAILED
                                Movie* entry = &((Movie*)list_buffer)[list_count];
                                entry->id = movie_entries[i].movie->id;
                                entry->title = strdup(movie_entries[i].movie->title);
                                entry->genres = strdup(movie_entries[i].movie->genres);
                                entry->director = strdup(movie_entries[i].movie->director);
                                entry->release_year = strdup(movie_entries[i].movie->release_year);
                                if (!entry->title || !entry->genres || !entry->director || !entry->release_year) {
                                    list_count = -1;
                                    MovieEntry_unlock(&movie_entries[i]);
                                    break;
                                }
                            }
                            list_count++;
                        }
                        MovieEntry_unlock(&movie_entries[i]);
                    }

                    if (list_count == (u32)-1) {
                        u32 actual_count = 0;
                        for (int i = 0; i < MAX_ENTRIES && actual_count < current_movie_count; ++i) {
                            if (movie_entries[i].movie) {
                                if (request.type == C2S_LIST_MOVIES) {
                                    S2C_MovieIdTitle* entry = &((S2C_MovieIdTitle*)list_buffer)[actual_count];
                                    free(entry->title);
                                } else {
                                    Movie* entry = &((Movie*)list_buffer)[actual_count];
                                    free(entry->title); free(entry->genres); free(entry->director); free(entry->release_year);
                                }
                                actual_count++;
                                if (actual_count == list_count) break;
                            }
                        }

                        free(list_buffer);
                        perror("strdup failed during list creation");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        break;
                    }

                    response.type = (request.type == C2S_LIST_MOVIES) ? S2C_MOVIE_LIST : S2C_MOVIE_LIST_DETAILED;
                    if (request.type == C2S_LIST_MOVIES) {
                        response.data.movie_list.count = list_count;
                        response.data.movie_list.movies = (S2C_MovieIdTitle*)list_buffer;
                    } else {
                        response.data.movie_list_detailed.count = list_count;
                        response.data.movie_list_detailed.movies = (Movie*)list_buffer;
                    }
                }

                if (S2CPacket_send(client_fd, &response) < 0) {
                    perror("Failed to send movie list");
                }
                S2CPacket_free(&response);
            }
            break;

        case C2S_GET_MOVIE:
            answered = 0;
            for (int i = 0; i < MAX_ENTRIES; ++i) {
                if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                if (movie_entries[i].movie && movie_entries[i].movie->id == request.data.get_movie.movie_id) {
                    response.type = S2C_MOVIE;
                    response.data.movie = *movie_entries[i].movie;
                    response.data.movie.title = strdup(movie_entries[i].movie->title);
                    response.data.movie.genres = strdup(movie_entries[i].movie->genres);
                    response.data.movie.director = strdup(movie_entries[i].movie->director);
                    response.data.movie.release_year = strdup(movie_entries[i].movie->release_year);

                    if (!response.data.movie.title || !response.data.movie.genres || !response.data.movie.director || !response.data.movie.release_year) {
                        S2CPacket_free(&response);
                        MovieEntry_unlock(&movie_entries[i]);
                        perror("strdup failed for get movie");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        answered = 1;
                        break;
                    }

                    MovieEntry_unlock(&movie_entries[i]);
                    if (S2CPacket_send(client_fd, &response) < 0) {
                        perror("Failed to send get movie response");
                    }
                    S2CPacket_free(&response);
                    answered = 1;
                    break;
                }
                MovieEntry_unlock(&movie_entries[i]);
            }
            if (!answered) {
                send_error_packet(client_fd, "Movie ID not found");
            }
            break;

        case C2S_LIST_MOVIES_BY_GENRE:
            {
                u32 current_movie_count = atomic_load(&movie_count);
                if (current_movie_count == 0) {
                    response.type = S2C_MOVIE_LIST;
                    response.data.movie_list.count = 0;
                    response.data.movie_list.movies = NULL;
                } else {
                    if (!request.data.list_by_genre.genre || strlen(request.data.list_by_genre.genre) == 0) {
                        send_error_packet(client_fd, "Genre cannot be empty");
                        break;
                    }
                    if (strchr(request.data.list_by_genre.genre, ',')) {
                        // Como usamos ',' como delimitador, não podemos permitir que o gênero tenha ','.
                        send_error_packet(client_fd, "Genre cannot contain ','");
                        break;
                    }

                    S2C_MovieIdTitle* list_buffer = (S2C_MovieIdTitle*)malloc(current_movie_count * sizeof(S2C_MovieIdTitle));

                    if (!list_buffer) {
                        perror("malloc failed for genre list");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        break;
                    }

                    u32 list_count = 0;
                    for (int i = 0; i < MAX_ENTRIES && list_count < current_movie_count; ++i) {
                        if (MovieEntry_lock(&movie_entries[i]) != 0) continue;
                        if (movie_entries[i].movie && genre_exists(movie_entries[i].movie->genres, request.data.list_by_genre.genre)) {
                            S2C_MovieIdTitle* entry = &list_buffer[list_count];
                            entry->id = movie_entries[i].movie->id;
                            entry->title = strdup(movie_entries[i].movie->title);
                            if (!entry->title) { list_count = -1; MovieEntry_unlock(&movie_entries[i]); break; }
                            list_count++;
                        }
                        MovieEntry_unlock(&movie_entries[i]);
                    }

                    // um hackzinho pra detectar erro. o (u32)-1 é um valor inválido.
                    if (list_count == (u32)-1) {
                        u32 actual_count = 0;
                        for (int i = 0; i < MAX_ENTRIES && actual_count < current_movie_count; ++i) {
                            if (movie_entries[i].movie && genre_exists(movie_entries[i].movie->genres, request.data.list_by_genre.genre)) {
                                S2C_MovieIdTitle* entry = &list_buffer[actual_count];
                                free(entry->title);
                                actual_count++;
                            }
                        }
                        free(list_buffer);
                        perror("strdup failed during genre list creation");
                        send_error_packet(client_fd, "Internal server error: allocation failed");
                        break;
                    }

                    response.type = S2C_MOVIE_LIST;
                    response.data.movie_list.count = list_count;
                    response.data.movie_list.movies = list_buffer;
                }

                if (S2CPacket_send(client_fd, &response) < 0) {
                    perror("Failed to send movie list by genre");
                }
                S2CPacket_free(&response);
            }
            break;
        default:
            send_error_packet(client_fd, "Unknown C2S packet type received");
            break;
        }
        C2SPacket_free(&request);
    }

    if (errno != 0 && errno != ECONNRESET) {
        perror("C2SPacket_recv error");
    }

    printf("Client %d disconnected.\n", client_fd);
    close(client_fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    int server_port = DEFAULT_PORT;

    if (argc >= 2) {
        server_port = atoi(argv[1]);
    }

    printf("Initializing server...\n");

    for (int i = 0; i < MAX_ENTRIES; ++i) {
        movie_entries[i].movie = NULL;
        if (MovieEntry_init(&movie_entries[i]) != 0) {
            fprintf(stderr, "Failed to initialize mutex for entry %d\n", i);
            return 1;
        }
    }
    printf("Initialized %d movie entry slots.\n", MAX_ENTRIES);

    switch (log_restore(LOG_FILE, movie_entries, MAX_ENTRIES, &movie_count, &next_movie_id)) {
        case 0:
            printf("Log file not found, starting fresh...\n");
            break;
        case 1:
            printf("Log file restored successfully.\n");
            printf("Current movie count: %u\n", atomic_load(&movie_count));
            break;
        case -1:
            fprintf(stderr, "Failed to restore log file\n");
            return 1;
    }

    if (log_init(LOG_FILE) < 0) {
        fprintf(stderr, "Failed to initialize log file\n");
        return 1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", server_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        client_args_t* args = malloc(sizeof(client_args_t));
        if (!args) {
            perror("malloc failed for client args");
            close(client_fd);
            continue;
        }
        args->client_fd = client_fd;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)args) != 0) {
            perror("pthread_create failed");
            free(args);
            close(client_fd);
            continue;
        }

        pthread_detach(thread_id);
    }

    for (int i = 0; i < MAX_ENTRIES; ++i) {
        MovieEntry_free(&movie_entries[i]);
    }
    close(server_fd);

    return 0;
}

#endif // _CABBAGE_SERVER_C
