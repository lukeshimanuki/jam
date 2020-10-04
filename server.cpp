#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

#include <asio.hpp>

int main(int argc, char* argv[]) {
	const size_t max_length = 1024;
	const unsigned short port = argc >= 2 ? std::atoi(argv[1]) : PORT;

	std::map<std::pair<uint32_t, uint16_t>, std::chrono::system_clock::time_point> remotes;

	using asio::ip::udp;
	asio::io_context io_context;
	udp::socket sock(io_context, udp::endpoint(udp::v4(), port));

	std::cout << "listening on " << port << '\n';
	while (true) {
		char inputdata[max_length];
		udp::endpoint endpoint;
		size_t length = sock.receive_from(asio::buffer(inputdata, max_length), endpoint);
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		const auto key = std::make_pair(endpoint.address().to_v4().to_ulong(), endpoint.port());
		if (std::strncmp(inputdata, "imhere", 6) != 0) {
			std::cout << "\ninvalid ping from " << key.first << ' ' << key.second << '\n';
			continue;
		}
		const auto remote = remotes.find(key);
		const bool is_new_connection = remote == remotes.end();

		// update last known time of remote
		if (!is_new_connection) remote->second = now;

		// remove old remotes
		std::vector<std::pair<uint32_t, uint16_t>> to_remove;
		for (const auto& remote : remotes) {
			if (now - remote.second > std::chrono::seconds(3)) {
				// disconnect
				to_remove.emplace_back(remote.first);
			}
		}
		for (const auto& key_to_remove : to_remove) {
			remotes.erase(key_to_remove);
			// tell existing connections about removed connection
			for (const auto& r : remotes) {
				endpoint.address(asio::ip::make_address_v4(r.first.first));
				endpoint.port(r.first.second);
				const int64_t message[32] = {0, int64_t(key_to_remove.first), key_to_remove.second};
				std::sprintf((char*)&message[3], "\ndisconnected %i %i\n", key_to_remove.first, key_to_remove.second);
				sock.send_to(asio::buffer(message, sizeof(message)), endpoint);
			}
			std::cout << "\ndisconnected " << key_to_remove.first << ' ' << key_to_remove.second << '\n';
		}

		// add new remote
		if (is_new_connection) {
			// connect
			// tell new remote about existing connections
			for (const auto& r : remotes) {
				int64_t message[32] = {1, r.first.first, r.first.second};
				std::snprintf((char*)&message[3], 28, "\nconnected %i %i\n", r.first.first, r.first.second);
				sock.send_to(asio::buffer(message, sizeof(message)), endpoint);
			}
			// tell existing connections about new connection
			for (const auto& r : remotes) {
				endpoint.address(asio::ip::make_address_v4(r.first.first));
				endpoint.port(r.first.second);
				int64_t message[32] = {1, int64_t(key.first), key.second};
				std::snprintf((char*)&message[3], 28, "\nconnected %zu %i\n", key.first, key.second);
				sock.send_to(asio::buffer(message, sizeof(message)), endpoint);
			}
			remotes.emplace(key, now);
			std::cout << "\nconnected " << key.first << ' ' << key.second << '\n';
		}

		std::cout << '.' << std::flush;
	}

	return 0;
}

