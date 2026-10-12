#include "user_u220.h"

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

uint32_t UserU220::to_raw(const TxDelay & delay) {
	static_assert(sizeof(TxDelay) == sizeof(uint32_t));

	uint32_t raw = 0;
	std::memcpy(&raw, &delay, sizeof(raw));
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

void UserU220::set_tx0_delay(uint32_t dac_delay, uint32_t gainon_delay) {
	uint32_t word = gainon_delay << 16 | (dac_delay & 0xFFFF);
	write_reg(WrReg::TX0_DELAY, word);
	read_reg(RdReg::TX0_DELAY);
}

void UserU220::set_tx1_delay(uint32_t dac_delay, uint32_t gainon_delay) {
	uint32_t word = gainon_delay << 16 | (dac_delay & 0xFFFF);
	write_reg(WrReg::TX1_DELAY, word);
	read_reg(RdReg::TX1_DELAY);
}

void UserU220::set_rx0_delay(uint32_t val) {
	write_reg(WrReg::RX0_DELAY, val & 0xFFFF);
	const uint64_t raw = read_reg(RdReg::RX0_DELAY);
}

void UserU220::set_rx1_delay(uint32_t val) {
	write_reg(WrReg::RX1_DELAY, val & 0xFFFF);
	const uint64_t raw = read_reg(RdReg::RX1_DELAY);
}

void UserU220::set_rx0_en_delay(uint32_t val) {
	write_reg(WrReg::RX0_DELAY, val & 0xFFFF);
	const uint64_t raw = read_reg(RdReg::RX_EN_DELAY);
}

void UserU220::set_rx1_en_delay(uint32_t val) {
	write_reg(WrReg::RX1_DELAY, val & 0xFFFF);
	const uint64_t raw = read_reg(RdReg::RX_EN_DELAY);
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
