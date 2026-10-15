#include "user_u220.h"
#include "../rls_fpga/rls.hpp"

#include <boost/format.hpp>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#define LIGHT_SPEED (299792458.0)
#define LAMBDA (0.032480222968581) 

#define PT (80.0)
#define GAIN_IF     (3162.2776601683795) // 10^(35/10)
#define GAIN_SDR    (10000000.0) // 10^(70/10)
#define U_REF (1.3)
#define G_MAX (132.02)

#define ENERGY_CONST (std::sqrt(PT * (LAMBDA * LAMBDA) * GAIN_IF * GAIN_SDR * 50.0 / (4 * std::pow(M_PI, 3)) * G_MAX * (1 << 12) / U_REF)) // will add here constant later
#define N_CHANNELS (8)
#define ANT_DIST     (LAMBDA/2) // distanse between antennae elements, will add later

UserU220::UserU220(uhd::usrp::multi_usrp::sptr usrp): usrp_(std::move(usrp)) {
	if (!usrp_) {
		throw std::invalid_argument("USRP pointer is null");
	}
}

uint32_t UserU220::to_raw(const PlayCtrl & ctrl) {
	static_assert(sizeof(PlayCtrl) == sizeof(uint32_t));

	uint32_t raw = 0;
	std::memcpy(&raw, &ctrl, sizeof(raw));
	return raw;
}

void UserU220::write_reg(WrReg reg, uint32_t value) {
	auto user_regs = usrp_->get_user_settings_iface();

	if (!user_regs) {
		throw std::runtime_error("No user settings!");
	}

	const uint32_t addr = reg_addr(reg);
	user_regs->poke32(addr, value);

	std::cout << "Write reg 0x" << std::hex << std::setw(4) << std::setfill('0') << addr << ": 0x" << std::setw(8) << std::setfill('0')
			  << value << std::dec << std::endl;
}

uint64_t UserU220::read_reg(RdReg reg) {
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

void UserU220::write_play_ctrl() {
	write_reg(WrReg::PLAY_CTRL, to_raw(play_ctrl_));
	read_reg(RdReg::PLAY_CTRL);
}

void UserU220::set_play(uint32_t enable, uint32_t trigger_src, uint32_t gain_on_enable, uint32_t rx_enable) {
	play_ctrl_.enable         = enable;
	play_ctrl_.trigger_src    = trigger_src;
	play_ctrl_.gain_on_enable = gain_on_enable;
	play_ctrl_.rx_enable      = rx_enable;

	write_play_ctrl();
}

void UserU220::set_tx_dac_delay(uint32_t delay_ch0, uint32_t delay_ch1) {
	uint32_t word = delay_ch1 << 16 | (delay_ch0 & 0xFFFF);
	write_reg(WrReg::TX_DAC_DELAY, word);
	// read_reg(RdReg::TX_DAC_DELAY);
}

void UserU220::set_tx_gainon_delay(uint32_t delay_ch0, uint32_t delay_ch1) {
	uint32_t word = delay_ch1 << 16 | (delay_ch0 & 0xFFFF);
	write_reg(WrReg::TX_GAINON_DELAY, word);
	// read_reg(RdReg::TX_GAINON_DELAY);
}

void UserU220::set_rx_adc_delay(uint32_t delay_ch0, uint32_t delay_ch1) {
	uint32_t word = delay_ch1 << 16 | (delay_ch0 & 0xFFFF);
	write_reg(WrReg::RX_ADC_DELAY, word);
	// read_reg(RdReg::RX_ADC_DELAY);
}

void UserU220::set_rx_en_delay(uint32_t delay_ch0, uint32_t delay_ch1) {
	uint32_t word = delay_ch1 << 16 | (delay_ch0 & 0xFFFF);
	write_reg(WrReg::RX_EN_DELAY, word);
	// read_reg(RdReg::RX_EN_DELAY);
}

void UserU220::set_play_enable(uint32_t enable) {
	play_ctrl_.enable = enable;
	write_play_ctrl();
}

void UserU220::set_play_trigger_src(uint32_t trigger_src) {
	play_ctrl_.trigger_src = trigger_src;
	write_play_ctrl();
}

void UserU220::set_play_gain_on_enable(uint32_t gain_on_enable) {
	play_ctrl_.gain_on_enable = gain_on_enable;
	write_play_ctrl();
}

void UserU220::set_play_rx_enable(uint32_t rx_enable) {
	play_ctrl_.rx_enable = rx_enable;
	write_play_ctrl();
}

void UserU220::set_play_gpio_tx_enable(uint32_t gpio_enable) {
	play_ctrl_.GPIO_TX_ENABLE = gpio_enable;
	write_play_ctrl();
}

void UserU220::set_play_gpio_rx_enable(uint32_t gpio_enable) {
	play_ctrl_.GPIO_RX_ENABLE = gpio_enable;
	write_play_ctrl();
}

void UserU220::set_play_start_pulse_enable(uint32_t start_pulse_enable) {
	play_ctrl_.start_pulse_enable = start_pulse_enable;
	write_play_ctrl();
}

void UserU220::set_play_start_pulse_40m(uint32_t start_pulse_40m) {
	play_ctrl_.start_pulse_40m = start_pulse_40m;
	write_play_ctrl();
}

void UserU220::set_play_rx_insert_count(uint32_t rx_insert_count) {
	play_ctrl_.rx_insert_count = rx_insert_count;
	write_play_ctrl();
}

void UserU220::reset_time(uint32_t vita_time) {
	write_reg(WrReg::RESET_TIME, vita_time << 16 | 0x1);
	const uint64_t raw = read_reg(RdReg::RESET_TIME);
}

void UserU220::set_play_start_tx() {
	play_ctrl_.enable             = 1;
	play_ctrl_.gain_on_enable     = 1;
	play_ctrl_.start_pulse_enable = 1;
	play_ctrl_.start_pulse_40m    = 1;

	write_play_ctrl();
}

void UserU220::set_play_stop_tx() {
	play_ctrl_.enable             = 0;
	play_ctrl_.gain_on_enable     = 0;
	play_ctrl_.start_pulse_enable = 0;
	play_ctrl_.start_pulse_40m    = 0;

	write_play_ctrl();
}

void UserU220::sync_ad9361_stage1() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 0;
	play_ctrl_.cat_sync_enable    = 0;
	play_ctrl_.start_pulse_40m    = 0;

	write_play_ctrl();
}

void UserU220::sync_ad9361_stage2() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 1;
	play_ctrl_.cat_sync_enable    = 1;
	play_ctrl_.start_pulse_40m    = 1;

	write_play_ctrl();
}

void UserU220::sync_ad9361_master() {
	play_ctrl_.enable = 1;
	write_play_ctrl();
}

void UserU220::sync_ad9361_finish() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 0;
	play_ctrl_.cat_sync_enable    = 0;
	play_ctrl_.start_pulse_40m    = 0;

	write_play_ctrl();
}

void UserU220::enable_ad9361_stage1() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 1;
	play_ctrl_.cat_sync_enable    = 0;
	play_ctrl_.start_pulse_40m    = 1;
	play_ctrl_.cat_enable_we      = 1;
	play_ctrl_.cat_enable_val     = 0;
	play_ctrl_.cat_edge           = 1;

	write_play_ctrl();
}

void UserU220::enable_ad9361_stage2() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 1;
	play_ctrl_.cat_sync_enable    = 0;
	play_ctrl_.start_pulse_40m    = 1;
	play_ctrl_.cat_enable_we      = 1;
	play_ctrl_.cat_enable_val     = 1;
	play_ctrl_.cat_edge           = 1;

	write_play_ctrl();
}

void UserU220::enable_ad9361_master() {
	play_ctrl_.enable = 1;
	write_play_ctrl();
}

void UserU220::enable_ad9361_finish() {
	play_ctrl_.enable             = 0;
	play_ctrl_.start_pulse_enable = 0;
	play_ctrl_.cat_sync_enable    = 0;
	play_ctrl_.start_pulse_40m    = 0;
	play_ctrl_.cat_enable_we      = 0;
	play_ctrl_.cat_enable_val     = 1;

	write_play_ctrl();
}

void UserU220::set_tx_dac_sg(size_t channel, const std::array<std::complex<short>, 232 * 4> & samples) {
	const WrReg reg = channel == 0 ? WrReg::TX0_DAC_MEM : WrReg::TX1_DAC_MEM;
	write_reg(WrReg::MEM_PTR, 0);
	for (const auto & sample: samples) {
		const uint32_t value = static_cast<uint16_t>(sample.real()) | (static_cast<uint32_t>(static_cast<uint16_t>(sample.imag())) << 16);
		write_reg(reg, value);
	}
}

void UserU220::set_tx_gainon_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples) {
	const WrReg reg = channel == 0 ? WrReg::TX0_GAINON_MEM : WrReg::TX1_GAINON_MEM;
	write_reg(WrReg::MEM_PTR, 0);
	for (uint8_t sample: samples) {
		write_reg(reg, sample);
	}
}

void UserU220::set_rx_en_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples) {
	const WrReg reg = channel == 0 ? WrReg::RX0_EN_MEM : WrReg::RX1_EN_MEM;
	write_reg(WrReg::MEM_PTR, 0);
	for (uint8_t sample: samples) {
		write_reg(reg, sample);
	}
}

uint32_t UserU220::get_fpga_version() {
	const uint64_t value   = read_reg(RdReg::FPGA_VERSION);
	const uint32_t version = (value & 0xFFFFFF);
	std::cout << "U220 FPGA version: " << std::hex << version << std::dec << std::endl;
	return version;
}

void UserU220::set_tx_complex_gain(size_t channel, const std::complex<short> gain) {
	const WrReg wr_reg           = channel == 0 ? WrReg::TX0_GAIN : WrReg::TX1_GAIN;
	const RdReg rd_reg           = channel == 0 ? RdReg::TX0_GAIN : RdReg::TX1_GAIN;
	const RdReg rd_reg_effective = channel == 0 ? RdReg::TX0_GAIN_EFFECTIVE : RdReg::TX1_GAIN_EFFECTIVE;

	const uint32_t value = static_cast<uint16_t>(gain.real()) | (static_cast<uint32_t>(static_cast<uint16_t>(gain.imag())) << 16);
	write_reg(wr_reg, value);
	const uint64_t raw     = read_reg(rd_reg);
	const uint64_t raw_eff = read_reg(rd_reg_effective);
}

uint32_t UserU220::rx_playback_offset(RxPlaybackReg reg, size_t playback) {
	if (playback >= 4) {
		throw std::out_of_range("RX playback must be 0..3");
	}
	return static_cast<uint32_t>(reg) + static_cast<uint32_t>(playback);
}

void UserU220::write_rx_playback_reg(size_t radio, uint32_t offset, uint32_t value) {
	if (radio >= 2 || offset >= 32 || (offset >= 4 && offset < 8)) {
		throw std::out_of_range("Invalid RX playback register");
	}
	write_reg(static_cast<WrReg>(32 + 32 * radio + offset), value);
}

uint64_t UserU220::read_rx_playback_reg(size_t radio, uint32_t offset) {
	if (radio >= 2 || offset >= 32 || (offset >= 4 && offset < 8) || (offset >= 24 && offset < 28)) {
		throw std::out_of_range("Invalid RX playback readback slot");
	}
	return read_reg(static_cast<RdReg>(32 + 32 * radio + offset));
}

void UserU220::set_rx_playback_enable(size_t radio, uint8_t mask) {
	if (mask > 0xF) {
		throw std::out_of_range("RX playback enable mask must be 4 bits");
	}
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::ENABLE), mask);
}

void UserU220::set_rx_playback_correction(size_t radio, std::complex<int32_t> correction) {
	if (correction.real() < -8388608 || correction.real() > 8388607 ||
	    correction.imag() < -8388608 || correction.imag() > 8388607) {
		throw std::out_of_range("RX playback correction must be signed 24-bit Q14");
	}
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::CORRECTION_RE),
	                      static_cast<uint32_t>(correction.real()) & 0xFFFFFF);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::CORRECTION_IM),
	                      static_cast<uint32_t>(correction.imag()) & 0xFFFFFF);
}

void UserU220::set_rx_playback_delay(size_t radio, size_t playback, uint16_t distance_m) {
	const auto offset = rx_playback_offset(RxPlaybackReg::DELAY, playback);
	const double bias = (2.0 * distance_m / LIGHT_SPEED) * usrp_->get_master_clock_rate();
	if (!std::isfinite(bias) || bias < 0 || bias >= 65535.5) {
		throw std::out_of_range("RX playback delay exceeds 16 bits");
	}
	// Use the requested propagation delay directly, without subtracting pipeline latency.
	write_rx_playback_reg(radio, offset, static_cast<uint32_t>(std::llround(bias)));
}

uint32_t UserU220::convert_rx_playback_phase(uint16_t, size_t, uint16_t, int16_t velocity_mps) {
	constexpr double period_s = static_cast<double>(rls::N_SAMPS_IN_PACK) / 5'000'000.0;
	// phase = -4*pi*velocity*period/c radians; divide by 2*pi to get turns.
	const double phase_turns = -2.0 * static_cast<double>(velocity_mps) * period_s / LIGHT_SPEED;
	const int64_t phase_units = std::llround(phase_turns * 4294967296.0);
	// Integer conversion wraps negative phase modulo 2^32.
	return static_cast<uint32_t>(phase_units);
}

double UserU220::rx_playback_rcs_from_attenuation(double attenuation_db) {
	if (!std::isfinite(attenuation_db)) {
		throw std::invalid_argument("Attenuation must be finite");
	}
	const double rcs = std::pow(10.0, -attenuation_db / 10.0);
	if (!std::isfinite(rcs)) {
		throw std::out_of_range("Attenuation produces an unrepresentable RCS");
	}
	return rcs;
}

std::complex<int16_t> UserU220::convert_rx_playback_amplitude(uint16_t distance_m, size_t channel,
                                                             uint16_t angle, double attenuation_db) {
	if (distance_m == 0) {
		throw std::invalid_argument("Target distance must be nonzero");
	}
	const double pi = std::acos(-1.0);
	const double um = static_cast<double>(angle) * (2.0 * pi / 65536.0);
	const double sigma_targ = rx_playback_rcs_from_attenuation(attenuation_db);
	const double dist = static_cast<double>(distance_m);
	const double magnitude = ENERGY_CONST * std::sqrt(sigma_targ) * std::cos(um) / (dist * dist);
	const double phase = (2.0 * pi / LAMBDA) *
	                     ((-N_CHANNELS - 1) / 2.0 + static_cast<double>(channel)) * ANT_DIST * std::sin(um);
	const std::complex<double> gain = magnitude * std::exp(std::complex<double>(0.0, phase));
	const auto to_q14 = [](double component) -> int16_t {
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

void UserU220::set_rx_playback_phase(size_t radio, size_t playback, uint16_t distance_m,
                                    size_t channel, uint16_t angle, int16_t velocity_mps) {
	const auto offset = rx_playback_offset(RxPlaybackReg::PHASE, playback);
	write_rx_playback_reg(radio, offset, convert_rx_playback_phase(distance_m, channel, angle, velocity_mps));
}

void UserU220::set_rx_playback_amplitude(size_t radio, size_t playback, uint16_t distance_m,
                                      size_t channel, uint16_t angle, double attenuation_db) {
	const auto offset = rx_playback_offset(RxPlaybackReg::AMPLITUDE, playback);
	const auto amplitude = convert_rx_playback_amplitude(distance_m, channel, angle, attenuation_db);
	const uint32_t word = static_cast<uint16_t>(amplitude.real()) |
	                      (static_cast<uint32_t>(static_cast<uint16_t>(amplitude.imag())) << 16);
	write_rx_playback_reg(radio, offset, word);
}

void UserU220::set_rx_playback_samples(size_t radio, size_t playback,
                                      const std::array<std::complex<int16_t>, 928> & samples) {
	const auto offset = rx_playback_offset(RxPlaybackReg::SAMPLE_MEM, playback);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::MEM_PTR), 0);
	for (const auto & sample : samples) {
		// Sample memory uses I in the high half, Q in the low half.
		const uint32_t word = (static_cast<uint32_t>(static_cast<uint16_t>(sample.real())) << 16) |
		                      static_cast<uint16_t>(sample.imag());
		write_rx_playback_reg(radio, offset, word);
	}
}

void UserU220::set_rx_playback_amplitude2(size_t radio, size_t playback,
                                         const std::array<int16_t, 4096> & samples) {
	const auto offset = rx_playback_offset(RxPlaybackReg::AMPLITUDE2_MEM, playback);
	write_rx_playback_reg(radio, static_cast<uint32_t>(RxPlaybackReg::MEM_PTR), 0);
	for (int16_t sample : samples) {
		write_rx_playback_reg(radio, offset, static_cast<uint16_t>(sample));
	}
}

void UserU220::set_rx_playback_amplitude2_offset(size_t radio, size_t playback, uint16_t angle) {
	write_rx_playback_reg(radio, rx_playback_offset(RxPlaybackReg::AMPLITUDE2_OFFSET, playback), angle >> 4);
}

void UserU220::set_rx_playback_amplitude2_counter_start(double angle_degrees) {
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

uint16_t UserU220::get_rx_playback_amplitude2_counter_start() {
	return static_cast<uint16_t>(read_reg(static_cast<RdReg>(22)) & 0xFFF);
}

UserU220::RxPlaybackStatus UserU220::get_rx_playback_status(size_t radio, size_t playback) {
	const auto raw = read_rx_playback_reg(radio, rx_playback_offset(RxPlaybackReg::SAMPLE_MEM, playback));
	return {bool(raw & 1), bool(raw & 2), static_cast<uint16_t>((raw >> 2) & 0xFFF)};
}
