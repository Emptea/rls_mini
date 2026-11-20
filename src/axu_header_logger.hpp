#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace rls {
struct header;
}

class AxuHeaderLogger {
public:
	bool start(const std::string & directory = "logs");
	void stop();

	void write(const rls::header & hdr);

	uint64_t get_dropped() const { return dropped.load(); }

	~AxuHeaderLogger();

private:
	using Record                           = std::array<uint32_t, 6>;

	static constexpr size_t MAX_QUEUE_SIZE = 16384;

	void worker_loop();
	void write_record(const Record & record);

	std::ofstream file;
	std::thread worker;

	std::deque<Record> queue;
	std::mutex mutex;
	std::condition_variable cv;

	bool running  = false;
	bool stopping = false;

	std::atomic<uint64_t> dropped{0};
};