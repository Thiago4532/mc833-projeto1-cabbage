#include "MovieEntry.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int MovieEntry_init(MovieEntry* entry) {
    if (entry == NULL) {
        fprintf(stderr, "Erro: Tentativa de inicializar um MovieEntry nulo.\n");
        return -1;
    }

    entry->movie = NULL;

    int status = pthread_mutex_init(&entry->mutex, NULL);
    if (status != 0) {
        errno = status;
        perror("Erro ao inicializar o mutex para MovieEntry");
        return -1;
    }

    return 0;
}

int MovieEntry_lock(MovieEntry* entry) {
    if (entry == NULL) {
        fprintf(stderr, "Erro: Tentativa de bloquear um MovieEntry nulo.\n");
        return -1;
    }

    int status = pthread_mutex_lock(&entry->mutex);
    if (status != 0) {
        errno = status;
        perror("Erro ao bloquear o mutex do MovieEntry");
        return -1;
    }

    return 0;
}

int MovieEntry_unlock(MovieEntry* entry) {
    if (entry == NULL) {
        fprintf(stderr, "Erro: Tentativa de desbloquear um MovieEntry nulo.\n");
        return -1;
    }

    int status = pthread_mutex_unlock(&entry->mutex);
    if (status != 0) {
        errno = status;
        perror("Erro ao desbloquear o mutex do MovieEntry");
        return -1;
    }

    return 0;
}

int MovieEntry_free(MovieEntry* entry) {
    if (entry == NULL) {
        fprintf(stderr, "Erro: Tentativa de destruir um MovieEntry nulo.\n");
        return -1;
    }

    int status = pthread_mutex_destroy(&entry->mutex);
    if (status != 0) {
        errno = status;
        perror("Erro ao destruir o mutex do MovieEntry");
        return -1;
    }

    return 0;
}
