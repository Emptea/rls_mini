
#include "u220_group.h"

#include "mini_config.hpp"
#include "u220.hpp"
#include "user_reg_logger.h"
#include "user_u220.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <piliterals_time.h>
#include <string>

U220Group::U220Group(const MiniConfig & cfg): cfg_(cfg) {
	for (auto & channel: dump_channels) {
		channel = std::make_unique<complexs[]>(RX_DUMP_SAMPLES);
	}
}

U220Group::~U220Group() {
	if (rx_dump_writer.joinable()) {
		rx_dump_writer.join();
	}
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

bool U220Group::get_play_gpio_tx_enable() const {
	if (active_boards_.isEmpty()) return false;
	for (size_t i: active_boards_) {
		if (!fpga_[i]->get_play_gpio_tx_enable()) return false;
	}
	return true;
}

void U220Group::set_play_gpio_tx_enable(bool enable) {
	for (size_t i: active_boards_) {
		fpga_[i]->set_play_gpio_tx_enable(enable);
	}
}

bool U220Group::get_play_gain_on_enable() const {
	if (active_boards_.isEmpty()) return false;
	for (size_t i: active_boards_) {
		if (!fpga_[i]->get_play_gain_on_enable()) return false;
	}
	return true;
}

void U220Group::set_play_gain_on_enable(bool enable) {
	for (size_t i: active_boards_) {
		fpga_[i]->set_play_gain_on_enable(enable);
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
	u->set_tx_mode(cfg_.tx_mode[ch0], cfg_.tx_mode[ch1]);
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
		u->set_tx_complex_gain(channel, cfg_.complex_tx_gains[ch]);
		u->set_tx_fap_val(channel, {static_cast<int16_t>(cfg_.fap_tx_level), 0});
		u->set_rx_playback_correction(channel, cfg_.complex_imitator_corrections[ch]);
		const uint8_t mask = cfg_.imitator_channels_en[ch] ? enable_mask : uint8_t{0};
		u->set_rx_playback_enable(channel, mask);
	}
	auto imitator_start_delay_ch0 = cfg_.tx_mode[ch0] == 0 ? cfg_.imitator_start_delay[ch0] : cfg_.imitator_start_delay_tx_mode[ch0];
	auto imitator_start_delay_ch1 = cfg_.tx_mode[ch1] == 0 ? cfg_.imitator_start_delay[ch1] : cfg_.imitator_start_delay_tx_mode[ch1];
	u->set_rx_playback_start_delay(imitator_start_delay_ch0, imitator_start_delay_ch1);
}

void U220Group::set_rx_playback_correction(const std::array<std::complex<float>, 8> & coeffs) {
	for (size_t i: active_boards_) {
		for (size_t channel = 0; channel < 2; ++channel) {
			const size_t ch = i * 2 + channel;
			const std::complex<float> correction_float = 1.0f / coeffs[ch];
			const std::complex<int32_t> correction =  {static_cast<int32_t>(std::round((1 << 14) * correction_float.real())),
		                                static_cast<int32_t>(std::round((1 << 14) * correction_float.imag()))};
										
			fpga_[i]->set_rx_playback_correction(channel, correction);
		}
	}
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
	fpga_[i]                = new UserU220(boards_[i]->get_usrp());
	const PIString & serial = boards_[i]->get_serial();
	fpga_[i]->set_reg_logger(&reg_logger_, i);
	if (active_boards_.isEmpty()) {
		boards_[i]->set_time_synchronizer(&time_synchronizer_);
	}
	active_boards_.push_back(i);
}

void U220Group::receive_rx_dump() {
	const size_t remaining = RX_DUMP_SAMPLES - rx_dump_offset;

	if (remaining == 0) return;

	size_t received = 0;
	bool first      = true;

	for (const auto board_index: active_boards_) {
		auto * b             = board(board_index);

		const size_t samples = std::min(remaining, b->get_board_config().rx_spb);

		const size_t n       = b->receive_dump(dump_channels[2 * board_index].get() + rx_dump_offset,
                                         dump_channels[2 * board_index + 1].get() + rx_dump_offset,
                                         samples);

		if (n == 0 || (!first && n != received)) {
			rx_dump_active = false;
			rx_dump_busy.store(false);
			piCout << "RX dump aborted: receive error or sample count mismatch";
			return;
		}

		received = n;
		first    = false;
	}

	rx_dump_offset += received;

	if (rx_dump_offset >= RX_DUMP_SAMPLES) {
		rx_dump_active = false;

		piCout << "RX dump complete, saving";

		rx_dump_writer = std::thread([this]() { save_rx_dump(); });
	}
}

void U220Group::start_rx_dump() {
	if (rx_dump_busy.exchange(true)) {
		piCout << "RX dump already active or saving";
		return;
	}

	rx_dump_requested.store(true);
}

bool U220Group::begin_rx_dump_cycle() {
	if (rx_dump_active) return true;

	if (!rx_dump_requested.exchange(false)) return false;

	if (rx_dump_writer.joinable()) {
		rx_dump_writer.join();
	}

	rx_dump_offset = 0;
	rx_dump_active = true;
	return true;
}

void U220Group::save_rx_dump() {
	const char * filename = "rx_dump.hex";

	std::ofstream file(filename, std::ios::binary | std::ios::trunc);

	if (!file) {
		piCout << "Cannot open RX dump:" << filename;
		rx_dump_busy.store(false);
		return;
	}

	constexpr char hex[] = "0123456789abcdef";

	std::array<char, 65> line{};
	line[64] = '\n';

	static_assert(sizeof(complexs) == 4, "Expected packed sc16 samples");

	for (size_t sample = 0; sample < RX_DUMP_SAMPLES; ++sample) {
		size_t pos = 0;

		for (int channel = 7; channel >= 0; --channel) {
			uint16_t parts[2]{};

			std::memcpy(parts, &dump_channels[channel][sample], sizeof(parts));

			for (int part: {1, 0}) {
				for (int shift = 12; shift >= 0; shift -= 4) {
					line[pos++] = hex[(parts[part] >> shift) & 0xF];
				}
			}
		}

		file.write(line.data(), line.size());

		if (!file) {
			piCout << "Failed writing RX dump:" << filename;
			break;
		}
	}

	file.close();

	if (file) {
		piCout << "RX dump saved:" << filename;
	}

	rx_dump_busy.store(false);
}
