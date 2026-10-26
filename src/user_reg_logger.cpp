#include "user_reg_logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

UserRegLogger::UserRegLogger() {
	std::filesystem::create_directories("logs");

	const auto now             = std::chrono::system_clock::now();
	const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

	std::tm tm{};
	localtime_r(&now_time, &tm);

	std::ostringstream name;
	name << "logs/ur_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".log";

	file_.open(name.str(), std::ios::out | std::ios::trunc);

	if (!file_) throw std::runtime_error("Cannot open user register log: " + name.str());

	thread_ = std::thread([this] { run(); });
}

UserRegLogger::~UserRegLogger() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
	}

	cv_.notify_one();

	if (thread_.joinable()) thread_.join();
}

void UserRegLogger::log(size_t board, Operation operation, uint32_t addr, uint64_t value) {
	const auto timestamp = std::chrono::system_clock::now();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_) return;
		queue_.push_back({timestamp, board, operation, addr, value});
	}
	cv_.notify_one();
}

void UserRegLogger::run() {
	for (;;) {
		std::deque<Entry> entries;

		{
			std::unique_lock<std::mutex> lock(mutex_);

			cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });

			if (queue_.empty() && stopping_) break;

			entries.swap(queue_);
		}

		for (const auto & entry: entries) {
			const auto us             = std::chrono::duration_cast<std::chrono::microseconds>(entry.timestamp.time_since_epoch()).count();
			const std::time_t seconds = static_cast<std::time_t>(us / 1000000);
			const auto millis         = (us / 1000) % 1000;
			std::tm tm{};
			localtime_r(&seconds, &tm);

			const char operation  = entry.operation == Operation::Read ? 'R' : 'W';
			const int value_width = entry.operation == Operation::Read ? 16 : 8;

			file_ << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << millis << " " << std::dec << entry.board
				  << " " << operation << " addr=0x" << std::hex << std::setw(8) << entry.addr << " value=0x" << std::setw(value_width)
				  << entry.value << std::dec << '\n';
		}
	}

	file_.flush();
}