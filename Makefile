all: client server

client: client.cpp
	g++ -std=c++11 -lpthread -lportaudio client.cpp -o client

server: server.cpp
	g++ -std=c++11 -lpthread server.cpp -o server

clean:
	rm -f server client

