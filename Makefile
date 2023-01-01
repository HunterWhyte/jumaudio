PLATFORM = $(shell uname -s)
ARCH = $(shell uname -m)

CC = gcc
CFLAGS = -g
CPPFLAGS = -Wall -pedantic -Wextra #-std=gnu90 

.PHONY: clean

B=./build/$(PLATFORM)$(ARCH)

all: $(B) simple_example

$(B):
	mkdir -p $(B)

simple_example: $(B)/simple_example.o $(B)/jumaudio.o $(B)/pffft.o
	$(CC) -o simple_example $(CFLAGS) $(CPPFLAGS) $(B)/simple_example.o $(B)/jumaudio.o $(B)/pffft.o \
	-L/usr/lib/ -lSDL2main -lSDL2 -lm

$(B)/simple_example.o: simple_example.c
	$(CC) -o $(B)/simple_example.o -c $(CFLAGS) $(CPPFLAGS) simple_example.c -I. -I/usr/include/SDL2/

$(B)/jumaudio.o: jumaudio.c
	$(CC) -o $(B)/jumaudio.o -c $(CFLAGS) $(CPPFLAGS) jumaudio.c -I.
	
$(B)/pffft.o: pffft/pffft.c
	$(CC) -o $(B)/pffft.o -c $(CFLAGS) $(CPPFLAGS) pffft/pffft.c -I. 

clean:
	rm -rf build simple_example
