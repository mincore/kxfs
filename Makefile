
CXXFLAGS=-std=c++11 -g -D_FILE_OFFSET_BITS=64 -Wall 
CXXFLAGS+=-DDEBUG
LDFLAGS=-lprotobuf -lpthread

all: server kxfs

kxfs: net.o client.cpp log.c msg.cpp crc16.c
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS) -lfuse

server: net.o server.cpp backend.cpp task.cpp log.c msg.cpp crc16.c
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS)

clean:
	rm -f kxfs server
