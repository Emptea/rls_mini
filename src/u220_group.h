#pragma once

#include "imitator_u220.h"
#include "rlso_const.hpp"
#include "uhd_time_synchronizer.hpp"
#include "user_reg_logger.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <pivector.h>
#include <thread>

class U220;
class UserU220;
class MiniConfig;

class U220Group {
public:
	explicit U220Group(const MiniConfig & cfg);
	~U220Group();

	U220Group(const U220Group &)             = delete;
	U220Group & operator=(const U220Group &) = delete;

	void add_board(U220 * board);
	void activate_board(size_t i);

	U220 * board(size_t i) const { return boards_[i]; }
	UserU220 * fpga(size_t i) const { return fpga_[i]; }
	const PIVector<size_t> & active_boards() const { return active_boards_; }
	size_t board_count() const { return boards_.size(); }


	void setup();
	void stop_and_drain_rx();
	bool get_play_gpio_tx_enable() const;
	void set_play_gpio_tx_enable(bool enable);
	bool get_play_gain_on_enable() const;
	void set_play_gain_on_enable(bool enable);
	void set_rx_playback_correction(const std::array<std::complex<float>, 8> & coeffs);
	void reset_time(uint32_t value);
	void sync_ad9361();
	std::chrono::steady_clock::time_point to_host_time(double usrp_sec) const { return time_synchronizer_.to_host(usrp_sec); }
	double get_time_drift_ppm() const { return time_synchronizer_.drift_ppm(); }
	void reset_time_synchronizer() { time_synchronizer_.reset(); }

	void start_rx_dump();
	bool begin_rx_dump_cycle();
	void receive_rx_dump();
	bool rx_dump_complete() const { return rx_dump_offset >= RX_DUMP_SAMPLES; }
	const complexs * get_dump_channel(size_t channel) const {
		return channel < dump_channels.size() ? dump_channels[channel].get() : nullptr;
	}

private:
	PIVector<U220 *> boards_;
	PIVector<UserU220 *> fpga_;
	PIVector<size_t> active_boards_;

	std::atomic_bool rx_dump_requested{false};
	bool rx_dump_active   = false; // Только RX-поток
	size_t rx_dump_offset = 0;
	std::atomic_bool rx_dump_busy{false};
	std::thread rx_dump_writer;

	// Retained after stop; index = 2 * board_index + local channel.
	static constexpr size_t RX_DUMP_SAMPLES = (10000000 / (SAMPLES_PER_CYCLE + 2)) * (SAMPLES_PER_CYCLE + 2);
	std::array<std::unique_ptr<complexs[]>, 8> dump_channels;

	const MiniConfig & cfg_;
	UserRegLogger reg_logger_;
	UhdTimeSynchronizer time_synchronizer_;

	void setup_one_board(size_t board_index);

	void generate_ad9361_sync_in();
	void sync_ad9361_mcs();
	void enable_ad9361_external_ensm();
	void enable_external_lo_all();
	void generate_ad9361_cat_enable();
	void generate_ad9361_cat_disable();
	void print_ad9361_ensm_states();

	void save_rx_dump();
};
