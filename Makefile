CXXFLAGS=-std=c++11 -g -D_FILE_OFFSET_BITS=64 -Wall 
CXXFLAGS+=-DDEBUG
LDFLAGS=-lpthread

all: server kxfs

kxfs: client.cpp log.c msg.cpp crc16.c net.o
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS) -lfuse

server: server.cpp backend.cpp task.cpp log.c msg.cpp crc16.c net.o
	g++ $(CXXFLAGS) -o $@  $^ $(LDFLAGS) 

win: server.cpp backend.cpp task.cpp log.c msg.cpp crc16.c net_win.o
	x86_64-w64-mingw32-g++ $(CXXFLAGS) -o server.exe  $^ -lws2_32 

net.o:net.c
	gcc -Wall -c -o $@ $^

net_win.o:net.c
	x86_64-w64-mingw32-gcc -Wall -c -o $@ $^
	
clean:
	rm -f kxfs server *.o server.exe
