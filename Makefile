CC=gcc
CFLAGS=-Wall -Werror -I. -O0 -g3
LDFLAGS=
TARGET=test_runner
C_SOURCES=$(wildcard *.c)
C_OBJS=$(C_SOURCES:.c=.o)

all: $(TARGET)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(TARGET): $(C_OBJS)
	$(CC) $(LDFLAGS) -o $@ $<

check: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) *.o

.PHONY: check clean
