#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using RxClock = std::chrono::steady_clock;


struct RxTraceEntry {
	uint64_t iteration           = 0;
	int64_t last_pps_time        = 0;

	int64_t start_ns             = 0;
	int64_t end_ns               = 0;

	// Между концом предыдущего recv и началом текущего
	int64_t gap_us               = 0;

	// Существующее измерение: от начала receive() до конца UHD recv()
	// Оставляем прежнюю семантику для сравнения со старыми CSV.
	int64_t recv_us              = 0;

	size_t samples               = 0;
	int error_code               = 0;

	bool has_time_spec           = false;

	double sample_time           = std::numeric_limits<double>::quiet_NaN();
	double last_sample_time      = std::numeric_limits<double>::quiet_NaN();

	double lag_estimate_ms       = std::numeric_limits<double>::quiet_NaN();
	int64_t lag_reference_age_us = -1;

	// Новые измерения внутри U220::receive().
	// Все значения в наносекундах.
	int64_t pre_recv_ns          = 0;
	int64_t uhd_recv_ns          = 0;
	int64_t lag_check_ns         = 0;
	int64_t trace_ns             = 0;
	int64_t stats_ns             = 0;
	int64_t rx_errors_ns         = 0;
	int64_t dump_queue_ns        = 0;
	int64_t rx_queue_ns          = 0;
	int64_t logging_ns           = 0;

	// Полное время receive(), включая обработку после UHD recv().
	int64_t receive_total_ns     = 0;
};

struct BoardRxTraceEntry {
	std::string serial;
	RxTraceEntry trace;
};


template<size_t N>
class RxTraceBuffer {
public:
	void push(const RxTraceEntry & entry) {
		entries_[write_pos_] = entry;
		write_pos_           = (write_pos_ + 1) % N;

		if (count_ < N) ++count_;
	}

	std::vector<RxTraceEntry> snapshot() const {
		std::vector<RxTraceEntry> result;
		result.reserve(count_);

		const size_t start = (write_pos_ + N - count_) % N;

		for (size_t i = 0; i < count_; ++i)
			result.push_back(entries_[(start + i) % N]);

		return result;
	}

	void clear() {
		write_pos_ = 0;
		count_     = 0;
	}

private:
	std::array<RxTraceEntry, N> entries_{};
	size_t write_pos_ = 0;
	size_t count_     = 0;
};

inline void write_rx_trace_csv(std::vector<BoardRxTraceEntry> entries, const std::string & filename) {
	std::ofstream file(filename);

	if (!file.is_open()) {
		return;
	}

	file << "serial;iteration;last_pps_time;start_ns;end_ns;gap_us;recv_us;"
			"samples;error_code;has_time_spec;sample_time;"
			"last_sample_time;lag_estimate_ms;lag_reference_age_us;"
			"pre_recv_us;uhd_recv_us;lag_check_us;trace_us;"
			"stats_us;rx_errors_us;dump_queue_us;rx_queue_us;"
			"logging_us;receive_total_us;other_us\n";

	constexpr double ns_to_us = 0.001;

	for (const auto & item: entries) {
		const auto & e            = item.trace;

		const int64_t measured_ns = e.pre_recv_ns + e.uhd_recv_ns + e.lag_check_ns + e.trace_ns + e.stats_ns + e.rx_errors_ns +
		                            e.dump_queue_ns + e.rx_queue_ns + e.logging_ns;

		const int64_t other_ns = e.receive_total_ns - measured_ns;

		// Существующие поля: сохраняем исходную точность.
		file << std::setprecision(15) << item.serial << ';' << e.iteration << ';' << e.last_pps_time << ';' << e.start_ns << ';' << e.end_ns
			 << ';' << e.gap_us << ';' << e.recv_us << ';' << e.samples << ';' << e.error_code << ';' << (e.has_time_spec ? 1 : 0) << ';'
			 << e.sample_time << ';' << e.last_sample_time << ';' << e.lag_estimate_ms << ';' << e.lag_reference_age_us << ';';

		// Новые измерения: наносекунды -> микросекунды.
		file << std::fixed << std::setprecision(3) << e.pre_recv_ns * ns_to_us << ';' << e.uhd_recv_ns * ns_to_us << ';'
			 << e.lag_check_ns * ns_to_us << ';' << e.trace_ns * ns_to_us << ';' << e.stats_ns * ns_to_us << ';'
			 << e.rx_errors_ns * ns_to_us << ';' << e.dump_queue_ns * ns_to_us << ';' << e.rx_queue_ns * ns_to_us << ';'
			 << e.logging_ns * ns_to_us << ';' << e.receive_total_ns * ns_to_us << ';' << other_ns * ns_to_us << '\n';

		// Сбрасываем fixed, чтобы следующая строка сохраняла
		// исходный формат временных меток и lag.
		file.unsetf(std::ios::floatfield);
	}

	file.flush();
}
