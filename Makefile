all: client server

client: client.cpp
	g++ -std=c++11 -Iasio/asio/include -DASIO_STANDALONE client.cpp -lpthread -lportaudio -o client

server: server.cpp
	g++ -std=c++11 -Iasio/asio/include -DASIO_STANDALONE server.cpp -lpthread -o server

clean:
	rm -f server client

