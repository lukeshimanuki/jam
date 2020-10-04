FROM ubuntu:18.04
MAINTAINER lukeshimanuki
RUN apt-get update \
	&& apt-get --yes --no-install-recommends install cmake make g++ \
	&& apt-get clean
COPY . /jam
RUN cd /jam && mkdir -p build && cd build && cmake .. -DPORT=3141 && make
EXPOSE 3141/udp
CMD ["/jam/build/jamserver", "3141"]

