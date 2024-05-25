CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

OBJS = proxy.o helpers.o

all: proxy

helpers.o: helpers.c
	$(CC) $(CFLAGS) -c helpers.c

proxy.o: proxy.c  
	$(CC) $(CFLAGS) -c proxy.c

cache.o: cache.c cache.h
	$(CC) $(CFLAGS) -c cache.c

sbuf.o: sbuf.c sbuf.h
	$(CC) $(CFLAGS) -c sbuf.c

proxy: proxy.o cache.o helpers.o sbuf.o
	$(CC) $(CFLAGS) proxy.o cache.o helpers.o sbuf.o -o proxy $(LDFLAGS)

clean:
	rm -f ./*.o ./proxy ./cache
