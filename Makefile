RANLIB=ranlib

SRC=CachingFileSystem.cpp
OBJ=$(SRC:.cpp=.o)

INCS=-I.
CPPFLAGS = -Wall -g -std=c++11 $(INCS)

Caching_File_System = CachingFileSystem
TARGETS = $(Caching_File_System)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex4.tar
TARSRCS=$(SRC) Cache.cpp Cache.h README Makefile


all: $(TARGETS)


CachingFileSystem: CachingFileSystem.cpp Cache.o Cache.h
	g++ $(CPPFLAGS) $< `pkg-config fuse --cflags --libs` -g $(word 2, $^) -o $@

%.o: %.cpp %.h
	g++ $(CPPFLAGS) -g -c $< -o $@

clean:
	rm -f $(TARGETS)$(OBJ) $(TARNAME) *~ *core *.o

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
