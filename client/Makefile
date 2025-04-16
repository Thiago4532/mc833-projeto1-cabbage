CC = gcc
CFLAGS = -O2 -g -I. -I../common
LDFLAGS =

SRC = 
SRC += cabbage/client.c

OBJ = ${SRC:.c=.o}

COMMON_DIR = ../common

client: cabbage-client

include $(COMMON_DIR)/common.mk

%.o: %.c
	$(CC) -MMD -c -o $@ $< $(CFLAGS)

cabbage-client: $(OBJ) $(COMMON_LIB)
	$(CC) -o cabbage-client $(OBJ) $(COMMON_LIB) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f cabbage/*.o cabbage/*.d
	rm -f cabbage-client

.PHONY: client clean

-include cabbage/*.d
