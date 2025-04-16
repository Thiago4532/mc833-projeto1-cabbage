#include "cabbage/common/Packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Para enviar os pacotes, usamos um mecanismo de serialização, onde cada pacote é serializado em um buffer antes
// de ser enviado pela rede, tentando fazer apenas uma chamada de send() para cada pacote.
// Não irei detalhar cada função, mas basicamente cada função de serialização/deserialização é responsável por
// um tipo específico de dado.

// --- Funções utilitárias ---

// Mesmo com o socket em modo blocking, uma interrupção no momento certo pode fazer com que
// o send() envie menos bytes do que o esperado.
static int send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;
    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (sent == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total_sent += (size_t)sent;
    }
    return 0;
}

// Similar ao anterior, na verdade não tenho certeza se é necessário aqui, mas melhor
// prevenir do que remediar.
static int recv_all(int sockfd, void *buf, size_t len) {
    size_t total_recv = 0;
    char *ptr = (char *)buf;
    while (total_recv < len) {
        ssize_t received = recv(sockfd, ptr + total_recv, len - total_recv, 0);
        if (received == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (received == 0) {
            errno = ECONNRESET;
            return -1;
        }
        total_recv += (size_t)received;
    }
    return 0;
}

// Usamos uma função para calcular o tamanho do pacote, para facilitar a serialização.
static size_t calculate_c2s_packet_size(const C2SPacket *packet) {
    size_t size = sizeof(u8);
    size_t len;

    switch (packet->type) {
    case C2S_ADD_MOVIE:
        size += sizeof(u32);
        size += (packet->data.add_movie.title ? strlen(packet->data.add_movie.title) : 0);
        size += sizeof(u32);
        size += (packet->data.add_movie.genres ? strlen(packet->data.add_movie.genres) : 0);
        size += sizeof(u32);
        size += (packet->data.add_movie.director ? strlen(packet->data.add_movie.director) : 0);
        size += sizeof(u32);
        size += (packet->data.add_movie.release_year ? strlen(packet->data.add_movie.release_year) : 0);
        break;
    case C2S_ADD_GENRE_TO_MOVIE:
        size += sizeof(u32);
        size += sizeof(u32);
        size += (packet->data.add_genre.genre ? strlen(packet->data.add_genre.genre) : 0);
        break;
    case C2S_REMOVE_MOVIE:
    case C2S_GET_MOVIE:
        size += sizeof(u32);
        break;
    case C2S_LIST_MOVIES_BY_GENRE:
        size += sizeof(u32);
        size += (packet->data.list_by_genre.genre ? strlen(packet->data.list_by_genre.genre) : 0);
        break;
    case C2S_LIST_MOVIES:
    case C2S_LIST_MOVIES_DETAILED:
    case C2S_UNKNOWN:
        break;
    default:
        return 0;
    }
    return size;
}

// Função equivalente para o pacote S2C.
static size_t calculate_s2c_packet_size(const S2CPacket *packet) {
    size_t size = sizeof(u8);

    switch (packet->type) {
    case S2C_MOVIE:
        size += sizeof(u32);
        size += sizeof(u32) + (packet->data.movie.title ? strlen(packet->data.movie.title) : 0);
        size += sizeof(u32) + (packet->data.movie.genres ? strlen(packet->data.movie.genres) : 0);
        size += sizeof(u32) + (packet->data.movie.director ? strlen(packet->data.movie.director) : 0);
        size += sizeof(u32) + (packet->data.movie.release_year ? strlen(packet->data.movie.release_year) : 0);
        break;
    case S2C_MOVIE_LIST:
        size += sizeof(u32);
        if(packet->data.movie_list.movies) {
            for (u32 i = 0; i < packet->data.movie_list.count; ++i) {
                size += sizeof(u32);
                size += sizeof(u32) + (packet->data.movie_list.movies[i].title ? strlen(packet->data.movie_list.movies[i].title) : 0);
            }
        }
        break;
    case S2C_MOVIE_LIST_DETAILED:
        size += sizeof(u32);
        if(packet->data.movie_list_detailed.movies) {
            for (u32 i = 0; i < packet->data.movie_list_detailed.count; ++i) {
                const Movie* item = &packet->data.movie_list_detailed.movies[i];
                size += sizeof(u32);
                size += sizeof(u32) + (item->title ? strlen(item->title) : 0);
                size += sizeof(u32) + (item->genres ? strlen(item->genres) : 0);
                size += sizeof(u32) + (item->director ? strlen(item->director) : 0);
                size += sizeof(u32) + (item->release_year ? strlen(item->release_year) : 0);
            }
        }
        break;
    case S2C_ERROR:
        size += sizeof(u32);
        size += (packet->data.error.message ? strlen(packet->data.error.message) : 0);
        break;
    case S2C_UNKNOWN:
    case S2C_OK:
        break;
    default:
        return 0;
    }
    return size;
}

static void serialize_string(const char *str, char **buffer_ptr) {
    u32 len = (str ? (u32)strlen(str) : 0);
    u32 net_len = htonl(len);
    memcpy(*buffer_ptr, &net_len, sizeof(u32));
    *buffer_ptr += sizeof(u32);
    if (len > 0) {
        memcpy(*buffer_ptr, str, len);
        *buffer_ptr += len;
    }
}

static int deserialize_string(int socket_fd, char **str_ptr) {
    u32 net_len, len;
    char *str_buffer = NULL;
    *str_ptr = NULL;

    if (recv_all(socket_fd, &net_len, sizeof(u32)) != 0) return -1;
    len = ntohl(net_len);

    if (len > 0) {
        str_buffer = malloc(len + 1);
        if (!str_buffer) return -1;
        if (recv_all(socket_fd, str_buffer, len) != 0) {
            free(str_buffer);
            return -1;
        }
        str_buffer[len] = '\0';
        *str_ptr = str_buffer;
    }
    return 0;
}

static void serialize_u32(u32 value, char **buffer_ptr) {
    u32 net_val = htonl(value);
    memcpy(*buffer_ptr, &net_val, sizeof(u32));
    *buffer_ptr += sizeof(u32);
}

static int deserialize_u32(int socket_fd, u32 *value_ptr) {
    u32 net_val;
    if (recv_all(socket_fd, &net_val, sizeof(u32)) != 0) return -1;
    *value_ptr = ntohl(net_val);
    return 0;
}


static void serialize_c2s_add_movie(const C2S_AddMovieData* data, char **buffer_ptr) {
    serialize_string(data->title, buffer_ptr);
    serialize_string(data->genres, buffer_ptr);
    serialize_string(data->director, buffer_ptr);
    serialize_string(data->release_year, buffer_ptr);
}

static int deserialize_c2s_add_movie(int socket_fd, C2S_AddMovieData* data) {
    if (deserialize_string(socket_fd, (char**)&data->title) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->genres) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->director) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->release_year) != 0) return -1;
    return 0;
}

static void serialize_c2s_add_genre(const C2S_AddGenreData* data, char **buffer_ptr) {
    serialize_u32(data->movie_id, buffer_ptr);
    serialize_string(data->genre, buffer_ptr);
}

static int deserialize_c2s_add_genre(int socket_fd, C2S_AddGenreData* data) {
    if (deserialize_u32(socket_fd, &data->movie_id) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->genre) != 0) return -1;
    return 0;
}

static void serialize_c2s_movie_id(const C2S_RemoveMovieData* data, char **buffer_ptr) {
    serialize_u32(data->movie_id, buffer_ptr);
}

static int deserialize_c2s_movie_id(int socket_fd, C2S_RemoveMovieData* data) {
    if (deserialize_u32(socket_fd, &data->movie_id) != 0) return -1;
    return 0;
}

static void serialize_c2s_list_by_genre(const C2S_ListByGenreData* data, char **buffer_ptr) {
    serialize_string(data->genre, buffer_ptr);
}

static int deserialize_c2s_list_by_genre(int socket_fd, C2S_ListByGenreData* data) {
    if (deserialize_string(socket_fd, (char**)&data->genre) != 0) return -1;
    return 0;
}

static void serialize_s2c_movie(const Movie* data, char **buffer_ptr) {
    serialize_u32(data->id, buffer_ptr);
    serialize_string(data->title, buffer_ptr);
    serialize_string(data->genres, buffer_ptr);
    serialize_string(data->director, buffer_ptr);
    serialize_string(data->release_year, buffer_ptr);
}

static int deserialize_s2c_movie(int socket_fd, Movie* data) {
    if (deserialize_u32(socket_fd, &data->id) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->title) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->genres) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->director) != 0) return -1;
    if (deserialize_string(socket_fd, (char**)&data->release_year) != 0) return -1;
    return 0;
}

static void serialize_s2c_movie_list(const S2C_MovieListData* data, char **buffer_ptr) {
    serialize_u32(data->count, buffer_ptr);
    if (data->movies) {
        for (u32 i = 0; i < data->count; ++i) {
            serialize_u32(data->movies[i].id, buffer_ptr);
            serialize_string(data->movies[i].title, buffer_ptr);
        }
    }
}

static int deserialize_s2c_movie_list(int socket_fd, S2C_MovieListData* data) {
    if (deserialize_u32(socket_fd, &data->count) != 0) return -1;
    data->movies = NULL;
    if (data->count > 0) {
        data->movies = malloc(data->count * sizeof(S2C_MovieIdTitle));
        if (!data->movies) return -1;
        memset(data->movies, 0, data->count * sizeof(S2C_MovieIdTitle));
        for (u32 i = 0; i < data->count; ++i) {
            if (deserialize_u32(socket_fd, &data->movies[i].id) != 0) return -1;
            if (deserialize_string(socket_fd, (char**)&data->movies[i].title) != 0) return -1;
        }
    }
    return 0;
}


static void serialize_s2c_movie_list_detailed(const S2C_MovieListDetailedData* data, char **buffer_ptr) {
    serialize_u32(data->count, buffer_ptr);
    if (data->movies) {
        for (u32 i = 0; i < data->count; ++i) {
            serialize_s2c_movie(&data->movies[i], buffer_ptr);
        }
    }
}

static int deserialize_s2c_movie_list_detailed(int socket_fd, S2C_MovieListDetailedData* data) {
    if (deserialize_u32(socket_fd, &data->count) != 0) return -1;
    data->movies = NULL;
    if (data->count > 0) {
        data->movies = malloc(data->count * sizeof(Movie));
        if (!data->movies) return -1;
        memset(data->movies, 0, data->count * sizeof(Movie));
        for (u32 i = 0; i < data->count; ++i) {
            if (deserialize_s2c_movie(socket_fd, &data->movies[i]) != 0) return -1;
        }
    }
    return 0;
}

static void serialize_s2c_error(const S2C_ErrorData* data, char **buffer_ptr) {
    serialize_string(data->message, buffer_ptr);
}

static int deserialize_s2c_error(int socket_fd, S2C_ErrorData* data) {
    if (deserialize_string(socket_fd, (char**)&data->message) != 0) return -1;
    return 0;
}

int C2SPacket_send(int socket_fd, const C2SPacket *packet) {
    size_t total_size = calculate_c2s_packet_size(packet);

    char *buffer = (char*)malloc(total_size);
    if (!buffer) return -1;

    char *ptr = buffer;

    memcpy(ptr, &packet->type, sizeof(u8));
    ptr += sizeof(u8);

    switch (packet->type) {
    case C2S_ADD_MOVIE:
        serialize_c2s_add_movie(&packet->data.add_movie, &ptr);
        break;
    case C2S_ADD_GENRE_TO_MOVIE:
        serialize_c2s_add_genre(&packet->data.add_genre, &ptr);
        break;
    case C2S_REMOVE_MOVIE:
    case C2S_GET_MOVIE:
        serialize_c2s_movie_id(&packet->data.remove_movie, &ptr);
        break;
    case C2S_LIST_MOVIES_BY_GENRE:
        serialize_c2s_list_by_genre(&packet->data.list_by_genre, &ptr);
        break;
    case C2S_LIST_MOVIES:
    case C2S_LIST_MOVIES_DETAILED:
    case C2S_UNKNOWN:
        break;
    default:
        free(buffer);
        return -1;
    }

    int result = send_all(socket_fd, buffer, total_size);
    free(buffer);
    return result;
}

int C2SPacket_recv(int socket_fd, C2SPacket *packet) {
    int result = -1;

    if (recv_all(socket_fd, &packet->type, sizeof(u8)) != 0) return -1;

    switch (packet->type) {
    case C2S_ADD_MOVIE:
        result = deserialize_c2s_add_movie(socket_fd, &packet->data.add_movie);
        break;
    case C2S_ADD_GENRE_TO_MOVIE:
        result = deserialize_c2s_add_genre(socket_fd, &packet->data.add_genre);
        break;
    case C2S_REMOVE_MOVIE:
    case C2S_GET_MOVIE:
        result = deserialize_c2s_movie_id(socket_fd, &packet->data.remove_movie);
        break;
    case C2S_LIST_MOVIES_BY_GENRE:
        result = deserialize_c2s_list_by_genre(socket_fd, &packet->data.list_by_genre);
        break;
    case C2S_LIST_MOVIES:
    case C2S_LIST_MOVIES_DETAILED:
    case C2S_UNKNOWN:
        result = 0;
        break;
    default:
        packet->type = C2S_UNKNOWN;
        return -1;
    }

    if (result != 0) {
        goto recv_error;
    }

    return 0;

recv_error:
    C2SPacket_free(packet);
    packet->type = C2S_UNKNOWN;
    return -1;
}

void C2SPacket_free(C2SPacket *packet) {
    if (!packet) return;
    switch (packet->type) {
    case C2S_ADD_MOVIE:
        free(packet->data.add_movie.title);
        free(packet->data.add_movie.genres);
        free(packet->data.add_movie.director);
        free(packet->data.add_movie.release_year);
        packet->data.add_movie.title = NULL;
        packet->data.add_movie.genres = NULL;
        packet->data.add_movie.director = NULL;
        packet->data.add_movie.release_year = NULL;
        break;
    case C2S_ADD_GENRE_TO_MOVIE:
        free(packet->data.add_genre.genre);
        packet->data.add_genre.genre = NULL;
        break;
    case C2S_LIST_MOVIES_BY_GENRE:
        free(packet->data.list_by_genre.genre);
        packet->data.list_by_genre.genre = NULL;
        break;
    case C2S_REMOVE_MOVIE:
    case C2S_LIST_MOVIES:
    case C2S_LIST_MOVIES_DETAILED:
    case C2S_GET_MOVIE:
    case C2S_UNKNOWN:
    default:
        break;
    }
    memset(&packet->data, 0, sizeof(packet->data));
    packet->type = C2S_UNKNOWN;
}


int S2CPacket_send(int socket_fd, const S2CPacket *packet) {
    size_t total_size = calculate_s2c_packet_size(packet);
    if (total_size == sizeof(u8) && packet->type != S2C_UNKNOWN) {
        if (total_size == 0) return -1;
    }

    char *buffer = (char*)malloc(total_size);
    if (!buffer) return -1;

    char *ptr = buffer;

    memcpy(ptr, &packet->type, sizeof(u8));
    ptr += sizeof(u8);

    switch (packet->type) {
    case S2C_MOVIE:
        serialize_s2c_movie(&packet->data.movie, &ptr);
        break;
    case S2C_MOVIE_LIST:
        serialize_s2c_movie_list(&packet->data.movie_list, &ptr);
        break;
    case S2C_MOVIE_LIST_DETAILED:
        serialize_s2c_movie_list_detailed(&packet->data.movie_list_detailed, &ptr);
        break;
    case S2C_ERROR:
        serialize_s2c_error(&packet->data.error, &ptr);
        break;
    case S2C_UNKNOWN:
    case S2C_OK:
        break;
    default:
        free(buffer);
        return -1;
    }

    int result = send_all(socket_fd, buffer, total_size);
    free(buffer);
    return result;
}

int S2CPacket_recv(int socket_fd, S2CPacket *packet) {
    int result = -1;

    if (recv_all(socket_fd, &packet->type, sizeof(u8)) != 0) {
        return -1;
    }

    switch (packet->type) {
    case S2C_MOVIE:
        result = deserialize_s2c_movie(socket_fd, &packet->data.movie);
        break;
    case S2C_MOVIE_LIST:
        result = deserialize_s2c_movie_list(socket_fd, &packet->data.movie_list);
        break;
    case S2C_MOVIE_LIST_DETAILED:
        result = deserialize_s2c_movie_list_detailed(socket_fd, &packet->data.movie_list_detailed);
        break;
    case S2C_ERROR:
        result = deserialize_s2c_error(socket_fd, &packet->data.error);
        break;
    case S2C_UNKNOWN:
    case S2C_OK:
        result = 0;
        break;
    default:
        packet->type = S2C_UNKNOWN;
        return -1;
    }

    if (result != 0) {
        goto s2c_recv_error;
    }

    return 0;

s2c_recv_error:
    S2CPacket_free(packet);
    packet->type = S2C_UNKNOWN;
    return -1;
}

void S2CPacket_free(S2CPacket *packet) {
    if (!packet) return;
    switch (packet->type) {
    case S2C_MOVIE:
        free(packet->data.movie.title);
        free(packet->data.movie.genres);
        free(packet->data.movie.director);
        free(packet->data.movie.release_year);
        packet->data.movie.title = NULL;
        packet->data.movie.genres = NULL;
        packet->data.movie.director = NULL;
        packet->data.movie.release_year = NULL;
        break;
    case S2C_MOVIE_LIST:
        if (packet->data.movie_list.movies) {
            for (u32 i = 0; i < packet->data.movie_list.count; ++i) {
                free(packet->data.movie_list.movies[i].title);
            }
            free(packet->data.movie_list.movies);
            packet->data.movie_list.movies = NULL;
            packet->data.movie_list.count = 0;
        }
        break;
    case S2C_MOVIE_LIST_DETAILED:
        if (packet->data.movie_list_detailed.movies) {
            for (u32 i = 0; i < packet->data.movie_list_detailed.count; ++i) {
                Movie* item = &packet->data.movie_list_detailed.movies[i];
                free(item->title);
                free(item->genres);
                free(item->director);
                free(item->release_year);
            }
            free(packet->data.movie_list_detailed.movies);
            packet->data.movie_list_detailed.movies = NULL;
            packet->data.movie_list_detailed.count = 0;
        }
        break;
    case S2C_ERROR:
        free(packet->data.error.message);
        packet->data.error.message = NULL;
        break;
    case S2C_UNKNOWN:
    default:
        break;
    }
    memset(&packet->data, 0, sizeof(packet->data));
    packet->type = S2C_UNKNOWN;
}
