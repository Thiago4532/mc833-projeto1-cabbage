CC = gcc
CFLAGS = -O2 -g -I. -I../common -pthread
LDFLAGS =

SRC = 
SRC += cabbage/server.c
SRC += cabbage/MovieEntry.c
SRC += cabbage/logger.c

OBJ = ${SRC:.c=.o}

COMMON_DIR = ../common

server: cabbage-server

include $(COMMON_DIR)/common.mk

%.o: %.c
	$(CC) -MMD -c -o $@ $< $(CFLAGS)

cabbage-server: $(OBJ) $(COMMON_LIB)
	$(CC) -o cabbage-server $(OBJ) $(COMMON_LIB) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f cabbage/*.o cabbage/*.d
	rm -f cabbage-server

.PHONY: server clean

-include cabbage/*.d
