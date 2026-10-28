#include "user_u220.h"

#include "user_reg_logger.h"

#include <boost/format.hpp>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

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

void UserU220::write_reg(WrReg reg, uint32_t value, bool log) {
	auto user_regs = usrp_->get_user_settings_iface();

	if (!user_regs) {
		throw std::runtime_error("No user settings!");
	}

	const uint32_t addr = reg_addr(reg);
	user_regs->poke32(addr, value);

	if (log && reg_logger_) {
		reg_logger_->log(board_index_, UserRegLogger::Operation::Write, addr, value);
	}
}

uint64_t UserU220::read_reg(RdReg reg, bool log) {
	auto user_regs = usrp_->get_user_settings_iface();

	if (!user_regs) {
		throw std::runtime_error("No user settings!");
	}

	const uint32_t addr  = reg_addr(reg);
	const uint64_t value = user_regs->peek64(addr);

	if (log && reg_logger_) {
		reg_logger_->log(board_index_, UserRegLogger::Operation::Read, addr, value);
	}

	// std::cout << "Read reg 0x" << std::hex << std::setw(4) << std::setfill('0') << addr << ": 0x" << std::setw(16) << std::setfill('0')
	// 		  << value << std::dec << std::endl;

	return value;
}

void UserU220::write_rx_playback_reg(size_t channel, RxPlaybackReg reg, uint32_t value, bool log) {
	const uint32_t offset = static_cast<uint32_t>(reg);
	if (channel >= 2 || offset >= 32 || (offset >= 4 && offset < 8)) throw std::out_of_range("Invalid RX playback register");
	write_reg(static_cast<WrReg>(32 + 32 * channel + offset), value, log);
}

uint64_t UserU220::read_rx_playback_reg(size_t channel, RxPlaybackReg reg, bool log) {
	const uint32_t offset = static_cast<uint32_t>(reg);
	if (channel >= 2 || offset >= 32 || (offset >= 4 && offset < 8) || (offset >= 24 && offset < 28))
		throw std::out_of_range("Invalid RX playback readback slot");

	return read_reg(static_cast<RdReg>(32 + 32 * channel + offset), log);
}

void UserU220::write_play_ctrl() {
	write_reg(WrReg::PLAY_CTRL, to_raw(play_ctrl_));
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
		write_reg(reg, value, false);
	}
}

void UserU220::set_tx_gainon_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples) {
	const WrReg reg = channel == 0 ? WrReg::TX0_GAINON_MEM : WrReg::TX1_GAINON_MEM;
	write_reg(WrReg::MEM_PTR, 0);
	for (uint8_t sample: samples) {
		write_reg(reg, sample, false);
	}
}

void UserU220::set_rx_en_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples) {
	const WrReg reg = channel == 0 ? WrReg::RX0_EN_MEM : WrReg::RX1_EN_MEM;
	write_reg(WrReg::MEM_PTR, 0);
	for (uint8_t sample: samples) {
		write_reg(reg, sample, false);
	}
}

uint32_t UserU220::get_fpga_version() {
	const uint64_t value   = read_reg(RdReg::FPGA_VERSION);
	const uint32_t version = (value & 0xFFFFFF);
	return version;
}

void UserU220::set_tx_complex_gain(size_t channel, const std::complex<short> gain) {
	const WrReg wr_reg   = channel == 0 ? WrReg::TX0_GAIN : WrReg::TX1_GAIN;
	const uint32_t value = static_cast<uint16_t>(gain.real()) | (static_cast<uint32_t>(static_cast<uint16_t>(gain.imag())) << 16);
	write_reg(wr_reg, value);
}

void UserU220::apply_rx_playback() {
	write_reg(WrReg::RX_PLAYBACK_APPLY, 1);
}

void UserU220::pulse_rx_playback_apply() {
	write_reg(WrReg::RX_PLAYBACK_APPLY_PULSE, 0);
}

void UserU220::set_rx_playback_enable(size_t radio, uint8_t mask) {
	if (mask > 0xF) throw std::out_of_range("RX playback enable mask must be 4 bits");

	write_rx_playback_reg(radio, RxPlaybackReg::ENABLE, mask);
}

void UserU220::set_rx_playback_correction(size_t radio, std::complex<int32_t> correction) {
	if (correction.real() < -8388608 || correction.real() > 8388607 || correction.imag() < -8388608 || correction.imag() > 8388607) {
		throw std::out_of_range("RX playback correction must be signed 24-bit Q14");
	}
	write_rx_playback_reg(radio, RxPlaybackReg::CORRECTION_RE, static_cast<uint32_t>(correction.real()) & 0xFFFFFF);
	write_rx_playback_reg(radio, RxPlaybackReg::CORRECTION_IM, static_cast<uint32_t>(correction.imag()) & 0xFFFFFF);
}

void UserU220::set_rx_playback_delay(size_t radio, const std::array<uint32_t, 4> & values) {
	for (size_t k = 0; k < values.size(); ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::DELAY) + k);

		write_rx_playback_reg(radio, reg, values[k]);
	}
}

void UserU220::set_rx_playback_phase(size_t radio, const std::array<uint32_t, 4> & values) {
	for (size_t k = 0; k < values.size(); ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::PHASE) + k);

		write_rx_playback_reg(radio, reg, values[k]);
	}
}

void UserU220::log_rx_playback_phase(size_t radio) {
	for (size_t k = 0; k < 4; ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::PHASE) + k);

		read_rx_playback_reg(radio, reg);
	}
}

void UserU220::set_rx_playback_amplitude(size_t radio, const std::array<uint32_t, 4> & values) {
	for (size_t k = 0; k < values.size(); ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::AMPLITUDE) + k);

		write_rx_playback_reg(radio, reg, values[k]);
	}
}

void UserU220::set_rx_playback_samples(size_t radio, size_t playback, const std::array<std::complex<int16_t>, 928> & samples) {
	if (playback >= 4) throw std::out_of_range("RX playback must be 0..3");

	const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::SAMPLE_MEM) + playback);

	write_rx_playback_reg(radio, RxPlaybackReg::MEM_PTR, 0);

	for (const auto & sample: samples) {
		const uint32_t value = (static_cast<uint32_t>(static_cast<uint16_t>(sample.real())) << 16) | static_cast<uint16_t>(sample.imag());

		write_rx_playback_reg(radio, reg, value, false);
	}
}

void UserU220::set_rx_playback_dn(size_t radio, size_t playback, const std::array<int16_t, 4096> & samples) {
	if (playback >= 4) throw std::out_of_range("RX playback must be 0..3");

	const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::AMPLITUDE2_MEM) + playback);

	write_rx_playback_reg(radio, RxPlaybackReg::MEM_PTR, 0);

	for (int16_t sample: samples)
		write_rx_playback_reg(radio, reg, static_cast<uint16_t>(sample), false);
}

void UserU220::set_rx_playback_dn_offset(size_t radio, const std::array<uint32_t, 4> & values) {
	for (uint32_t value: values) {
		if (value > 0xFFF) throw std::out_of_range("DN offset must be 12 bits");
	}

	for (size_t k = 0; k < values.size(); ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::AMPLITUDE2_OFFSET) + k);

		write_rx_playback_reg(radio, reg, values[k]);
	}
}

void UserU220::set_rx_playback_start_delay(uint16_t delay_radio0, uint16_t delay_radio1) {
	const uint32_t value = static_cast<uint32_t>(delay_radio0) | (static_cast<uint32_t>(delay_radio1) << 16);

	write_reg(WrReg::RX_PLAYBACK_START_DELAY, value);
}

void UserU220::set_rx_playback_dn_counter(uint16_t start, int32_t step) {
	if (start > 0xFFF) throw std::out_of_range("DN counter start must be 12 bits");

	if (step < -524288 || step > 524287) throw std::out_of_range("DN counter step must be signed 20-bit");

	const uint32_t value = ((static_cast<uint32_t>(step) & 0xFFFFF) << 12) | start;

	write_reg(WrReg::RX_PLAYBACK_START, value);
}

uint16_t UserU220::get_rx_playback_dn_counter_start() {
	return static_cast<uint16_t>(read_reg(RdReg::RX_PLAYBACK_START) & 0xFFF);
}

UserU220::RxPlaybackStatus UserU220::get_rx_playback_status(size_t radio, size_t playback) {
	if (playback >= 4) throw std::out_of_range("RX playback must be 0..3");

	const auto reg     = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::SAMPLE_MEM) + playback);

	const uint64_t raw = read_rx_playback_reg(radio, reg, false);

	RxPlaybackStatus status;
	status.sin        = static_cast<int16_t>(static_cast<uint16_t>(raw >> 48));
	status.cos        = static_cast<int16_t>(static_cast<uint16_t>(raw >> 32));
	status.dn_counter = static_cast<uint32_t>((raw >> 2) & 0xFFFFF);
	status.playing    = (raw >> 1) & 1;
	status.waiting    = raw & 1;

	return status;
}
void UserU220::log_rx_playback_status(size_t radio) {
	const auto reg     = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::SAMPLE_MEM));

	const uint64_t raw = read_rx_playback_reg(radio, reg);
	(void)raw;

	// RxPlaybackStatus status;
	// status.sin        = static_cast<int16_t>(static_cast<uint16_t>(raw >> 48));
	// status.cos        = static_cast<int16_t>(static_cast<uint16_t>(raw >> 32));
	// status.dn_counter = static_cast<uint32_t>((raw >> 2) & 0xFFFFF);
	// status.playing    = (raw >> 1) & 1;
	// status.waiting    = raw & 1;
}

void UserU220::log_rx_playback_enable(size_t radio) {
	read_rx_playback_reg(radio, RxPlaybackReg::ENABLE);
}

void UserU220::log_rx_playback_correction(size_t radio) {
	read_rx_playback_reg(radio, RxPlaybackReg::CORRECTION_RE);
	read_rx_playback_reg(radio, RxPlaybackReg::CORRECTION_IM);
}

void UserU220::log_rx_playback_delay(size_t radio) {
	for (size_t k = 0; k < 4; ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::DELAY) + k);

		read_rx_playback_reg(radio, reg);
	}
}

void UserU220::log_rx_playback_amplitude(size_t radio) {
	for (size_t k = 0; k < 4; ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::AMPLITUDE) + k);

		read_rx_playback_reg(radio, reg);
	}
}

void UserU220::log_rx_playback_dn_offset(size_t radio) {
	for (size_t k = 0; k < 4; ++k) {
		const auto reg = static_cast<RxPlaybackReg>(static_cast<uint32_t>(RxPlaybackReg::AMPLITUDE2_OFFSET) + k);

		read_rx_playback_reg(radio, reg);
	}
}

void UserU220::log_rx_playback_dn_counter() {
	read_reg(RdReg::RX_PLAYBACK_START);
}

void UserU220::log_rx_playback_start_delay() {
	read_reg(RdReg::RX_PLAYBACK_START_DELAY);
}
