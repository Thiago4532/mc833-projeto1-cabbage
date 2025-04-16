#ifndef _CABBAGE_PACKET_H
#define _CABBAGE_PACKET_H

#include "cabbage/common/types.h"
#include "Movie.h"

// Essa classe implementa os DTOs de pacotes de comunicação entre o cliente e o servidor.
// A gente separa os pacotes em dois tipos: Client-to-Server (C2S) e Server-to-Client (S2C).
// Usamos 'typed unions' para representar os dados de cada pacote.
//
// Criamos um pacote para cada tipo de operação, o mecanismo de comunicação não é um RPC, onde o cliente
// espera pela resposta diretamente, o cliente recebe individualmente os pacotes do servidor, por isso a separação clara
// entre os dois tipos de pacotes.

// --- Pacotes Client-to-Server (C2S) ---
#define C2S_UNKNOWN             0x00
#define C2S_ADD_MOVIE           0x01
#define C2S_ADD_GENRE_TO_MOVIE  0x02
#define C2S_REMOVE_MOVIE        0x03
#define C2S_LIST_MOVIES         0x04
#define C2S_LIST_MOVIES_DETAILED 0x05
#define C2S_GET_MOVIE           0x06
#define C2S_LIST_MOVIES_BY_GENRE 0x07

// --- Pacotes Server-to-Client (S2C) ---
#define S2C_UNKNOWN             0x00
#define S2C_MOVIE               0x01
#define S2C_MOVIE_LIST          0x02
#define S2C_MOVIE_LIST_DETAILED 0x03
#define S2C_ERROR               0x04
#define S2C_OK                  0x05

typedef struct {
    char* title;
    char* genres;
    char* director;
    char* release_year;
} C2S_AddMovieData;

typedef struct {
    u32 movie_id;
    char* genre;
} C2S_AddGenreData;

typedef struct {
    u32 movie_id;
} C2S_RemoveMovieData;

typedef struct {
    u32 movie_id;
} C2S_GetMovieData;

typedef struct {
    char* genre;
} C2S_ListByGenreData;

typedef union {
    C2S_AddMovieData add_movie;
    C2S_AddGenreData add_genre;
    C2S_RemoveMovieData remove_movie;
    // LIST_MOVIES não precisa de dados específicos
    // LIST_MOVIES_DETAILED não precisa de dados específicos
    C2S_GetMovieData get_movie;
    C2S_ListByGenreData list_by_genre;
} C2SPacketDataUnion;

// Definindo o pacote Client-to-Server (C2S)
typedef struct {
    u8 type;
    C2SPacketDataUnion data;
} C2SPacket;

typedef struct {
    u32 id;
    char* title;
} S2C_MovieIdTitle;

typedef struct {
    u32 count;
    S2C_MovieIdTitle* movies;
} S2C_MovieListData;

typedef struct {
    u32 count;
    Movie* movies;
} S2C_MovieListDetailedData;

typedef struct {
    char* message;
} S2C_ErrorData;

typedef union {
    Movie movie;
    S2C_MovieListData movie_list;
    S2C_MovieListDetailedData movie_list_detailed;
    S2C_ErrorData error;
    // OK não precisa de dados
} S2CPacketDataUnion;

// Definindo o pacote Server-to-Client (S2C)
typedef struct {
    u8 type;
    S2CPacketDataUnion data;
} S2CPacket;

int C2SPacket_recv(int socket_fd, C2SPacket *packet);
int C2SPacket_send(int socket_fd, const C2SPacket *packet);
void C2SPacket_free(C2SPacket *packet);

int S2CPacket_recv(int socket_fd, S2CPacket *packet);
int S2CPacket_send(int socket_fd, const S2CPacket *packet);
void S2CPacket_free(S2CPacket *packet);

#endif // _CABBAGE_PACKET_H
