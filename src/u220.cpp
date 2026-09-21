#include "u220.hpp"

#include "misc.h"
#include "rx_stream_command.hpp"

#include <cmath>
#include <cstdint>
#include <pistring_std.h>
#include <stdexcept>

// clang-format off
void print_config(const u220_config_t& config) {
    std::cout << boost::format("       === U220 Configuration ===\n"
                              "       Sample Rate:      %5.3f MHz\n"
                              "       Center Freq:      %7.3f MHz\n"
                              "       TX Gain:          [%4.1f, %4.1f] dB\n"
                              "       RX Gain:          [%4.1f, %4.1f] dB\n"
                              "       TX BW:            [%3.1f, %3.1f] MHz\n"
                              "       RX BW:            [%3.1f, %3.1f] MHz\n"
                              "       Ref Clock:        %s\n"
                              "       OTW Format:       %s\n"
                              "       PPS Source:       %s\n"
                              "       TX Samples/Frame: %-6zu\n"
                              "       RX Samples/Frame: %-6zu\n")
        % (config.rate    / 1e6)
        % (config.freq    / 1e6)
        % config.tx_gain[0] % config.tx_gain[1]
        % config.rx_gain[0] % config.rx_gain[1]
        % (config.tx_bw[0] / 1e6) % (config.tx_bw[1] / 1e6)
        % (config.rx_bw[0] / 1e6) % (config.rx_bw[1] / 1e6)
        % config.ref
        % config.otw_format
        % config.pps
        % config.tx_spb
        % config.rx_spb << std::endl;
}
// clang-format on


static VectorComplexS init_wavetable() {
	VectorComplexS result;

	result.append(wave_table_far_sc16);
	result.resize(result.size() + SAMPLES_WAIT_AFTER_FAR, {0, 0});
	result.append(wave_table_close_sc16);
	result.resize(result.size() + SAMPLES_WAIT_AFTER_CLOSE, {0, 0});

	piCout << "Generated waveform of " << result.size() << "samples.";
	return result;
}

void U220::fill_buffer_with_wavetable(VectorComplexS & buffer) {
	static const VectorComplexS wave_table = init_wavetable();

	if (wave_table.isEmpty()) {
		throw std::invalid_argument("Wave table cannot be empty");
	}

	size_t table_size = wave_table.size();

	for (size_t i = 0; i < buffer.size(); ++i) {
		buffer[i] = wave_table[i % table_size];
	}
}


U220::U220(const PIString & serial, const PIString & args, uint64_t num_samps, u220_config_t config)
	: serial(serial)
	, device_args(args)
	, user_config(config)
	, rx_stream_cmd((num_samps == 0) ? uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS
                                     : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE) {}

U220::~U220() {}

void U220::init(dma_channel::ch_config dma_configs[2]) {
	initialize_usrp();
	initialize_dma(dma_configs);
	setup_tx_streamer();
	setup_rx_streamer();
}

void U220::initialize_usrp() {
	device_args.append(",serial=");
	device_args.append(serial);
	std::cout << std::endl;
	std::cout << boost::format("Creating the usrp device with: %s...") % device_args << std::endl;

	usrp = uhd::usrp::multi_usrp::make(PIString2StdString(device_args));

	if (!user_config.ref.isEmpty()) {
		usrp->set_clock_source(PIString2StdString(user_config.ref));
	}

	usrp->set_tx_rate(user_config.rate);
	usrp->set_rx_rate(user_config.rate);
	board_config.rate = usrp->get_tx_rate();

	// Configure both channels (0 and 1)
	for (size_t ch = 0; ch < 2; ch++) {
		configure_tx_channel(ch);
		configure_rx_channel(ch);
	}

	std::this_thread::sleep_for(std::chrono::seconds(1));
}

void U220::initialize_dma(dma_channel::ch_config dma_configs[2]) {
	dma_channels.resize(2);
	for (size_t i = 0; i < 2; i++) {
		dma_channels[i] = new dma_channel();
		dma_channels[i]->init(dma_configs[i]);
		dma_channels[i]->get_all_buffers(dma_tx_buffers[i]);
		dma_channels[i]->set_num_transfers(0);

		for (size_t k = 0; k < TX_BUFFER_COUNT; k++) {
			piCout << "ch" << i << " buf" << k << " " << PICoutManipulators::PICoutFormat::Hex << dma_tx_buffers[i][k];
		}
	}

	uint8_t * current_buffers[NUM_CHANNELS_TX];
	for (size_t k = 0; k < 2; k++) {
		misc_read_channel_from_file("hex_50000_lines_overflow_counter.txt",
		                            (uint8_t *)dma_tx_buffers[k][0],
		                            dma_configs[k].channel_number,
		                            BUFFER_SIZE,
		                            0);
		const uint32_t * buffer_words = static_cast<const uint32_t *>(dma_tx_buffers[k][0]);
		piCout << "ch" << k << " last loaded DMA TX value " << PICoutManipulators::PICoutFormat::Hex
			   << buffer_words[BUFFER_SIZE / sizeof(uint32_t) - 1];
	}
}

void U220::configure_tx_channel(size_t channel) {
	uhd::tune_request_t tune_request(user_config.freq, 0);
	usrp->set_tx_freq(tune_request, channel);
	board_config.freq = usrp->get_tx_freq(channel);

	// Set TX gain if specified
	if (user_config.tx_gain[channel] > 0) {
		usrp->set_tx_gain(user_config.tx_gain[channel], channel);
		board_config.tx_gain[channel] = usrp->get_tx_gain(channel);
	}

	// Set TX bandwidth if specified
	if (user_config.tx_bw[channel] > 0) {
		usrp->set_tx_bandwidth(user_config.tx_bw[channel], channel);
		board_config.tx_bw[channel] = usrp->get_tx_bandwidth(channel);
	}
}

void U220::configure_rx_channel(size_t channel) {
	uhd::tune_request_t tune_request(user_config.freq, 0);
	usrp->set_rx_freq(tune_request, channel);
	board_config.freq = usrp->get_rx_freq(channel);

	// Set rx gain if specified
	if (user_config.rx_gain[channel] > 0) {
		usrp->set_rx_gain(user_config.rx_gain[channel], channel);
		board_config.rx_gain[channel] = usrp->get_rx_gain(channel);
	}

	// Set rx bandwidth if specified
	if (user_config.rx_bw[channel] > 0) {
		usrp->set_rx_bandwidth(user_config.rx_bw[channel], channel);
		board_config.rx_bw[channel] = usrp->get_rx_bandwidth(channel);
	}
}

void U220::setup_tx_streamer() {
	// Create transmit streamer
	uhd::stream_args_t stream_args(PIString2StdString(user_config.cpu_format), PIString2StdString(user_config.otw_format));
	stream_args.channels = {0, 1};
	tx_stream            = usrp->get_tx_stream(stream_args);

	// Allocate buffer
	if (user_config.tx_spb == 0) {
		user_config.tx_spb = tx_stream->get_max_num_samps() * 200;
		user_config.tx_spb = ((user_config.tx_spb + SAMPLES_PER_CYCLE - 1) / SAMPLES_PER_CYCLE) * SAMPLES_PER_CYCLE;
	}
	board_config.tx_spb = user_config.tx_spb;

	tx_buffer.resize(user_config.tx_spb);
	tx_buffer_ptrs = {&tx_buffer.front(), &tx_buffer.front()};

	// Pre-fill buffer with waveform
	fill_buffer_with_wavetable(tx_buffer);
}

void U220::setup_rx_streamer() {
	uhd::stream_args_t stream_args(PIString2StdString(user_config.cpu_format), PIString2StdString(user_config.otw_format));
	board_config.cpu_format = user_config.cpu_format;
	board_config.otw_format = user_config.otw_format;
	stream_args.channels    = {0, 1};
	rx_stream               = usrp->get_rx_stream(stream_args);

	if (user_config.rx_spb == 0) {
		user_config.rx_spb = rx_stream->get_max_num_samps();
		user_config.rx_spb = ((user_config.rx_spb + SAMPLES_PER_CYCLE - 1) / SAMPLES_PER_CYCLE) * SAMPLES_PER_CYCLE;
	}
	board_config.rx_spb = user_config.rx_spb;

	rx_buffer.resize(4, VectorComplexS(board_config.rx_spb));
	rx_buffer_ptrs[0].resize(2);
	rx_buffer_ptrs[1].resize(2);
	for (size_t ch = 0; ch < 2; ch++) {
		rx_buffer_ptrs[0][ch] = static_cast<complexs *>(dma_tx_buffers[ch][0]);
		rx_buffer_ptrs[1][ch] = static_cast<complexs *>(dma_tx_buffers[ch][1]);
	}
}

void U220::set_pps_source() {
	usrp->set_time_source(PIString2StdString(user_config.pps));
}

void U220::set_time_sync() {
	if (user_config.pps == "external" || user_config.pps == "gpsdo") {
		std::cout << boost::format("Using %s pps") % usrp->get_time_source(0) << std::endl;
		std::cout << boost::format("Setting device timestamp to 0...") << std::endl;
		usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
		const uhd::time_spec_t last_pps_time = usrp->get_time_last_pps();
		while (last_pps_time == usrp->get_time_last_pps()) {
			;
		}
		usrp->set_time_next_pps(uhd::time_spec_t(0.0));
	} else {
		usrp->set_time_now(0.0);
	}
}

bool U220::check_lo_lock() {
	// Check LO Lock
	std::vector<std::string> sensor_names;
	const size_t tx_sensor_chan = 0;
	sensor_names                = usrp->get_tx_sensor_names(tx_sensor_chan);

	if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
		uhd::sensor_value_t lo_locked = usrp->get_tx_sensor("lo_locked", tx_sensor_chan);
		std::cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string() << std::endl;
		if (!lo_locked.to_bool()) {
			return false;
		}
	}
	return true;
}


VectorComplexS U220::take_rx_queue_and_clear(int index) {
	if (index < 0 || index >= 2) return {};
	auto ref = rx_queue[index].getRef();
	if (ref->isEmpty()) return {};
	auto ret = ref->dequeue();
	ref->clear();
	return ret;
}


VectorComplexS U220::take_rx_queue(int index) {
	if (index < 0 || index >= 2) return {};
	auto ref = rx_queue[index].getRef();
	if (ref->isEmpty()) return {};
	return ref->dequeue();
}


VectorComplexS U220::get_rx_queue(int index) {
	if (index < 0 || index >= 2) return {};
	auto ref = rx_queue[index].getRef();
	if (ref->isEmpty()) return {};
	return ref->head();
}


bool U220::check_ref_lock() {
	// Check Ref Lock
	std::vector<std::string> sensor_names;
	const size_t mboard_sensor_idx = 0;
	sensor_names                   = usrp->get_mboard_sensor_names(mboard_sensor_idx);

	if (user_config.pps == "external") {
		auto start         = std::chrono::steady_clock::now();
		const auto timeout = std::chrono::seconds(20);

		while (true) {
			if (std::chrono::steady_clock::now() - start > timeout) {
				std::cout << "Ref lock timeout after 20s" << std::endl;
				return false;
			}

			uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked", mboard_sensor_idx);

			if (ref_locked.to_bool()) {
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	return true;
}


void U220::transmit() {
	// Send buffer contents
	uint64_t num_samps         = tx_stream->send(tx_buffer_ptrs, tx_buffer.size(), tx_metadata, 1.0) * 2;
	// fill_buffer_with_wavetable(tx_buffer);
	// piCout << PISystemTime::current() << " " << num_samps;
	tx_metadata.start_of_burst = false;
	tx_metadata.has_time_spec  = false;
	tx_metadata.time_spec      = usrp->get_time_now() + uhd::time_spec_t(0.01);
	stats.tx_packet_cnt += num_samps;
}

void U220::rx_errors_worker(uhd::rx_metadata_t::error_code_t err) {
	switch (err) {
	case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT: {
		stats.rx_timeouts++;
		piCout << "Timeout in recv";
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND: {
		stats.rx_late_commands++;
		piCout << "Late command in recv";
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_BROKEN_CHAIN: {
		stats.rx_broken_chains++;
		piCout << "Broken chain in recv";
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW: {
		stats.rx_overflows++;
		piCout << "Overflow in recv";
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_ALIGNMENT: {
		stats.rx_alignment_errors++;
		piCout << "Wrong code aligment in recv";
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET: {
		stats.rx_bad_packets++;
		piCout << "Bad packet in recv";
		break;
	}
	default: {
		break;
	}
	}
}

void U220::receive() {
	// piCout << "Enter receive thread fcn";
	// Snapshot before receiving: the first successful receive clears first_transfer below.
	const bool startup_receive = first_transfer.load();
	using Clock                = std::chrono::steady_clock;
	const auto loop_start      = Clock::now();
	if (!startup_receive) {
		// Entry-to-entry includes the other boards' turns and scheduling overhead.
		// Start with the first post-startup entry so the scheduled-start wait is excluded.
		if (rx_timing.have_previous_entry) {
			const uint64_t period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(loop_start - rx_timing.previous_entry).count();
			rx_timing.period_sum_ns += period_ns;
			rx_timing.period_max_ns = std::max(rx_timing.period_max_ns, period_ns);
			rx_timing.period_count++;
		}
		rx_timing.previous_entry      = loop_start;
		rx_timing.have_previous_entry = true;
	}
	const auto recv_start = Clock::now();
	size_t num_rx_samps   = rx_stream->recv(rx_buffer_ptrs[active_buffer_idx], board_config.rx_spb, rx_metadata, rx_timeout) * 2;
	const auto recv_end   = Clock::now();
	rx_timeout            = rx_burst_pkt_time; // small timeout for subsequent recv

	if (startup_receive) {
		// The first board waits here for the timed start; the other boards wait for their turn.
		// Keep this visible without mixing it into steady-state receive latency.
		rx_timing.startup_recv_us += std::chrono::duration_cast<std::chrono::microseconds>(recv_end - recv_start).count();
		rx_timing.startup_recv_count++;
	}

	if (num_rx_samps && rx_metadata.has_time_spec && rx_acquisition.has_more()) {
		// Use device time so lost samples do not postpone refilling the command queue.
		const auto elapsed = (rx_metadata.time_spec - rx_start_time).to_ticks(usrp->get_rx_rate());
		if (elapsed >= 0 && rx_acquisition.needs_refill(static_cast<uint64_t>(elapsed) + num_rx_samps / 2)) {
			queue_rx_command();
		}
	}

	const auto dma_start = Clock::now();
	bool dma_ok          = true;
	// piCout << "Received " << num_rx_samps << " at " << rx_metadata.time_spec.get_real_secs() << "."
	// 	   << rx_metadata.time_spec.get_frac_secs();
	if (num_rx_samps) {
		active_buffer_idx = 1 - active_buffer_idx;
		first_transfer    = false;
		dma_channels[0]->start_transfer();
		dma_channels[1]->start_transfer();
		const int dma_status_0 = dma_channels[0]->wait_for_transfer();
		const int dma_status_1 = dma_channels[1]->wait_for_transfer();
		dma_ok                 = dma_status_0 == proxy_status::PROXY_NO_ERROR && dma_status_1 == proxy_status::PROXY_NO_ERROR;

	} // 0 or 1
	const auto next_recv_start = Clock::now();

	if (dma_ok && !startup_receive) {
		const uint64_t recv_us = std::chrono::duration_cast<std::chrono::microseconds>(recv_end - recv_start).count();
		const uint64_t dma_us  = std::chrono::duration_cast<std::chrono::microseconds>(next_recv_start - dma_start).count();
		const uint64_t gap_us  = std::chrono::duration_cast<std::chrono::microseconds>(next_recv_start - recv_end).count();
		const uint64_t loop_us = std::chrono::duration_cast<std::chrono::microseconds>(next_recv_start - loop_start).count();

		rx_timing.recv_max_us  = std::max(rx_timing.recv_max_us, recv_us);
		rx_timing.dma_max_us   = std::max(rx_timing.dma_max_us, dma_us);
		rx_timing.gap_max_us   = std::max(rx_timing.gap_max_us, gap_us);
		rx_timing.loop_max_us  = std::max(rx_timing.loop_max_us, loop_us);
		rx_timing.recv_sum_us += recv_us;
		rx_timing.dma_sum_us += dma_us;
		rx_timing.gap_sum_us += gap_us;
		rx_timing.loop_sum_us += loop_us;
		rx_timing.count++;
	}

	rx_errors_worker(rx_metadata.error_code);
	stats.rx_packet_cnt += num_rx_samps;
	if (dma_channels[0]->reached_transfer_limit() && dma_channels[1]->reached_transfer_limit()) {
		rx_thread.stop();
		return;
	}
	if (rx_metadata.end_of_burst) {
		const auto end_time = rx_metadata.time_spec + uhd::time_spec_t::from_ticks(num_rx_samps / 2, usrp->get_rx_rate());
		std::cout << boost::format("RX %s end timestamp (exclusive): %.9f, samples/channel: %llu\n") % serial % end_time.get_real_secs() %
						 (stats.rx_packet_cnt / 2);
		rx_thread.stop();
	} else if (Clock::now() >= rx_deadline) {
		std::cerr << "RX " << serial << ": acquisition incomplete; no end-of-burst before drain deadline" << std::endl;
		rx_thread.stop();
	}
}

void U220::start_continuous_reception(double start_time, std::shared_ptr<RxOrder> order, size_t order_index) {
	const double rate          = usrp->get_rx_rate();
	const double settling_time = start_time - usrp->get_time_now().get_real_secs();
	if (settling_time < 0.1) throw std::runtime_error("RX start timestamp is too close or already past");

	rx_burst_pkt_time   = std::max<float>(0.100f, (2 * user_config.rx_spb / rate));
	rx_timeout          = settling_time + rx_burst_pkt_time;
	rx_deadline         = std::chrono::steady_clock::time_point::max();
	rx_timing           = {};
	stats.rx_packet_cnt = 0;
	first_transfer      = true;
	rx_acquisition.reset(0);
	rx_start_time = uhd::time_spec_t(start_time);

	uhd::stream_cmd_t command(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
	command.stream_now = false;
	command.time_spec  = rx_start_time;
	rx_stream->issue_stream_cmd(command);

	status.rx_on[0]    = true;
	status.rx_on[1]    = true;
	const bool started = rx_thread.start([this, order, order_index]() {
		if (order && !order->wait(order_index)) {
			rx_thread.stop();
			return;
		}
		try {
			receive();
		} catch (const std::exception & error) {
			if (order) order->cancel();
			rx_thread.stop();
			std::cerr << "RX " << serial << ": " << error.what() << std::endl;
		} catch (...) {
			if (order) order->cancel();
			rx_thread.stop();
			std::cerr << "RX " << serial << ": unknown receive failure" << std::endl;
		}
		if (order) order->finish(order_index, rx_thread.isStopping());
	});
	if (!started) {
		if (order) order->cancel();
		throw std::runtime_error("Failed to start receive thread");
	}
	std::cout << std::endl << "Continuous reception started for " << serial << std::endl;
}

bool U220::sync() {
	PISystemTime sync_time = PISystemTime::current();
	if (!usrp || !tx_stream) {
		std::cerr << "USRP not properl	y initialized!" << std::endl;
		return false;
	}

	set_pps_source();

	if (!check_ref_lock()) {
		std::cerr << "PPS REF lock detection failed!" << std::endl;
		return false;
	}

	// GlobalData resets every board on one shared PPS edge after these checks.
	if (!check_lo_lock()) {
		std::cerr << "LO Lock detection failed!" << std::endl;
		return false;
		;
	}

	board_config.pps = StdString2PIString(usrp->get_time_source(0));
	board_config.ref = StdString2PIString(usrp->get_clock_source(0));

	piCout << "Device" << serial << "sync at" << sync_time; // compare this times on console (should be +- same)
	std::cout << "Real board configuration for " << serial << std::endl;
	print_config(board_config);
	return true;
}

void U220::start_sync() {
	sync_thread.startOnce([this]() { status.on = sync(); });
}

void U220::start_transmission(double start_time) {
	// Setup tx_metadata
	tx_metadata.start_of_burst = true;
	tx_metadata.end_of_burst   = false;
	tx_metadata.has_time_spec  = true;
	tx_metadata.time_spec      = uhd::time_spec_t(start_time);

	status.tx_on[0]            = true;
	status.tx_on[1]            = true;
	tx_thread.start([this]() { transmit(); });
	std::cout << std::endl << "Transmission started for " << serial << std::endl;
}

void U220::queue_rx_command() {
	rx_stream_cmd = next_rx_stream_command(rx_acquisition, rx_start_time, usrp->get_rx_rate());
	rx_stream->issue_stream_cmd(rx_stream_cmd);
}

void U220::start_reception(double start_time, double acquisition_seconds, std::shared_ptr<RxOrder> order, size_t order_index) {
	const double rate          = usrp->get_rx_rate();
	const uint64_t samples     = RxAcquisition::sample_count(acquisition_seconds, rate);
	const double settling_time = start_time - usrp->get_time_now().get_real_secs();
	if (settling_time < 0.1) {
		throw std::runtime_error("RX start timestamp is too close or already past");
	}
	rx_deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
														 std::chrono::duration<double>(settling_time + acquisition_seconds + 5.0));
	rx_burst_pkt_time   = std::max<float>(0.100f, (2 * user_config.rx_spb / rate));
	rx_timeout          = settling_time + rx_burst_pkt_time; // expected settling time + padding for first recv
	rx_timing           = {};
	stats.rx_packet_cnt = 0;
	first_transfer      = true;

	// Keep at most two finite commands queued: the current chunk and its successor.
	// Only the last chunk ends the burst; continuations start immediately after the previous chunk.
	rx_acquisition.reset(samples);
	rx_start_time = uhd::time_spec_t(start_time);
	queue_rx_command();
	if (rx_acquisition.has_more()) queue_rx_command();

	status.rx_on[0]    = true;
	status.rx_on[1]    = true;
	const bool started = rx_thread.start([this, order, order_index]() {
		if (order && !order->wait(order_index)) {
			rx_thread.stop();
			return;
		}
		try {
			receive();
		} catch (const std::exception & error) {
			if (order) order->cancel();
			rx_thread.stop();
			std::cerr << "RX " << serial << ": " << error.what() << std::endl;
			return;
		} catch (...) {
			if (order) order->cancel();
			rx_thread.stop();
			std::cerr << "RX " << serial << ": unknown receive failure" << std::endl;
			return;
		}
		if (order) order->finish(order_index, rx_thread.isStopping());
	});
	if (!started) {
		if (order) order->cancel();
		throw std::runtime_error("Failed to start receive thread");
	}
	std::cout << std::endl << "Reception started for " << serial << std::endl;
}

void U220::stop_transmission() {
	tx_thread.stopAndWait();
	if (tx_stream) {
		tx_metadata.end_of_burst = true;
		tx_stream->send("", 0, tx_metadata);
		std::cout << "Stream tx stopped." << std::endl;
	} else {
		std::cout << "No stream to stop." << std::endl << std::endl;
	}
}

void U220::stop_reception() {
	// The receive worker stops after both TX DMAs reach the shared transfer target.
	rx_thread.waitForFinish();
	uhd::stream_cmd_t stop_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
	stop_cmd.stream_now = true;
	rx_stream->issue_stream_cmd(stop_cmd);
	status.rx_on[0] = status.rx_on[1] = false;
	deinitialize_dma();
	std::cout << "Stream rx stopped for device " << serial << std::endl;
	if (rx_timing.startup_recv_count) {
		std::cout << "RX " << serial << ": startup recv total = " << rx_timing.startup_recv_us
				  << " us (includes scheduled-start wait), calls = " << rx_timing.startup_recv_count << std::endl;
	}
	if (rx_timing.count) {
		std::cout << "RX " << serial << ": steady-state recv avg/max = " << rx_timing.recv_sum_us / rx_timing.count << "/"
				  << rx_timing.recv_max_us << " us, DMA avg/max = " << rx_timing.dma_sum_us / rx_timing.count << "/" << rx_timing.dma_max_us
				  << " us, gap avg/max = " << rx_timing.gap_sum_us / rx_timing.count << "/" << rx_timing.gap_max_us
				  << " us, receive/DMA duration avg/max = " << rx_timing.loop_sum_us / rx_timing.count << "/" << rx_timing.loop_max_us
				  << " us, loops = " << rx_timing.count << std::endl;
	} else {
		std::cout << "RX " << serial << ": no steady-state timing samples collected" << std::endl;
	}
	if (rx_timing.period_count) {
		std::cout << "RX " << serial << ": iteration period avg/max = " << rx_timing.period_sum_ns / rx_timing.period_count / 1000.0 << "/"
				  << rx_timing.period_max_ns / 1000.0 << " us, intervals = " << rx_timing.period_count
				  << " (entry-to-entry, excludes startup)" << std::endl;
	} else {
		std::cout << "RX " << serial << ": no steady-state iteration periods collected" << std::endl;
	}
	PRINT_U220_STATS(stats);
}

int U220::get_dma_pending_transfer_target() const {
	int target = 0;
	for (auto * channel: dma_channels)
		target = std::max(target, channel->get_pending_transfer_target());
	return target;
}

void U220::set_dma_num_transfers(int num_transfers) {
	for (auto * channel: dma_channels)
		channel->set_num_transfers(num_transfers);
}

void U220::deinitialize_dma() {
	for (int k = dma_channels.size() - 1; k >= 0; k--) {
		dma_channels[k]->cleanup();
		delete dma_channels[k];
		dma_channels[k] = nullptr;
	}
}


void U220::set_tx_gain(double new_gain) {
	for (size_t ch = 0; ch < 2; ch++) {
		user_config.tx_gain[ch] = new_gain;
		usrp->set_tx_gain(user_config.tx_gain[ch], ch);
		board_config.tx_gain[ch] = usrp->get_tx_gain(ch);
	}
}

void U220::set_rx_gain(double new_gain) {
	for (size_t ch = 0; ch < 2; ch++) {
		user_config.rx_gain[ch] = new_gain;
		usrp->set_rx_gain(user_config.rx_gain[ch], ch);
		board_config.rx_gain[ch] = usrp->get_rx_gain(ch);
	}
}


void U220::set_frequency(double new_freq) {
	for (size_t ch = 0; ch < 2; ch++) {
		user_config.freq = new_freq;
		uhd::tune_request_t tune_request(user_config.freq, 0);
		usrp->set_tx_freq(tune_request, ch);
		usrp->set_rx_freq(tune_request, ch);
		board_config.freq = usrp->get_tx_freq(ch);
	}
}

void U220::set_serial(const PIString & ser) {
	serial = ser;
}
