CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

SRC = $(wildcard src/*.c)
DEPS = $(wildcard src/*.h)
OBJ = $(SRC:.c=.o)

TARGET = cli-space-analyzer

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
