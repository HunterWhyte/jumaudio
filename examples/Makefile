PLATFORM = $(shell uname -s)
ARCH = $(shell uname -m)

CC = gcc
CFLAGS = -g
CPPFLAGS = -Wall -pedantic -Wextra #-std=gnu90 

.PHONY: clean

B=../build/$(PLATFORM)$(ARCH)

all: $(B) simple_example

$(B):
	mkdir -p $(B)

simple_example: $(B)/simple_example.o
	$(CC) -o simple_example $(CFLAGS) $(CPPFLAGS) $(B)/simple_example.o -L$(B) -ljumaudio \
	-L/usr/lib/ -lSDL2main -lSDL2 -lm

$(B)/simple_example.o: simple_example.c
	$(CC) -o $(B)/simple_example.o -c $(CFLAGS) $(CPPFLAGS) simple_example.c -I.. -I/usr/include/SDL2/

clean:
	rm -rf build simple_example
