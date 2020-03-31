FROM ubuntu:18.04
MAINTAINER lukeshimanuki
RUN apt-get update \
	&& apt-get --yes --no-install-recommends install make g++ \
	&& apt-get clean
COPY . /jam
RUN cd /jam && make server
EXPOSE 3141/udp
CMD ["/jam/server", "3141"]

