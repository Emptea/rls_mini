#pragma once

#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

class DeviceClock {
public:
	using Clock = std::chrono::steady_clock;

	void update(double device_time, Clock::time_point host_time) {
		if (!std::isfinite(device_time)) return;

		std::lock_guard<std::mutex> lock(mutex_);
		device_time_ = device_time;
		host_time_   = host_time;
		valid_       = true;
	}

	std::optional<double> now() const {
		std::lock_guard<std::mutex> lock(mutex_);
		if (!valid_) return std::nullopt;

		return device_time_ + std::chrono::duration<double>(Clock::now() - host_time_).count();
	}

private:
	mutable std::mutex mutex_;
	double device_time_ = 0.0;
	Clock::time_point host_time_{};
	bool valid_ = false;
};