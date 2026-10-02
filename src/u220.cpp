#include "u220.hpp"

#include "user_regs.h"

#include <pistring_std.h>

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

void U220::init(void ** tx0_buf, void ** tx1_buf) {
	initialize_usrp();
	setup_tx_streamer();
	setup_rx_streamer(tx0_buf, tx1_buf);
	// setup_rx_streamer();
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

	set_sr_core_tx_delay(usrp, 0x03, 0x30);
	set_sr_core_play_gpio_tx_enable(usrp, 0b11);
}

void U220::setup_rx_streamer(void ** tx0_buf, void ** tx1_buf) {
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
		rx_buffer_ptrs[0][ch] = static_cast<complexs *>(tx0_buf[ch]);
		rx_buffer_ptrs[1][ch] = static_cast<complexs *>(tx1_buf[ch]);
	}
	set_sr_core_rx_delay(usrp, 0xFD);
	set_sr_core_play_gpio_rx_enable(usrp, 0b11);
	set_sr_core_play_rx_insert_count(usrp, 1);
	set_sr_core_play_pps_time_reset(usrp, 0x05DC05DD);
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

	rx_buffer.resize(2, VectorComplexS(board_config.rx_spb));
	rx_buffer_ptrs[0].resize(2);
	rx_buffer_ptrs[1].resize(2);
	for (size_t ch = 0; ch < 2; ch++) {
		rx_buffer_ptrs[ch][0] = &rx_buffer[ch].front();
		rx_buffer_ptrs[ch][1] = &rx_buffer[ch].front();
	}
	set_sr_core_rx_delay(usrp, 0xFD);
	set_sr_core_play_gpio_rx_enable(usrp, 0b11);
	set_sr_core_play_rx_insert_count(usrp, 1);
	set_sr_core_play_pps_time_reset(usrp, 0x05DC05DD);
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
	tx_metadata.time_spec      = usrp->get_time_now() + uhd::time_spec_t(0.05);
	stats.tx_packet_cnt += num_samps;
}

void U220::rx_errors_worker(uhd::rx_metadata_t::error_code_t err) {
	switch (err) {
	case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT: {
		stats.rx_timeouts++;
		piCout << "Timeout in recv for" << serial;
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND: {
		stats.rx_late_commands++;
		piCout << "Late command in recv for" << serial;
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_BROKEN_CHAIN: {
		stats.rx_broken_chains++;
		piCout << "Broken chain in recv for" << serial;
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW: {
		stats.rx_overflows++;
		piCout << "Overflow in recv for" << serial;
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_ALIGNMENT: {
		stats.rx_alignment_errors++;
		piCout << "Wrong code aligment in recv for" << serial;
		break;
	}
	case uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET: {
		stats.rx_bad_packets++;
		piCout << "Bad packet in recv for" << serial;
		break;
	}
	default: {
		break;
	}
	}
}

void U220::receive() {
	const auto recv_start = std::chrono::steady_clock::now();
	size_t num_rx_samps   = rx_stream->recv(rx_buffer_ptrs[active_buffer_idx], board_config.rx_spb, rx_metadata, rx_timeout) * 2;
	const auto recv_us    = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - recv_start).count();
	rx_timeout            = rx_burst_pkt_time; // small timeout for subsequent recv

	// for (int ch: {0, 1}) {
	// 	auto ch_ptr = rx_queue[ch].getRef();
	// 	ch_ptr->push_back(rx_buffer[ch]);
	// }

	rx_errors_worker(rx_metadata.error_code);
	if (num_rx_samps) {
		active_buffer_idx = 1 - active_buffer_idx;
		stats.cycles_completed++;
		stats.rx_recv_total_us += recv_us;
		stats.rx_recv_max_us = std::max<uint64_t>(stats.rx_recv_max_us, recv_us);
	}
	stats.rx_packet_cnt += num_rx_samps;
	// piCout << "Received " << num_rx_samps << "/" << board_config.rx_spb * 2 << "pkt_cnt" << stats.rx_packet_cnt
	// 	   << "hdr:" << PICoutManipulators::Hex << *((uint32_t *)rx_buffer_ptrs[0]) << "cnt:" << *(((uint32_t *)rx_buffer_ptrs[0]) + 1)
	// 	   << "serial" << serial;
	if (stats.rx_packet_cnt % (board_config.rx_spb * 5000) == 0) {
		piCout << serial << "Received " << num_rx_samps << "/" << board_config.rx_spb * 2 << "pkt_cnt" << stats.rx_packet_cnt
			   << "last PPS time:" << usrp->get_time_last_pps().get_real_secs() << "s or" << usrp->get_time_last_pps().get_tick_count(5e6);
		// PRINT_U220_STATS(stats);
	}
}

bool U220::sync() {
	PISystemTime sync_time = PISystemTime::current();
	if (!usrp) {
		std::cerr << "USRP not properly initialized!" << std::endl;
		return false;
	}

	set_pps_source();

	if (!check_ref_lock()) {
		std::cerr << "PPS REF lock detection failed!" << std::endl;
		return false;
	}

	set_time_sync();
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

void U220::start_transmission() {
	// set_sr_core_play_enable(usrp, 1);
	// set_sr_core_play_gain_on_enable(usrp, 1);
	set_sr_core_play_start_tx(usrp); // sets enable, gain_on_enable, start_pulse_enable, start_pulse_40m
	std::cout << std::endl << "Transmission started for " << serial << std::endl;
}

void U220::start_reception(double settling_time) {
	const double rate = usrp->get_rx_rate();
	auto start_time   = usrp->get_time_now() + uhd::time_spec_t(settling_time);
	rx_burst_pkt_time = std::max<float>(0.100f, (2 * user_config.rx_spb / rate));
	rx_timeout        = settling_time + rx_burst_pkt_time; // expected settling time + padding for first recv

	// setup streaming
	set_sr_core_play_rx_enable(usrp, 1);
	print_config(board_config);
	rx_stream_cmd.num_samps  = board_config.rx_spb;
	rx_stream_cmd.stream_now = false;
	rx_stream_cmd.time_spec  = start_time;
	// rx_stream_cmd.time_spec  = uhd::time_spec_t(0, 4640, 5e6);
	rx_stream->issue_stream_cmd(rx_stream_cmd);

	status.rx_on[0] = true;
	status.rx_on[1] = true;
	shutdown_done   = false;

	// rx_thread.start([this]() { receive(); });
	std::cout << std::endl << "Reception started for " << serial << std::endl;
}

void U220::stop_transmission() {
	set_sr_core_play_stop_tx(usrp);
	// set_sr_core_play_enable(usrp, 0);
	// set_sr_core_play_gain_on_enable(usrp, 0);
	// tx_thread.stopAndWait();
	// if (tx_stream) {
	// 	tx_metadata.end_of_burst = true;
	// 	tx_stream->send("", 0, tx_metadata);
	// 	std::cout << "Stream tx stopped." << std::endl;
	// } else {
	// 	std::cout << "No stream to stop." << std::endl << std::endl;
	// }
}

void U220::stop_and_drain_rx() {
	if (shutdown_done) return;
	shutdown_done = true;
	try {
		uhd::stream_cmd_t stop_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
		stop_cmd.stream_now = true;
		rx_stream->issue_stream_cmd(stop_cmd);

		// Discard trailing USB samples without touching or submitting DMA buffers.
		const size_t count = rx_stream->get_max_num_samps();
		std::vector<std::vector<complexs>> buffers(2, std::vector<complexs>(count));
		std::vector<void *> pointers{buffers[0].data(), buffers[1].data()};
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		size_t discarded    = 0;
		while (std::chrono::steady_clock::now() < deadline) {
			uhd::rx_metadata_t metadata;
			// One packet limits each recv; a nonzero timeout also surfaces pending errors.
			discarded += rx_stream->recv(pointers, count, metadata, 0.05, true);
			if (metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
				std::cout << "RX " << serial << ": shutdown drained " << discarded << " samples/channel" << std::endl;
				return;
			}
			if (metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
			    metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
				std::cerr << "RX " << serial << ": shutdown drain: " << metadata.strerror() << std::endl;
				return;
			}
		}
		std::cerr << "RX " << serial << ": shutdown drain deadline reached" << std::endl;
	} catch (const std::exception & error) {
		std::cerr << "RX " << serial << ": shutdown stop/drain failed: " << error.what() << std::endl;
	} catch (...) {
		std::cerr << "RX " << serial << ": unknown shutdown stop/drain failure" << std::endl;
	}
}

void U220::stop_reception() {
	// rx_thread.stopAndWait();
	stop_and_drain_rx();
	set_sr_core_play_pps_time_reset(usrp, 0);

	// rx_stream->issue_stream_cmd(rx_stream_cmd);

	piCout << "RX recv" << serial << "mean(us):" << static_cast<double>(stats.rx_recv_total_us) / stats.cycles_completed
	       << "max(us):" << stats.rx_recv_max_us;
	piCout << "Stream rx stopped";
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

void U220::mcs_stage1() {
	auto tree = usrp->get_device()->get_tree();
	tree->access<int>("/mboards/0/mcs/command").set(1);
}

void U220::mcs_stage2() {
	auto tree = usrp->get_device()->get_tree();
	tree->access<int>("/mboards/0/mcs/command").set(2);
}

void U220::mcs_finish() {
	auto tree = usrp->get_device()->get_tree();
	tree->access<int>("/mboards/0/mcs/command").set(3);
}
