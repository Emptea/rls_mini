
#include "u220_group.h"

#include "mini_config.hpp"
#include "u220.hpp"
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
		piCout << boards_[i]->get_serial() << PICoutManipulators::Hex << fpga_[i]->get_fpga_version();
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

	u->set_play_gpio_tx_enable(0b11);
	u->set_play_gpio_rx_enable(0b11);
	u->set_play_rx_insert_count(1);

	for (size_t channel = 0; channel < 2; ++channel) {
		const size_t ch = i * 2 + channel;

		u->set_tx_gainon_sg(channel, cfg_.tx_gainon_sg[ch]);

		if (cfg_.use_ext_file_sg[ch]) {
			u->set_tx_dac_sg(channel, cfg_.tx_dac_sg[ch]);
		}

		u->set_tx_complex_gain(channel, cfg_.complex_gains[ch]);
	}
}

void U220Group::reset_time(uint32_t value) {
	for (size_t i: active_boards_) {
		fpga_[i]->reset_time(value);
	}
}


void U220Group::sync_ad9361() {
	enable_ad9361_external_ensm();
	generate_ad9361_cat_disable();
	1_s .sleep();
	sync_ad9361_mcs();
	generate_ad9361_cat_enable();
	print_ad9361_ensm_states();
	enable_external_lo_all();
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
	piCout << "AD9361 MCS: stage 1";

	for (size_t i: active_boards_) {
		boards_[i]->mcs_stage1();
	}

	piCout << "AD9361 MCS: SYNC #1";

	generate_ad9361_sync_in();
	1_s .sleep();

	piCout << "AD9361 MCS: stage 2";

	for (size_t i: active_boards_) {
		boards_[i]->mcs_stage2();
	}

	piCout << "AD9361 MCS: SYNC #2";

	generate_ad9361_sync_in();
	1_s .sleep();

	piCout << "AD9361 MCS: finish";

	for (size_t i: active_boards_) {
		boards_[i]->mcs_finish();
	}
}

void U220Group::enable_ad9361_external_ensm() {
	piCout << "AD9361 ENSM: start";

	generate_ad9361_cat_disable();
	1_s .sleep();

	piCout << "BEFORE ALERT:";
	print_ad9361_ensm_states();

	for (size_t i: active_boards_) {
		boards_[i]->ensm_to_alert();
	}

	1_s .sleep();

	piCout << "AFTER SPI ALERT:";
	print_ad9361_ensm_states();

	for (size_t i: active_boards_) {
		boards_[i]->ensm_enable_external();
	}

	1_s .sleep();

	piCout << "AFTER EXTERNAL CONTROL:";
	print_ad9361_ensm_states();

	generate_ad9361_cat_enable();
	1_s .sleep();

	piCout << "AFTER ENABLE = 1:";
	print_ad9361_ensm_states();

	piCout << "AD9361 ENSM: complete";
}

void U220Group::enable_external_lo_all() {
	for (size_t i: active_boards_) {
		boards_[i]->enable_external_lo();
	}
}

void U220Group::print_ad9361_ensm_states() {
	for (size_t i: active_boards_) {
		const uint8_t state = boards_[i]->get_ensm_state();

		piCout << "AD9361[" << i << "] ENSM = 0x" << PICoutManipulators::Hex << static_cast<int>(state);
	}
}

void U220Group::activate_board(size_t i) {
	if (fpga_[i] != nullptr) return;

	boards_[i]->init();
	fpga_[i] = new UserU220(boards_[i]->get_usrp());
	active_boards_.push_back(i);
}
