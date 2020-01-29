CFLAGS = -Wall -g
CC=gcc
LIB = libblank.so 


%.so : %.c
	$(CC) $(CFLAGS) -shared -nostdlib -fPIC $+ -o $@

all: audioserver audioclient ${LIB} 

audioserver: audioserver.o audio.o
	$(CC) -o audioserver audioserver.o audio.o -pthread -lrt

audioclient: audioclient.o audio.o
	$(CC) -o audioclient audioclient.o audio.o -pthread -lrt

audio.o: audio.c audio.h
	$(CC) -c $(CFLAGS) audio.c 

audioserver.o: audioserver.c audio.h
	$(CC) -c $(CFLAGS) audioserver.c 
 
audioclient.o: audioclient.c audio.h
	$(CC) -c $(CFLAGS) audioclient.c 


.PHONY: clean
clean:
	$(RM) audioserver audioclient *.o


