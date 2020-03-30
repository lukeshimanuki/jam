#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include "asio.hpp"

constexpr size_t buffer_size = 128;
constexpr size_t num_buffers = 256;
size_t lag = 4; // in terms of number of buffers
size_t buffer_idx = 0;
int16_t mic_buffers[num_buffers][buffer_size];
size_t prev_sample_idx = 0;
size_t sample_idx = 0;
udp::endpoint master_endpoint;

struct Remote {
	udp::endpoint endpoint;
	size_t last_buffer_idx;
	size_t play_buffer_idx;
	int16_t buffers[num_buffers][buffer_size];

	struct {
		void operator()(
			const asio::error_code& err,
			size_t bytes_transferred
		) {
			if (err != paNoError)
				std::cerr << "Error sending udp to " << endpoint.address() << '\n';
		}
	} send_handler;

	remote(uint32_t address, uint16_t port) {
		endpoint.address(asio::ip::address_v4::make_address_v4(address));
		endpoint.port(port);
		play_buffer_idx = 1;
		last_buffer_idx = 0;
	}
};

std::unordered_map<std::pair<uint32_t, uint16_t>, Remote> remotes;

int sink_cb(
	const void* inputBuffer, void* outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void* userData
) {
	// prevent unused variable warnings
	void(inputBuffer);
	void(timeInfo);
	void(statusFlags);
	void(userData);

	int16_t* const out = (int16_t*)outputBuffer;
	for (unsigned long frame = 0; frame < framesPerBuffer; ++frame) {
		int32_t value = 0;
		for (remote : remotes.values()) {
			if (remote.last_buffer_idx >= remote.play_buffer_idx || 
			(remote.last_buffer_idx < num_buffers / 4 && remote.play_buffer_idx > num_buffers * 3/4))
				value += remote.buffers[remote.play_buffer_idx][frame];
		}
		out[frame] = value / remotes.size();
	}
	for (remote : remotes.values()) {
		if (remote.play_buffer_idx > num_buffers / 2 && remote.play_buffer_idx + 8 < remote.last_buffer_idx)
			remote.play_buffer_idx += 3;
		if (remote.last_buffer_idx < remote.play_buffer_idx &&
		!(remote.play_buffer_idx > num_buffers * 3/4 && remote.last_buffer_idx < num_buffers / 4))
			remote.play_buffer_idx -= 1;
		remote.play_buffer_idx = (remote.play_buffer_idx + num_buffers + 1) % num_buffers;
	}
	return 0;
}

int source_cb(
	const void* inputBuffer, void* outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void* userData
) {
	std::memcpy(mic_buffers[buffer_idx], inputBuffer, framesPerBuffer * sizeof(int16_t));
	mic_buffers[buffer_idx][buffer_size] = buffer_idx;
	for (const auto& remote : remotes.values())
		socket.async_send_to(asio::buffer(mic_buffers[buffer_idx], (buffer_size + 1) * sizeof(int16_t)), remote.endpoint, &remote.send_handler);
	buffer_idx = (buffer_idx + 1) % num_buffers;
	// prevent unused variable warnings
	void(outputBuffer);
	void(timeInfo);
	void(statusFlags);
	void(userData);
	return 0;
}

struct {
	udp::endpoint endpoint;
	char buffer[1024];

	void operator()(
		const asio::error_code& err,
		size_t bytes_transferred
	) {
		if (err != paNoError)
			std::cerr << "Error receiving udp from master\n";

		if (
			endpoint.address() == master_endpoint.address()
			&& endpoint.port() == master_endpoint.port()
		) {
			bool connect = ((uint64_t*)buffer)[0];
			uint32_t address = ((uint64_t*)buffer)[1];
			uint16_t port = ((uint64_t*)buffer)[2];

			if (connect) {
				if (remotes.find(key) == remotes.end()) {
					remotes.emplace(key, Remote(address, port));
				}
			} else {
				if (remotes.find(key) != remotes.end())
					remotes.erase(key);
			}
		} else {
			const auto key = std::make_pair(endpoint.address().to_v4().to_ulong(), endpoint.port());
			const auto remote = remotes.find(key);
			if (remote != remotes.end()) {
				const size_t buffer_idx = buffer[buffer_size];
				std::memcpy(remote->buffers[buffer_idx], buffer, buffer_size * sizeof(int16_t));
				if (buffer_idx > remote->last_buffer_idx || buffer_idx < int32_t(remote->last_buffer_idx) - num_buffers / 2)
					remote->last_buffer_idx = buffer_idx;
			}
		}

		socket.async_receive_from(asio::buffer(buffer, sizeof(buffer)), endpoint, this);
	}
} receive_handler;

void master_send_handler(
    const asio::error_code& err,
    std::size_t bytes_transferred
) {
	if (err != paNoError) std::cerr << "Error sending test udp to master\n";
}

int main(int argc, char* argv[]) {
	const size_t max_length = 1024;
	const size_t sample_rate = 22050;
	const char default_host[] = "ec2-18-188-147-41.us-east-2.compute.amazonaws.com";
	const char default_port[] = "3141";

	const char* const host = argc >= 2 ? argv[1] : default_host;
	const char* const port = argc >= 3 ? argv[2] : default_port;

	using asio::ip::udp;
	asio::io_context io_context;
	udp::socket socket(io_context, udp::endpoint(udp::v4(), 0));

	udp::resolver resolver(io_context);
	udp::resolver::results_type master_endpoints = resolver.resolve(udp::v4(), host, port);
	master_endpoint = *master_endpoints.begin();

	PaError err;
	PaStream *sink;
	/* Open an audio I/O stream. */
	err = Pa_OpenDefaultStream(
		&sink,
		0,			/* no input channels */
		1,			/* stereo output */
		paInt16,	/* 16 bit int output */
		sample_rate,
		buffer_size,		/* frames per buffer, i.e. the number
						   of sample frames that PortAudio will
						   request from the callback. Many apps
						   may want to use
						   paFramesPerBufferUnspecified, which
						   tells PortAudio to pick the best,
						   possibly changing, buffer size.*/
		sink_cb, /* this is your callback function */
		nullptr/*This is a pointer that will be passed to
				   your callback*/
	);
	if (err != paNoError) {
		std::cerr << "Error opening audio sink\n";
		return 1;
	}

	PaStream *source;
	/* Open an audio I/O stream. */
	err = Pa_OpenDefaultStream(
		&source,
		1,			/* no input channels */
		0,			/* stereo output */
		paInt16,	/* 16 bit int output */
		sample_rate,
		buffer_size,		/* frames per buffer, i.e. the number
						   of sample frames that PortAudio will
						   request from the callback. Many apps
						   may want to use
						   paFramesPerBufferUnspecified, which
						   tells PortAudio to pick the best,
						   possibly changing, buffer size.*/
		source_cb, /* this is your callback function */
		nullptr/*This is a pointer that will be passed to
				   your callback*/
	);
	if (err != paNoError) {
		std::cerr << "Error opening audio source\n";
		return 1;
	}

	err = Pa_StartStream(source);
	if (err != paNoError) {
		std::cerr << "Error starting source stream\n";
		return 1;
	}

	err = Pa_StartStream(sink);
	if (err != paNoError) {
		std::cerr << "Error starting sink stream\n";
		return 1;
	}

	socket.async_receive_from(asio::buffer(receive_handler.buffer, sizeof(receive_handler.buffer)), receive_handler.endpoint, &receive_handler);

	while (true) {
		Pa_Sleep(100);
		const char message[] = "imhere";
		socket.async_send_to(asio::buffer(message, std::strlen(message)), master_receive_handler.endpoint, &master_send_handler);
	}

	return 0;
}

