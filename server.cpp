#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include "asio.hpp"

int main(int argc, char* argv[]) {
	const size_t max_length = 1024;
	const unsigned short default_port = 3141;
	const unsigned short port = argc >= 2 ? std::atoi(argv[1]) : default_port;

	std::unordered_map<std::pair<uint32_t, uint16_t>, std::chrono::time_point> remotes;

	using asio::ip::udp;
	asio::io_context io_context;
	udp::socket sock(io_context, udp::endpoint(udp::v4(), port));

	while (true) {
		char inputdata[max_length];
		udp::endpoint endpoint;
		size_t length = sock.receive_from(asio::buffer(data, max_length), endpoint);
		std::time_point now = std::chrono::system_clock::now();
		const auto key = std::make_pair(endpoint.address().to_v4().to_ulong(), endpoint.port());
		const auto remote = remotes.find(key);
		if (remote == remotes.end()) {
			// connect
			for (const auto& r : remotes) {
				endpoint.address(asio::ip::address_v4::make_address_v4(r->first.first));
				endpoint.port(r->first.second);
				const int64_t[] message = {1, key.first, key.second};
				sock.send_to(asio::buffer(message, sizeof(message)), endpoint);
			}
			remotes.emplace(key, now);
			std::cout << "connected " << key.first << ' ' << key.second << '\n';
		} else remote->second = now;

		std::vector<std::pair<uint32_t, uint16_t>> to_remove;
		for (const auto& remote : remotes) {
			if (now - remote->second > std::chrono::seconds(3)) {
				// disconnect
				to_remove.emplace_back(remote->first);
			}
		}
		for (const auto& key : to_remove) {
			remotes.erase(key);
			for (const auto& r : remotes) {
				endpoint.address(asio::ip::address_v4::make_address_v4(r->first.first));
				endpoint.port(r->first.second);
				const int64_t[] message = {0, key.first, key.second};
				sock.send_to(asio::buffer(message, sizeof(message)), endpoint);
			}
			std::cout << "disconnected " << key.first << ' ' << key.second << '\n';
		}
	}

	return 0;
}

