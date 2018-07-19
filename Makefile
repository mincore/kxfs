
CXXFLAGS=-std=c++11 -g -D_FILE_OFFSET_BITS=64 -Wall 
#CXXFLAGS+=-DDEBUG
LDFLAGS=-lprotobuf -lpthread

all: server kxfs

kxfs: net.o kxfs.pb.cc client.cpp
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS) -lfuse

server: net.o kxfs.pb.cc server.cpp backend.cpp task.cpp
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS)

clean:
	rm -f kxfs server
