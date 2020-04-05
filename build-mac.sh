#!/bin/sh

g++ -g -ggdb -std=c++11 -Iasio/asio/include -DASIO_STANDALONE client.cpp -lportaudio -o client

