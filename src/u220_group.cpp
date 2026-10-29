
#include "u220_group.h"

#include "mini_config.hpp"
#include "u220.hpp"
#include "user_reg_logger.h"
#include "user_u220.h"

#include <piliterals_time.h>

U220Group::U220Group(const MiniConfig & cfg): cfg_(cfg) {}

U220Group::~U220Group() {
	for (auto * u: fpga_) {
		delete u;
	}

	for (auto * u: boards_) {
		delete u;
	}
}

void U220Group::setup() {
	for (size_t i: active_boards_) {
		piCout << boards_[i]->get_serial() << "FPGA Version: " << PICoutManipulators::Hex << fpga_[i]->get_fpga_version();
	}

	for (size_t i: active_boards_) {
		setup_one_board(i);
	}
}

void U220Group::stop_and_drain_rx() {
	for (size_t i: active_boards_) {
		boards_[i]->stop_and_drain_rx();
	}
}

void U220Group::add_board(U220 * board) {
	boards_ << board;
	fpga_ << nullptr;
}

void U220Group::setup_one_board(size_t i) {
	auto * u         = fpga_[i];

	const size_t ch0 = i * 2;
	const size_t ch1 = ch0 + 1;

	u->set_tx_dac_delay(cfg_.tx_dac_delay[ch0], cfg_.tx_dac_delay[ch1]);
	u->set_tx_gainon_delay(cfg_.tx_gpio_delay[ch0], cfg_.tx_gpio_delay[ch1]);
	u->set_rx_adc_delay(cfg_.rx_adc_delay[ch0], cfg_.rx_adc_delay[ch1]);
	u->set_rx_en_delay(cfg_.rx_en_delay[ch0], cfg_.rx_en_delay[ch1]);

	u->set_play_gpio_tx_enable(cfg_.tx_gpio_enable);
	u->set_play_gpio_rx_enable(cfg_.rx_gpio_enable);
	u->set_play_rx_insert_count(cfg_.rx_insert_count);

	uint8_t enable_mask = 0;

	for (size_t k = 0; k < 4; ++k) {
		if (cfg_.imitator_target_en[k]) enable_mask |= static_cast<uint8_t>(1u << k);
	}

	for (size_t channel = 0; channel < 2; ++channel) {
		const size_t ch = i * 2 + channel;

		u->set_tx_gainon_sg(channel, cfg_.tx_gainon_sg[ch]);

		if (cfg_.use_ext_file_sg[ch]) {
			u->set_tx_dac_sg(channel, cfg_.tx_dac_sg[ch]);
		}

		u->set_tx_complex_gain(channel, cfg_.complex_gains[ch]);

		// Калибровка имитатора
		u->set_rx_playback_correction(channel, cfg_.complex_corrections[ch]);

		// Включение целей для данного радиоканала
		const uint8_t mask = cfg_.imitator_channels_en[ch] ? enable_mask : uint8_t{0};

		u->set_rx_playback_enable(channel, mask);
	}
	u->set_rx_playback_start_delay(cfg_.imitator_start_delay[ch0], cfg_.imitator_start_delay[ch1]);
}

void U220Group::reset_time(uint32_t value) {
	for (size_t i: active_boards_) {
		fpga_[i]->reset_time(value);
	}
}


void U220Group::sync_ad9361() {
	piCout << "AD9361: Start sync";
	enable_ad9361_external_ensm();
	generate_ad9361_cat_disable();
	1_s .sleep();
	sync_ad9361_mcs();
	generate_ad9361_cat_enable();
	print_ad9361_ensm_states();
	piCout << "AD9361: Complete sync";
	if (cfg_.use_ext_lo) {
		enable_external_lo_all();
		piCout << "AD9361: Use external LO";
	} else {
		piCout << "AD9361: Use internal LO";
	}
}

void U220Group::generate_ad9361_sync_in() {
	for (size_t i: active_boards_) {
		fpga_[i]->sync_ad9361_stage1();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->sync_ad9361_stage2();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->sync_ad9361_master();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->sync_ad9361_finish();
	}
}

void U220Group::generate_ad9361_cat_enable() {
	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_stage1();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_stage2();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_master();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_finish();
	}
}

void U220Group::generate_ad9361_cat_disable() {
	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_stage1();
	}

	for (size_t i: active_boards_) {
		fpga_[i]->enable_ad9361_master();
	}
}

void U220Group::sync_ad9361_mcs() {
	for (size_t i: active_boards_) {
		boards_[i]->mcs_stage1();
	}
	generate_ad9361_sync_in();
	1_s .sleep();
	for (size_t i: active_boards_) {
		boards_[i]->mcs_stage2();
	}
	generate_ad9361_sync_in();
	1_s .sleep();
	for (size_t i: active_boards_) {
		boards_[i]->mcs_finish();
	}
}

void U220Group::enable_ad9361_external_ensm() {
	generate_ad9361_cat_disable();
	1_s .sleep();
	// print_ad9361_ensm_states();

	for (size_t i: active_boards_) {
		boards_[i]->ensm_to_alert();
	}

	1_s .sleep();

	// print_ad9361_ensm_states();

	for (size_t i: active_boards_) {
		boards_[i]->ensm_enable_external();
	}

	1_s .sleep();

	// print_ad9361_ensm_states();

	generate_ad9361_cat_enable();
	1_s .sleep();

	// print_ad9361_ensm_states();
}

void U220Group::enable_external_lo_all() {
	for (size_t i: active_boards_) {
		boards_[i]->enable_external_lo();
	}
}

void U220Group::print_ad9361_ensm_states() {
	for (size_t i: active_boards_) {
		const uint8_t state = boards_[i]->get_ensm_state();
	}
}

void U220Group::activate_board(size_t i) {
	if (fpga_[i] != nullptr) return;

	boards_[i]->init();
	fpga_[i]                  = new UserU220(boards_[i]->get_usrp());
	const PIString & serial   = boards_[i]->get_serial();
	const size_t board_number = static_cast<size_t>(std::stoul(serial.mid(4).data()));
	fpga_[i]->set_reg_logger(&reg_logger_, i);
	active_boards_.push_back(i);
}
