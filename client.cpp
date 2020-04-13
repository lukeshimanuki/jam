#include <cstdlib>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>

#include <asio.hpp>
#include <portaudio.h>

using asio::ip::udp;

constexpr size_t buffer_size = 128;
constexpr size_t num_buffers = 256;
size_t lag = 4; // in terms of number of buffers
size_t buffer_idx = 0;
int16_t mic_buffers[num_buffers][buffer_size];
size_t prev_sample_idx = 0;
size_t sample_idx = 0;
udp::endpoint master_endpoint;

asio::io_context io_context;
asio::steady_timer timer(io_context);
udp::socket sock(io_context, udp::endpoint(udp::v4(), 0));

std::mutex lock;

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
			if (err.value() != 0)
				std::cerr << "Error sending udp to " << endpoint.address() << ' ' << endpoint.port() << '\n' << err << '\n';
			//else std::cout << '.' << std::flush;
			//else std::cout << "sending udp to " << endpoint.address() << ' ' << endpoint.port() << '\n';
			//else std::cout << '-' << std::flush;
		}
		udp::endpoint endpoint;
	} send_handler;

	Remote(uint32_t address, uint16_t port) {
		endpoint.address(asio::ip::make_address_v4(address));
		endpoint.port(port);
		send_handler.endpoint = endpoint;
		play_buffer_idx = 1;
		last_buffer_idx = 0;
	}
};

struct pair_hash {
	template <class T1, class T2>
	std::size_t operator() (const std::pair<T1, T2> &pair) const {
		return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
	}
};
std::unordered_map<uint64_t, Remote> remotes;

int sink_cb(
	const void* inputBuffer, void* outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void* userData
) {
	//lock.lock();
	//for (int32_t i = 0; i < buffer_size; ++i)
	//	((int16_t*)outputBuffer)[i] = (i - buffer_size/2) * 200;
	//std::memcpy(outputBuffer, mic_buffers[(buffer_idx + num_buffers - 1) % num_buffers], buffer_size * 2);
	//return 0;

	int16_t* const out = (int16_t*)outputBuffer;
	for (unsigned long frame = 0; frame < framesPerBuffer; ++frame) {
		int32_t value = 0;
		for (auto& remote : remotes) {
			if (remote.second.last_buffer_idx >= remote.second.play_buffer_idx || 
			(remote.second.last_buffer_idx < num_buffers / 4 && remote.second.play_buffer_idx > num_buffers * 3/4))
				value += remote.second.buffers[remote.second.play_buffer_idx][frame];
		}
		out[frame] = value / std::max(1, int(remotes.size()));
	}
	for (auto& remote : remotes) {
		if (remote.second.play_buffer_idx > num_buffers / 2 && remote.second.play_buffer_idx + 8 < remote.second.last_buffer_idx)
			remote.second.play_buffer_idx += 3;
		if (remote.second.last_buffer_idx < remote.second.play_buffer_idx &&
		!(remote.second.play_buffer_idx > num_buffers * 3/4 && remote.second.last_buffer_idx < num_buffers / 4))
			remote.second.play_buffer_idx -= 1;
		remote.second.play_buffer_idx = (remote.second.play_buffer_idx + num_buffers + 1) % num_buffers;
	}

	//lock.unlock();
	return 0;
}

int source_cb(
	const void* inputBuffer, void* outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void* userData
) {
	//lock.lock();
	//std::cout << ((int16_t*)inputBuffer)[0] << '\n';
	std::memcpy(mic_buffers[buffer_idx], inputBuffer, framesPerBuffer * sizeof(int16_t));
	//for (int32_t i = 0; i < buffer_size; ++i)
	//	mic_buffers[buffer_idx][i] = (i - buffer_size/2) * 200;
	mic_buffers[buffer_idx][buffer_size] = buffer_idx;
	for (const auto& remote : remotes)
		sock.async_send_to(asio::buffer(mic_buffers[buffer_idx], (buffer_size + 1) * sizeof(int16_t)), remote.second.endpoint, remote.second.send_handler);
	buffer_idx = (buffer_idx + 1) % num_buffers;
	//lock.unlock();
	return 0;
}

char receive_buffer[1024];
udp::endpoint receive_endpoint;
struct {
	void operator()(
		const asio::error_code& err,
		size_t bytes_transferred
	) {
		//std::cout << "received\n";
		//lock.lock();
		if (err.value() != 0)
			std::cerr << "Error receiving udp from master\n" << err << '\n';

		if (
			receive_endpoint.address() == master_endpoint.address()
			&& receive_endpoint.port() == master_endpoint.port()
		) {
			bool connect = ((int64_t*)receive_buffer)[0] == 1;
			uint32_t address = ((int64_t*)receive_buffer)[1];
			uint16_t port = ((int64_t*)receive_buffer)[2];

			std::cout << (connect ? "connected " : "disconnected ") << address << ' ' << port << '\n';

			const int64_t key = int64_t(address)*100000 + port;
			if (connect) {
				if (remotes.find(key) == remotes.end()) {
					remotes.emplace(key, Remote(address, port));
				}
			} else {
				if (remotes.find(key) != remotes.end())
					remotes.erase(key);
			}
		} else {
			//std::cout << "other\n";
			const int64_t key = int64_t(receive_endpoint.address().to_v4().to_ulong()) * 100000 + receive_endpoint.port();
			const auto remote = remotes.find(key);
			if (remote != remotes.end()) {
				//std::cout << "remote\n";
				//std::cout << "." << std::flush;
				//std::cout << ((int16_t*)receive_buffer)[0] << ' ' << ((int16_t*)receive_buffer)[1] << ' ' << ((int16_t*)receive_buffer)[2] << '\n';
				const size_t buffer_idx = receive_buffer[buffer_size];
				std::memcpy(remote->second.buffers[buffer_idx], receive_buffer, buffer_size * sizeof(int16_t));
				if (buffer_idx > remote->second.last_buffer_idx || buffer_idx < int32_t(remote->second.last_buffer_idx) - num_buffers / 2)
					remote->second.last_buffer_idx = buffer_idx;
			}
		}

		sock.async_receive_from(asio::buffer(receive_buffer, sizeof(receive_buffer)), receive_endpoint, *this);
		//lock.unlock();
	}
} receive_handler;

void master_send_handler(
    const asio::error_code& err,
    std::size_t bytes_transferred
) {
	//lock.lock();
	const size_t num_remotes = remotes.size();
	//lock.unlock();
	if (err.value() != 0)
		std::cerr << "Error sending test udp to master\n" << err << '\n';
	else std::cout << "connected to " << num_remotes << " remotes\n";
}

void ping_master(const asio::error_code& err) {
	if (err.value() != 0)
		std::cerr << "Timer error\n" << err << '\n';

	const char message[] = "imhere";
	sock.async_send_to(asio::buffer(message, std::strlen(message)), master_endpoint, master_send_handler);

	timer.expires_from_now(asio::chrono::milliseconds(500));
	timer.async_wait(ping_master);
}

int main(int argc, char* argv[]) {
	const size_t max_length = 1024;
	const size_t sample_rate = 22050;
	const char default_host[] = "ec2-18-188-147-41.us-east-2.compute.amazonaws.com";
	const char default_port[] = "3141";

	const char* const host = argc >= 2 ? argv[1] : default_host;
	const char* const port = argc >= 3 ? argv[2] : default_port;

	udp::resolver resolver(io_context);
	udp::resolver::results_type master_endpoints = resolver.resolve(udp::v4(), host, port);
	master_endpoint = *master_endpoints.begin();

	PaError err;

	err = Pa_Initialize();
	if(err != paNoError)
		std::cerr << "Error starting portaudio\n";

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

	std::cout << "opened socket on " << sock.local_endpoint().address() << ' ' << sock.local_endpoint().port() << '\n';

	ping_master(asio::error_code());

	sock.async_receive_from(asio::buffer(receive_buffer, sizeof(receive_buffer)), receive_endpoint, receive_handler);

	io_context.run();

	Pa_CloseStream(source);
	Pa_CloseStream(sink);

	return 0;
}

