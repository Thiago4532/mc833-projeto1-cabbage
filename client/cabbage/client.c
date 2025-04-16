#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "cabbage/common/Packet.h"

// Implementação de um CLI simples para interagir com o servidor de filmes

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 12345
#define INPUT_BUFFER_SIZE 2048
#define MAX_ARGS 10

void print_movie(const Movie* movie) {
    if (!movie) return;
    printf("  ID: %u\n", movie->id);
    printf("  Title: %s\n", movie->title ? movie->title : "(null)");
    printf("  Genres: %s\n", movie->genres ? movie->genres : "(null)");
    printf("  Director: %s\n", movie->director ? movie->director : "(null)");
    printf("  Year: %s\n", movie->release_year ? movie->release_year : "(null)");
}

void print_s2c_packet(S2CPacket *packet) {
    switch (packet->type) {
        case S2C_MOVIE:
            print_movie(&packet->data.movie);
            break;
        case S2C_MOVIE_LIST:
            printf("Count: %u\n", packet->data.movie_list.count);
            for (u32 i = 0; i < packet->data.movie_list.count; ++i) {
                printf("  %u - %s\n", packet->data.movie_list.movies[i].id,
                       packet->data.movie_list.movies[i].title ? packet->data.movie_list.movies[i].title : "(null)");
            }
            break;
        case S2C_MOVIE_LIST_DETAILED:
            printf("Count: %u\n", packet->data.movie_list_detailed.count);
            for (u32 i = 0; i < packet->data.movie_list_detailed.count; ++i) {
                printf(" Movie %u/%u:\n", i + 1, packet->data.movie_list_detailed.count);
                print_movie(&packet->data.movie_list_detailed.movies[i]);
                if (i < packet->data.movie_list_detailed.count - 1) printf(" -----\n");
            }
            break;
        case S2C_ERROR:
            printf("Error: %s\n", packet->data.error.message ? packet->data.error.message : "(null)");
            break;
        case S2C_OK:
            printf("Success!\n");
            break;
        default:
            printf("Unknown packet (%u)\n", packet->type);
            break;
    }
     fflush(stdout);
}

void print_usage() {
    printf("Usage:\n");
    printf("  add \"<title>\" \"<genres>\" \"<director>\" \"<year>\"\n");
    printf("    Adds a new movie. Use quotes (\") for arguments with spaces.\n");
    printf("    Genres field should be comma-separated (e.g., \"Action,Comedy\").\n");
    printf("  list\n");
    printf("    Lists all movies (ID and Title).\n");
    printf("  listd\n");
    printf("    Lists all movies with details.\n");
    printf("  get <movie_id>\n");
    printf("    Gets details for a specific movie ID.\n");
    printf("  remove <movie_id>\n");
    printf("    Removes a movie by its ID.\n");
    printf("  addgenre <movie_id> <genre>\n");
    printf("    Adds a single genre to a movie. Genre name should not contain spaces/commas.\n");
    printf("  listgenre <genre> | \"<genre with spaces>\"\n");
    printf("    Lists movies matching the genre. Use quotes for genres with spaces.\n");
    printf("  help\n");
    printf("    Displays this help message.\n");
    printf("  quit | exit\n");
    printf("    Disconnects and exits.\n");
}


int parse_command_line(char* input, char** args, int max_args) {
    int argc = 0;
    char* p = input;

    while (*p != '\0' && argc < max_args) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        if (*p == '"') {
            args[argc] = ++p;
            char* end_quote = strchr(p, '"');
            if (end_quote == NULL) {
                 fprintf(stderr, "Error: Unmatched quote in command.\n");
                 return -1;
            }
            *end_quote = '\0';
            p = end_quote + 1;
        } else {
            args[argc] = p;
            while (*p != '\0' && !isspace((unsigned char)*p)) {
                p++;
            }
            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
        argc++;
    }
    return argc;
}


int main(int argc, char* argv[]) {
    const char* server_ip = DEFAULT_IP;
    int server_port = DEFAULT_PORT;

    // Sem ip padrão, pra facilitar a mensagem de erro.
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        return 1;
    }

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = atoi(argv[2]);
    }

    int sockfd;
    struct sockaddr_in serv_addr;
    char input_buffer[INPUT_BUFFER_SIZE];
    int should_exit = 0;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return 1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported\n");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        close(sockfd);
        return 1;
    }

    printf("Connected to server %s:%d\n", server_ip, server_port);
    printf("Enter commands (type 'help' for options, 'quit' or 'exit' to stop):\n");

    while (!should_exit) {
        printf("> ");
        fflush(stdout);

        if (fgets(input_buffer, INPUT_BUFFER_SIZE, stdin) == NULL) {
            if (feof(stdin)) {
                printf("\nEOF detected, exiting.\n");
            } else {
                perror("fgets error");
            }
            should_exit = 1;
            continue;
        }

        input_buffer[strcspn(input_buffer, "\n")] = 0;


        char* args[MAX_ARGS];
        int arg_count = parse_command_line(input_buffer, args, MAX_ARGS);

        if (arg_count <= 0)
            continue;

        if (strcmp(args[0], "quit") == 0 || strcmp(args[0], "exit") == 0) {
            printf("Exiting...\n");
            should_exit = 1;
            continue;
        }

        if (strcmp(args[0], "help") == 0) {
            print_usage();
            continue;
        }

        C2SPacket request_packet;
        memset(&request_packet, 0, sizeof(C2SPacket));
        int valid_command = 1;

        if (strcmp(args[0], "add") == 0 && arg_count == 5) {
            request_packet.type = C2S_ADD_MOVIE;
            request_packet.data.add_movie.title = args[1];
            request_packet.data.add_movie.genres = args[2];
            request_packet.data.add_movie.director = args[3];
            request_packet.data.add_movie.release_year = args[4];
        } else if (strcmp(args[0], "list") == 0 && arg_count == 1) {
            request_packet.type = C2S_LIST_MOVIES;
        } else if (strcmp(args[0], "listd") == 0 && arg_count == 1) {
            request_packet.type = C2S_LIST_MOVIES_DETAILED;
        } else if (strcmp(args[0], "get") == 0 && arg_count == 2) {
            request_packet.type = C2S_GET_MOVIE;
            request_packet.data.get_movie.movie_id = (u32)strtoul(args[1], NULL, 10);
        } else if (strcmp(args[0], "remove") == 0 && arg_count == 2) {
            request_packet.type = C2S_REMOVE_MOVIE;
            request_packet.data.remove_movie.movie_id = (u32)strtoul(args[1], NULL, 10);
        } else if (strcmp(args[0], "addgenre") == 0 && arg_count == 3) {
            request_packet.type = C2S_ADD_GENRE_TO_MOVIE;
            request_packet.data.add_genre.movie_id = (u32)strtoul(args[1], NULL, 10);
            request_packet.data.add_genre.genre = args[2];
        } else if (strcmp(args[0], "listgenre") == 0 && arg_count == 2) {
            request_packet.type = C2S_LIST_MOVIES_BY_GENRE;
            request_packet.data.list_by_genre.genre = args[1];
        } else {
            fprintf(stderr, "Error: Invalid command or incorrect number of arguments. Type 'help' for usage.\n");
            valid_command = 0;
        }

        if (valid_command) {
            if (C2SPacket_send(sockfd, &request_packet) < 0) {
                perror("C2SPacket_send error");
                should_exit = 1;
                continue;
            }

            S2CPacket response_packet;
            memset(&response_packet, 0, sizeof(S2CPacket));
            int bytes_received = S2CPacket_recv(sockfd, &response_packet);

            if (bytes_received < 0) {
                perror("S2CPacket_recv error");
                should_exit = 1;
            } else {
                print_s2c_packet(&response_packet);
                S2CPacket_free(&response_packet);
            }
        }
    }

    close(sockfd);
    printf("Client shut down.\n");
    return 0;
}
