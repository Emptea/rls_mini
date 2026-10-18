
#pragma once

#include <cstddef>
#include <cstdint>
#include <pivector.h>
#include <vector>

class U220;
class UserU220;
class MiniConfig;

class U220Group {
public:
	U220Group(const PIVector<U220 *> & boards,
	          const PIVector<UserU220 *> & fpga,
	          const PIVector<size_t> & active_boards,
	          const MiniConfig & cfg);

	void setup();
	void sync_ad9361();
	void reset_time(uint32_t value);

private:
	const PIVector<U220 *> & boards_;
	const PIVector<UserU220 *> & fpga_;
	const PIVector<size_t> & active_boards_;
	const MiniConfig & cfg_;

	void setup_one_board(size_t board_index);

	void generate_ad9361_sync_in();
	void sync_ad9361_mcs();
	void enable_ad9361_external_ensm();
	void enable_external_lo_all();
	void generate_ad9361_cat_enable();
	void generate_ad9361_cat_disable();
	void print_ad9361_ensm_states();
};
