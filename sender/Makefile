CC = gcc
CFLAGS = -g -Wall -Wextra -Wno-unused -Werror

all: sender

SERVER_SOURCES = sender.c
SERVER_OBJECTS = $(subst .c,.o,$(SERVER_SOURCES))

sender: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJECTS)

clean:
	rm -fr *.o *~ *.bak *.tar.gz core *.core *.tmp sender
