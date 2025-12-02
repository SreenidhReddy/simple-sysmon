CC = gcc
CFLAGS = -Wall -Wextra -O2

SRC = src/main.c src/cpu.c src/memory.c src/disk.c
OBJ = $(SRC:.c=.o)

TARGET = sysmon

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
