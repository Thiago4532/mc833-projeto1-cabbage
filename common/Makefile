CC = clang
CFLAGS = -O2 -I. -g
LDFLAGS =


COMMON_LIB = cabbage/common/Packet.o

common: $(COMMON_LIB)

%.o: %.c
	$(CC) -MMD -c -o $@ $< $(CFLAGS)

clean:
	rm -f cabbage/common/*.o cabbage/common/*.d

.PHONY: common clean

-include cabbage/common/*.d
