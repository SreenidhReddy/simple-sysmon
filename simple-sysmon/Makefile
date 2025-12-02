CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lncurses
SRC = src/system_monitor.c
BIN = sysmon

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

clean:
	rm -f $(BIN)
