#ifndef RX_ACQUISITION_HPP
#define RX_ACQUISITION_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

class RxAcquisition {
public:
	static uint64_t sample_count(double seconds, double rate) {
		const double samples = seconds * rate;
		// Keep the total exactly representable by the duration calculation.
		if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(rate) || rate <= 0 || !std::isfinite(samples) || samples < 1 ||
		    samples > 9007199254740991.0 || std::abs(samples - std::round(samples)) > 1e-6) {
			throw std::invalid_argument("Acquisition requires a positive duration and a whole sample count per channel");
		}
		return static_cast<uint64_t>(std::llround(samples));
	}

	void reset(uint64_t samples) {
		remaining = samples;
		queued    = 0;
		refill_at = 0;
	}

	uint64_t next() {
		// The legacy RX command has a 28-bit sample count, per channel.
		const uint64_t count = std::min<uint64_t>(remaining, 0x0fffffff);
		refill_at            = queued;
		queued += count;
		remaining -= count;
		return count;
	}

	bool has_more() const { return remaining != 0; }
	uint64_t next_offset() const { return queued; }
	bool needs_refill(uint64_t elapsed_samples) const { return has_more() && elapsed_samples >= refill_at; }

private:
	uint64_t remaining = 0;
	uint64_t queued    = 0;
	uint64_t refill_at = 0;
};

#endif // RX_ACQUISITION_HPP
