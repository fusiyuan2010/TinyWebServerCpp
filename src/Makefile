#!/bin/sh

OPT=-O3
DEBUG=-g
COMPRESSION=-DHTTP_COMPRESSION

CFLAGS=-std=c++11 -Wall -Wextra -pedantic -Wno-format -fPIC $(OPT) $(DEBUG) $(COMPRESSION)
CC=g++
LIBS=-pthread -lboost_system-mt -lz
LIB_PATH=
INCLUDE_PATH=-I./

OBJS=http_server.o main.o zlib_compression.o

all: tws_test libtws.so

tws_test: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS) $(LIB_PATH)
	
libtws.so: http_server.o zlib_compression.o
	$(CC) -shared -o $@ $^ $(LIBS) $(LIB_PATH)

%.o: %.cpp
	$(CC) $(INCLUDE_PATH) -c $(CFLAGS) $(INCLUDE_PATH) -o $@ $^

clean:
	rm -f *.o tws_test libtws.so

rebuild: clean all



