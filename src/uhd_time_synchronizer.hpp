#pragma once

#include <chrono>
#include <mutex>

class UhdTimeSynchronizer {
public:
	void reset() {
		std::lock_guard<std::mutex> lock(mutex_);
		ref_usrp_sec_ = 0.0;
		ref_host_     = std::chrono::steady_clock::time_point{};
		drift_        = 0.0;
	}

	// Вызывается после get_time_now().
	// true — измерение принято.
	bool update(std::chrono::steady_clock::time_point host_before, std::chrono::steady_clock::time_point host_after, double usrp_sec) {
		const double read_us = std::chrono::duration<double, std::micro>(host_after - host_before).count();
		const auto host_mid  = host_before + (host_after - host_before) / 2;
		std::lock_guard<std::mutex> lock(mutex_);
		if (ref_usrp_sec_ == 0.0) {
			ref_usrp_sec_ = usrp_sec;
			ref_host_     = host_mid;
			drift_        = 0.0;
			return true;
		}
		if (read_us > 300.0) {
			return false;
		}

		const double dt = usrp_sec - ref_usrp_sec_;
		if (dt <= 0.0) {
			return false;
		}

		constexpr double alpha = 0.05;
		constexpr double beta  = 0.002;

		const auto predicted   = ref_host_ + to_duration((1.0 + drift_) * dt);
		const double residual  = std::chrono::duration<double>(host_mid - predicted).count();
		ref_host_              = predicted + to_duration(alpha * residual);
		drift_ += beta * residual / dt;
		ref_usrp_sec_ = usrp_sec;

		return true;
	}

	// Перевод UHD timestamp -> steady_clock.
	std::chrono::steady_clock::time_point to_host(double usrp_sec) const {
		std::lock_guard<std::mutex> lock(mutex_);
		const double dt = usrp_sec - ref_usrp_sec_;
		return ref_host_ + to_duration((1.0 + drift_) * dt);
	}

	double drift_ppm() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return drift_ * 1e6;
	}

private:
	static std::chrono::steady_clock::duration to_duration(double seconds) {
		return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
	}

	mutable std::mutex mutex_;

	double ref_usrp_sec_ = 0.0;
	std::chrono::steady_clock::time_point ref_host_{};

	double drift_ = 0.0;
};