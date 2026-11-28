#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "../rls_fpga/rls.hpp"

class AxuHeaderLogger {
public:
	struct log_entry {
		rls::header hdr;
		rls::compens_formular cf;
	};

	bool start(const std::string & directory = "logs");
	void stop();

	void write(const log_entry & entry);

	uint64_t get_dropped() const { return dropped.load(); }

	~AxuHeaderLogger();


private:
	using Record                           = std::array<uint32_t, sizeof(log_entry) / sizeof (uint32_t)>;

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