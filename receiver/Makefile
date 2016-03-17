CC = gcc
CFLAGS = -g -Wall -Wextra -Wno-unused -Werror

all: receiver

SERVER_SOURCES = receiver.c
SERVER_OBJECTS = $(subst .c,.o,$(SERVER_SOURCES))

receiver: $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJECTS)

clean:
	rm -fr *.o *~ *.bak *.tar.gz core *.core *.tmp receiver
