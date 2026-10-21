#pragma once

#include "imitator_u220.h"
#include "user_reg_logger.h"

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
	ImitatorU220 * imitator(size_t i) const { return imitators_[i]; }
	const PIVector<size_t> & active_boards() const { return active_boards_; }
	size_t board_count() const { return boards_.size(); }

	void setup();
	void stop_and_drain_rx();
	void reset_time(uint32_t value);
	void sync_ad9361();

private:
	PIVector<U220 *> boards_;
	PIVector<UserU220 *> fpga_;
	PIVector<size_t> active_boards_;
	PIVector<ImitatorU220 *> imitators_;

	const MiniConfig & cfg_;
	UserRegLogger reg_logger_;

	void setup_one_board(size_t board_index);

	void generate_ad9361_sync_in();
	void sync_ad9361_mcs();
	void enable_ad9361_external_ensm();
	void enable_external_lo_all();
	void generate_ad9361_cat_enable();
	void generate_ad9361_cat_disable();
	void print_ad9361_ensm_states();
};