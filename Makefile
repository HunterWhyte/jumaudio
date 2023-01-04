PLATFORM = $(shell uname -s)
ARCH = $(shell uname -m)

CC = gcc
CFLAGS = -g -fPIC
CPPFLAGS = -Wall -pedantic -Wextra #-std=gnu90 

.PHONY: clean

B=./build/$(PLATFORM)$(ARCH)

all: $(B) $(B)/libjumaudio.a

$(B):
	mkdir -p $(B)

$(B)/libjumaudio.a: $(B)/jumaudio.o $(B)/pffft.o
	ar rcs $(B)/libjumaudio.a $(B)/jumaudio.o $(B)/pffft.o

$(B)/jumaudio.o: jumaudio.c
	$(CC) -o $(B)/jumaudio.o -c $(CFLAGS) $(CPPFLAGS) jumaudio.c -I.
	
$(B)/pffft.o: pffft/pffft.c
	$(CC) -o $(B)/pffft.o -c $(CFLAGS) $(CPPFLAGS) pffft/pffft.c -I. 

clean:
	rm -rf build
