#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

class UserRegLogger {
public:
	explicit UserRegLogger();
	~UserRegLogger();

	UserRegLogger(const UserRegLogger &)             = delete;
	UserRegLogger & operator=(const UserRegLogger &) = delete;

	enum class Operation : uint8_t {
		Read,
		Write
	};

	void log(size_t board, Operation operation, uint32_t addr, uint64_t value);

private:
	struct Entry {
		std::chrono::system_clock::time_point timestamp;
		size_t board;
		Operation operation;
		uint32_t addr;
		uint64_t value;
	};

	void run();

	std::ofstream file_;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<Entry> queue_;
	std::thread thread_;
	bool stopping_ = false;
};