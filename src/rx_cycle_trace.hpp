#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>


struct RxCycleTraceEntry {
	uint64_t iteration      = 0;

	int64_t start_ns        = 0;

	int64_t cycle_us        = 0;
	int64_t cycle_period_us = -1;

	int64_t recv_total_us   = 0;
	int64_t overhead_us     = 0;

	int64_t cpu_us          = 0;
	int64_t off_cpu_us      = 0;

	std::array<int64_t, 4> board_receive_us{};
	std::array<int, 4> board_error{};
};

bool write_rx_cycle_csv(const std::vector<RxCycleTraceEntry> & entries, const std::string & filename);

inline int64_t rx_thread_cpu_time_ns() {
	timespec ts{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return static_cast<int64_t>(ts.tv_sec) * 1000000000ULL + static_cast<int64_t>(ts.tv_nsec);
}

template<size_t N>
class RxCycleTraceBuffer {
public:
	void push(const RxCycleTraceEntry & entry) {
		entries_[write_pos_] = entry;
		write_pos_           = (write_pos_ + 1) % N;

		if (count_ < N) {
			++count_;
		}
	}

	std::vector<RxCycleTraceEntry> snapshot() const {
		std::vector<RxCycleTraceEntry> result;
		result.reserve(count_);
		const size_t start = (write_pos_ + N - count_) % N;
		for (size_t i = 0; i < count_; ++i) {
			result.push_back(entries_[(start + i) % N]);
		}
		return result;
	}

	void clear() {
		write_pos_ = 0;
		count_     = 0;
	}

private:
	std::array<RxCycleTraceEntry, N> entries_{};
	size_t write_pos_ = 0;
	size_t count_     = 0;
};