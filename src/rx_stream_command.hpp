#ifndef RX_STREAM_COMMAND_HPP
#define RX_STREAM_COMMAND_HPP

#include "rx_acquisition.hpp"

#include <uhd/types/stream_cmd.hpp>

inline uhd::stream_cmd_t next_rx_stream_command(RxAcquisition & acquisition, const uhd::time_spec_t & start_time, double rate) {
	const uint64_t offset = acquisition.next_offset();
	const uint64_t count  = acquisition.next();
	uhd::stream_cmd_t command(acquisition.has_more() ? uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE
	                                                 : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
	command.num_samps  = count;
	// Multichannel streamers require a timed command for every chunk, including continuations.
	command.stream_now = false;
	command.time_spec  = start_time + uhd::time_spec_t::from_ticks(offset, rate);
	return command;
}

#endif // RX_STREAM_COMMAND_HPP
