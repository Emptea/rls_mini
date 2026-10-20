#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <uhd/usrp/multi_usrp.hpp>

class ImitatorU220 {
public:
	explicit ImitatorU220(uhd::usrp::multi_usrp::sptr usrp);

	// Four targets, both radios. Angles in degrees, distances in metres.
	void set_params(const std::array<double, 4> & target_az,
	                const std::array<double, 4> & target_um,
	                const std::array<double, 4> & target_dist,
	                const std::array<int16_t, 4> & target_vel,
	                const std::array<double, 4> & target_osl,
	                double opu_az);

	// radio: 0..1; playback: 0..3. All accesses use radio 0's user interface.
	enum class RxPlaybackReg : uint32_t {
		ENABLE            = 0,
		MEM_PTR           = 1,
		CORRECTION_RE     = 2,
		CORRECTION_IM     = 3,
		DELAY             = 8,
		PHASE             = 12,
		AMPLITUDE         = 16,
		SAMPLE_MEM        = 20,
		AMPLITUDE2_MEM    = 24,
		AMPLITUDE2_OFFSET = 28,
	};
	struct RxPlaybackStatus {
		bool waiting;
		bool playing;
		uint16_t amplitude2_counter;
	};

	// For per-playback registers, add playback to the enum's offset.
	void write_rx_playback_reg(size_t radio, uint32_t offset, uint32_t value);
	// Memory contents cannot be read back; SAMPLE_MEM slots return status.
	uint64_t read_rx_playback_reg(size_t radio, uint32_t offset);
	// Parameter setters stage values until an apply (both radios together).
	void apply_rx_playback();
	// Sends a pulse only; requires RX_PLAY_APPLY_OUT wired to each board
	// RX_PLAY_APPLY_IN, including this board, to apply across boards.
	void pulse_rx_playback_apply();
	void set_rx_playback_enable(size_t radio, uint8_t mask);
	void set_rx_playback_correction(size_t radio, std::complex<int32_t> correction);
	void set_rx_playback_delay(size_t radio, size_t playback, uint16_t distance_m);
	void set_rx_playback_phase(size_t radio, size_t playback, uint16_t distance_m, size_t channel, uint16_t angle, int16_t velocity_mps);
	// Positive attenuation reduces RCS: 0 dB = 1.0, 10 dB = 0.1.
	static double rx_playback_rcs_from_attenuation(double attenuation_db);
	// distance_m must be nonzero; channel is the n_chan in the gain formula.
	void set_rx_playback_amplitude(size_t radio,
	                               size_t playback,
	                               uint16_t distance_m,
	                               size_t channel,
	                               uint16_t angle,
	                               double attenuation_db = 0.0);
	void set_rx_playback_samples(size_t radio, size_t playback, const std::array<std::complex<int16_t>, 928> & samples);
	void set_rx_playback_amplitude2(size_t radio, size_t playback, const std::array<int16_t, 4096> & samples);
	// angle: 65536 units per revolution; counter start: degrees, wrapped at 360.
	void set_rx_playback_amplitude2_offset(size_t radio, size_t playback, uint16_t angle);
	// Writes start and speed together; next apply reloads all eight counters.
	// Speed: seconds per revolution; zero holds, negative runs backwards.
	// Encoded step: signed 20-bit, 1/256 entry per capture; out of range throws.
	void set_rx_playback_amplitude2_counter_start(double angle_degrees, double speed = 0.0);
	uint16_t get_rx_playback_amplitude2_counter_start();
	RxPlaybackStatus get_rx_playback_status(size_t radio, size_t playback);

private:
	static constexpr double LIGHT_SPEED = 299792458.0;
	static constexpr double LAMBDA      = 0.032480222968581;
	static constexpr double FREQ        = 9.23e9;
	static constexpr double PT          = 80.0;
	static constexpr double GAIN_IF     = 3162.2776601683795; // 10^(35/10)
	static constexpr double GAIN_SDR    = 10000000.0;         // 10^(70/10)
	static constexpr double U_REF       = 1.3;
	static constexpr double G_MAX       = 132.02;
	static constexpr int N_CHANNELS     = 8;
	static constexpr double ANT_DIST    = LAMBDA / 2.0;
	static const double ENERGY_CONST;

	enum class WrReg : uint32_t {
		RX_PLAYBACK_START       = 14,
		RX_PLAYBACK_APPLY       = 18,
		RX_PLAYBACK_APPLY_PULSE = 19
	};
	enum class RdReg : uint32_t {
		RX_PLAYBACK_START = 22
	};
	static constexpr uint32_t reg_addr(WrReg reg) { return static_cast<uint32_t>(reg) << 2; }
	static constexpr uint32_t reg_addr(RdReg reg) { return static_cast<uint32_t>(reg) << 3; }

	uhd::usrp::multi_usrp::sptr usrp_;
	void write_reg(WrReg reg, uint32_t value);
	uint64_t read_reg(RdReg reg);

	static uint32_t rx_playback_offset(RxPlaybackReg reg, size_t playback);
	// Phase: 2^32 units/revolution; currently uses velocity only.
	// Amplitude: signed complex Q14; angle: 65536 units/revolution.
	static uint32_t convert_rx_playback_phase(uint16_t distance_m, size_t channel, uint16_t angle, int16_t velocity_mps);
	static std::complex<int16_t> convert_rx_playback_amplitude(uint16_t distance_m, size_t channel, uint16_t angle, double attenuation_db);
};
