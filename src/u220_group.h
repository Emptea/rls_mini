#pragma once

#include "imitator_u220.h"
#include "rlso_const.hpp"
#include "uhd_time_synchronizer.hpp"
#include "user_reg_logger.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pivector.h>

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

	// Retained after stop; index = 2 * board_index + local channel.
	std::array<VectorComplexS, 8> dump_channels;
	void start_rx_dump();
	void dump_rx_buffers(size_t board_index, int buffer_idx);

	void setup();
	void stop_and_drain_rx();
	bool get_play_gpio_tx_enable() const;
	void set_play_gpio_tx_enable(bool enable);
	void reset_time(uint32_t value);
	void sync_ad9361();
	std::chrono::steady_clock::time_point to_host_time(double usrp_sec) const { return time_synchronizer_.to_host(usrp_sec); }
	double get_time_drift_ppm() const { return time_synchronizer_.drift_ppm(); }
	void reset_time_synchronizer() { time_synchronizer_.reset(); }

private:
	PIVector<U220 *> boards_;
	PIVector<UserU220 *> fpga_;
	PIVector<size_t> active_boards_;

	std::atomic_bool rx_dump_requested{false};
	std::array<size_t, 4> rx_dump_blocks_remaining{};

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
};