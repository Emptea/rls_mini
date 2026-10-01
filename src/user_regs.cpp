#include "user_regs.h"

static union {
	sr_core_play_ctrl play_ctrl;
	uint32_t raw;
} play_ctrl_un = {0};
static union {
	sr_core_tx_delay tx_delay;
	uint32_t raw;
} tx_delay_un = {0};


static void sr_core_set_reg(uhd::usrp::multi_usrp::sptr usrp, uint32_t addr, uint32_t raw) {
	auto user_regs = usrp->get_user_settings_iface();
	if (!user_regs) {
		std::cout << "No user settings!" << std::endl;
		return;
	} else {
		user_regs->poke32(addr, raw);
		std::cout << "Setting reg 0x" << std::hex << std::setw(4) << std::setfill('0') << addr << ": 0x" << std::hex
				  << std::setw(8) << std::setfill('0') << raw << std::endl;
	}
}

static uint64_t sr_core_get_reg(uhd::usrp::multi_usrp::sptr usrp, uint32_t addr) {
	auto user_regs = usrp->get_user_settings_iface();
	if (!user_regs) {
		std::cout << "No user settings!" << std::endl;
		return -1;
	} else {
		return user_regs->peek64(addr);
	}
}

void set_sr_core_play(uhd::usrp::multi_usrp::sptr usrp,
                      uint32_t enable,
                      uint32_t trigger_src,
                      uint32_t gain_on_enable,
                      uint32_t rx_enable) {
	play_ctrl_un.play_ctrl.enable         = enable;
	play_ctrl_un.play_ctrl.trigger_src    = trigger_src;
	play_ctrl_un.play_ctrl.gain_on_enable = gain_on_enable;
	play_ctrl_un.play_ctrl.rx_enable      = rx_enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_tx_delay(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_dac, uint32_t del_pps) {
	tx_delay_un.tx_delay.del_dac = del_dac;
	tx_delay_un.tx_delay.del_pps = del_pps;

	sr_core_set_reg(usrp, SR_CORE_WR_TX_DELAY_ADDR, tx_delay_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_TX_DELAY_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_rx_delay(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_adc) {
	union {
		sr_core_rx_delay rx_delay;
		uint32_t raw;
	} rx_delay_un = {.rx_delay{.del_adc = del_adc}};
	sr_core_set_reg(usrp, SR_CORE_WR_RX_DELAY_ADDR, rx_delay_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_RX_DELAY_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t enable) {
    play_ctrl_un.play_ctrl.enable         = enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_trigger_src(uhd::usrp::multi_usrp::sptr usrp, uint32_t trigger_src) {
	play_ctrl_un.play_ctrl.trigger_src    = trigger_src;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_gain_on_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gain_on_enable) {
	play_ctrl_un.play_ctrl.gain_on_enable = gain_on_enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_rx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t rx_enable) {
	play_ctrl_un.play_ctrl.rx_enable      = rx_enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_gpio_tx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gpio_enable) {
	play_ctrl_un.play_ctrl.GPIO_TX_ENABLE = gpio_enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_play_gpio_rx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gpio_enable) {
	play_ctrl_un.play_ctrl.GPIO_RX_ENABLE = gpio_enable;

	sr_core_set_reg(usrp, SR_CORE_WR_PLAY_CTRL_ADDR, play_ctrl_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_PLAY_CTRL_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_tx_delay_del_dac(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_dac) {
	tx_delay_un.tx_delay.del_dac = del_dac;

	sr_core_set_reg(usrp, SR_CORE_WR_TX_DELAY_ADDR, tx_delay_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_TX_DELAY_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

void set_sr_core_tx_delay_del_pps(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_pps) {
	tx_delay_un.tx_delay.del_pps = del_pps;

	sr_core_set_reg(usrp, SR_CORE_WR_TX_DELAY_ADDR, tx_delay_un.raw);
	uint64_t raw_got = sr_core_get_reg(usrp, SR_CORE_RD_TX_DELAY_ADDR);
	std::cout << boost::format("0x%016X") % raw_got << std::endl;
}

