CC=gcc -Wall

PROGS=main fat32 fat32.o

all: $(PROGS)

clean:
	rm -f $(PROGS)

main: main.c fat32.o fat32
	$(CC) main.c -o main fat32.o -lm

fat32: fat32.c fat32.h
	$(CC) -g -c fat32.c