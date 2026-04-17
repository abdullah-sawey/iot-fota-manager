CC = gcc
CFLAGS = -Wall -Wextra -std=c99
SRCS = $(wildcard src/*.c)
TARGET = fota_demo

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
