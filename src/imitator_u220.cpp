#include "imitator_u220.h"

#include "../rls_fpga/rls.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

const double ImitatorU220::ENERGY_CONST =
	std::sqrt(PT * (LAMBDA * LAMBDA) * GAIN_IF * GAIN_SDR * 50.0 / (4.0 * std::pow(std::acos(-1.0), 3)) * G_MAX * (1 << 12) / U_REF);

ImitatorU220::ImitatorU220(uhd::usrp::multi_usrp::sptr usrp): usrp_(std::move(usrp)) {
	if (!usrp_) {
		throw std::invalid_argument("USRP pointer is null");
	}
}

void ImitatorU220::write_reg(WrReg reg, uint32_t value) {
	auto user_regs = usrp_->get_user_settings_iface();

	if (!user_regs) {
		throw std::runtime_error("No user settings!");
	}

	const uint32_t addr = reg_addr(reg);
	user_regs->poke32(addr, value);

	std::cout << "Write reg 0x" << std::hex << std::setw(4) << std::setfill('0') << addr << ": 0x" << std::setw(8) << std::setfill('0')
			  << value << std::dec << std::endl;
}

uint64_t ImitatorU220::read_reg(RdReg reg) {
	auto user_regs = usrp_->get_user_settings_iface();

	if (!user_regs) {
		throw std::runtime_error("No user settings!");
	}

	const uint32_t addr  = reg_addr(reg);
	const uint64_t value = user_regs->peek64(addr);

	std::cout << "Read reg 0x" << std::hex << std::setw(4) << std::setfill('0') << addr << ": 0x" << std::setw(16) << std::setfill('0')
			  << value << std::dec << std::endl;

	return value;
}

uint32_t ImitatorU220::rx_playback_offset(RxPlaybackReg reg, size_t playback) {
	if (playback >= 4) {
		throw std::out_of_range("RX playback must be 0..3");
	}
	return static_cast<uint32_t>(reg) + static_cast<uint32_t>(playback);
}

void ImitatorU220::write_rx_playback_reg(size_t radio, uint32_t offset, uint32_t value) {
	if (radio >= 2 || offset >= 32 || (offset >= 4 && offset < 8)) {
		throw std::out_of_range("Invalid RX playback register");
	}
	write_reg(static_cast<WrReg>(32 + 32 * radio + offset), value);
}

uint64_t ImitatorU220::read_rx_playback_reg(size_t radio, uint32_t offset) {
	if (radio >= 2 || offset >= 32 || (offset >= 4 && offset < 8) || (offset >= 24 && offset < 28)) {
		throw std::out_of_range("Invalid RX playback readback slot");
	}
	return read_reg(static_cast<RdReg>(32 + 32 * radio + offset));
}

void ImitatorU220::set_rx_playback_enable(size_t radio, uint8_t mask) {
	if (mask > 0xF) {
		throw std::out_of_range("RX playback enable mask must be 4 bits");
	}
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::ENABLE), mask);
}

void ImitatorU220::set_rx_playback_correction(size_t radio, std::complex<int32_t> correction) {
	if (correction.real() < -8388608 || correction.real() > 8388607 || correction.imag() < -8388608 || correction.imag() > 8388607) {
		throw std::out_of_range("RX playback correction must be signed 24-bit Q14");
	}
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::CORRECTION_RE), static_cast<uint32_t>(correction.real()) & 0xFFFFFF);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::CORRECTION_IM), static_cast<uint32_t>(correction.imag()) & 0xFFFFFF);
}

void ImitatorU220::set_rx_playback_delay(size_t radio, size_t playback, uint16_t distance_m) {
	const auto offset = rx_playback_offset(RxPlaybackReg::DELAY, playback);
	const double bias = (2.0 * distance_m / LIGHT_SPEED) * usrp_->get_master_clock_rate();
	if (!std::isfinite(bias) || bias < 0 || bias >= 65535.5) {
		throw std::out_of_range("RX playback delay exceeds 16 bits");
	}
	// Use the requested propagation delay directly, without subtracting pipeline latency.
	write_rx_playback_reg(radio, offset, static_cast<uint32_t>(std::llround(bias)));
}

uint32_t ImitatorU220::convert_rx_playback_phase(uint16_t, size_t, uint16_t, int16_t velocity_mps) {
	constexpr double period_s = static_cast<double>(rls::N_SAMPS_IN_PACK) / 5000000.0;
	// phase = -4*pi*velocity*period/c radians; divide by 2*pi to get turns.
	const double phase_turns  = -2.0 * static_cast<double>(velocity_mps) * period_s / LIGHT_SPEED;
	const int64_t phase_units = std::llround(phase_turns * 4294967296.0);
	// Integer conversion wraps negative phase modulo 2^32.
	return static_cast<uint32_t>(phase_units);
}

double ImitatorU220::rx_playback_rcs_from_attenuation(double attenuation_db) {
	if (!std::isfinite(attenuation_db)) {
		throw std::invalid_argument("Attenuation must be finite");
	}
	const double rcs = std::pow(10.0, -attenuation_db / 10.0);
	if (!std::isfinite(rcs)) {
		throw std::out_of_range("Attenuation produces an unrepresentable RCS");
	}
	return rcs;
}

std::complex<int16_t>
ImitatorU220::convert_rx_playback_amplitude(uint16_t distance_m, size_t channel, uint16_t angle, double attenuation_db) {
	if (distance_m == 0) {
		throw std::invalid_argument("Target distance must be nonzero");
	}
	const double pi         = std::acos(-1.0);
	const double um         = static_cast<double>(angle) * (2.0 * pi / 65536.0);
	const double sigma_targ = rx_playback_rcs_from_attenuation(attenuation_db);
	const double dist       = static_cast<double>(distance_m);
	const double magnitude  = ENERGY_CONST * std::sqrt(sigma_targ) * std::cos(um) / (dist * dist);
	const double phase      = (2.0 * pi / LAMBDA) * ((-N_CHANNELS - 1) / 2.0 + static_cast<double>(channel)) * ANT_DIST * std::sin(um);
	const std::complex<double> gain = magnitude * std::exp(std::complex<double>(0.0, phase));
	const auto to_q14               = [](double component) -> int16_t {
        if (!std::isfinite(component)) {
            throw std::out_of_range("RX playback gain must be finite");
        }
        const double scaled = component * 16384.0;
        if (scaled <= -32768.0) return -32768;
        if (scaled >= 32767.0) return 32767;
        return static_cast<int16_t>(std::lround(scaled));
	};
	return {to_q14(gain.real()), to_q14(gain.imag())};
}

void ImitatorU220::set_rx_playback_phase(size_t radio,
                                         size_t playback,
                                         uint16_t distance_m,
                                         size_t channel,
                                         uint16_t angle,
                                         int16_t velocity_mps) {
	const auto offset = rx_playback_offset(RxPlaybackReg::PHASE, playback);
	write_rx_playback_reg(radio, offset, convert_rx_playback_phase(distance_m, channel, angle, velocity_mps));
}

void ImitatorU220::set_rx_playback_amplitude(size_t radio,
                                             size_t playback,
                                             uint16_t distance_m,
                                             size_t channel,
                                             uint16_t angle,
                                             double attenuation_db) {
	const auto offset    = rx_playback_offset(RxPlaybackReg::AMPLITUDE, playback);
	const auto amplitude = convert_rx_playback_amplitude(distance_m, channel, angle, attenuation_db);
	const uint32_t word  = static_cast<uint16_t>(amplitude.real()) | (static_cast<uint32_t>(static_cast<uint16_t>(amplitude.imag())) << 16);
	write_rx_playback_reg(radio, offset, word);
}

void ImitatorU220::set_rx_playback_samples(size_t radio, size_t playback, const std::array<std::complex<int16_t>, 928> & samples) {
	const auto offset = rx_playback_offset(RxPlaybackReg::SAMPLE_MEM, playback);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::MEM_PTR), 0);
	for (const auto & sample: samples) {
		// Sample memory uses I in the high half, Q in the low half.
		const uint32_t word = (static_cast<uint32_t>(static_cast<uint16_t>(sample.real())) << 16) | static_cast<uint16_t>(sample.imag());
		write_rx_playback_reg(radio, offset, word);
	}
}

void ImitatorU220::set_rx_playback_amplitude2(size_t radio, size_t playback, const std::array<int16_t, 4096> & samples) {
	const auto offset = rx_playback_offset(RxPlaybackReg::AMPLITUDE2_MEM, playback);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::MEM_PTR), 0);
	for (int16_t sample: samples) {
		write_rx_playback_reg(radio, offset, static_cast<uint16_t>(sample));
	}
}

void ImitatorU220::set_rx_playback_amplitude2_offset(size_t radio, size_t playback, uint16_t angle) {
	write_rx_playback_reg(radio, rx_playback_offset(RxPlaybackReg::AMPLITUDE2_OFFSET, playback), angle >> 4);
}

void ImitatorU220::set_rx_playback_amplitude2_counter_start(double angle_degrees) {
	if (!std::isfinite(angle_degrees)) {
		throw std::invalid_argument("Counter start angle must be finite");
	}
	double wrapped = std::fmod(angle_degrees, 360.0);
	if (wrapped < 0) {
		wrapped += 360.0;
	}
	const auto n_samp = static_cast<uint32_t>(wrapped * (4096.0 / 360.0)) & 0xFFF;
	write_reg(WrReg::RX_PLAYBACK_START, n_samp);
}

uint16_t ImitatorU220::get_rx_playback_amplitude2_counter_start() {
	return static_cast<uint16_t>(read_reg(RdReg::RX_PLAYBACK_START) & 0xFFF);
}

ImitatorU220::RxPlaybackStatus ImitatorU220::get_rx_playback_status(size_t radio, size_t playback) {
	const auto raw = read_rx_playback_reg(radio, rx_playback_offset(RxPlaybackReg::SAMPLE_MEM, playback));
	return {bool(raw & 1), bool(raw & 2), static_cast<uint16_t>((raw >> 2) & 0xFFF)};
}
