#include "u220.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

void U220::init() {
	initialize_usrp();
}

void U220::setup(void ** tx0_buf, void ** tx1_buf) {
	setup_tx_streamer();
	setup_rx_streamer(tx0_buf, tx1_buf);
	// setup_rx_streamer();
}

void U220::setup() {
	setup_tx_streamer();
	setup_rx_streamer();
	// setup_rx_streamer();
}

void U220::initialize_usrp() {
	device_args.append(",serial=");
	device_args.append(serial);
	device_args.append(",master_clock_rate=20e6");
	std::cout << std::endl;
	std::cout << boost::format("Creating the usrp device with: %s...") % device_args << std::endl;

	usrp = uhd::usrp::multi_usrp::make(PIString2StdString(device_args));
	// usrp->set_master_clock_rate(20e6);

	if (!user_config.ref.isEmpty()) {
		usrp->set_clock_source(PIString2StdString(user_config.ref));
	}

	usrp->set_tx_rate(user_config.rate);
	usrp->set_rx_rate(user_config.rate);
	board_config.rate = usrp->get_rx_rate();

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

	for (size_t n_buf = 0; n_buf < TX_BUFFER_COUNT; n_buf++) {
		rx_buffer_ptrs[n_buf].resize(2);
		rx_buffer_ptrs[n_buf][0] = static_cast<complexs *>(tx0_buf[n_buf]);
		rx_buffer_ptrs[n_buf][1] = static_cast<complexs *>(tx1_buf[n_buf]);
	}
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

	rx_buffer.resize(2 * TX_BUFFER_COUNT, VectorComplexS(board_config.rx_spb));
	for (size_t n_buf = 0; n_buf < TX_BUFFER_COUNT; n_buf++) {
		rx_buffer_ptrs[n_buf].resize(2);
		rx_buffer_ptrs[n_buf][0] = &rx_buffer[2 * n_buf].front();
		rx_buffer_ptrs[n_buf][1] = &rx_buffer[2 * n_buf + 1].front();
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

void U220::start_rx_dump() {
	rx_dump_requested.store(true);
}

VectorComplexS U220::take_dump_queue_and_clear(int index) {
	if (index < 0 || index >= 2) return {};
	auto ref = dump_queue[index].getRef();
	VectorComplexS result;
	while (!ref->isEmpty()) {
		const auto block = ref->dequeue();
		for (const auto & sample: block)
			result.push_back(sample);
	}
	return result;
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


uhd::rx_metadata_t::error_code_t U220::receive(const int active_buffer_idx) {
	using Clock               = std::chrono::steady_clock;

	const auto function_start = Clock::now();
	const auto recv_start     = function_start;

	const auto elapsed_ns     = [](Clock::time_point start, Clock::time_point end) -> int64_t {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	};

	RxTraceEntry entry{};

	// ------------------------------------------------------------
	// 1. Интервал с предыдущим recv()
	// ------------------------------------------------------------

	int64_t gap_us = 0;

	if (have_previous_recv) {
		gap_us = std::chrono::duration_cast<std::chrono::microseconds>(recv_start - previous_recv_end).count();
	}

	// ------------------------------------------------------------
	// 2. UHD recv()
	// ------------------------------------------------------------

	const auto uhd_start             = Clock::now();

	entry.pre_recv_ns                = elapsed_ns(function_start, uhd_start);

	const size_t samples_per_channel = rx_stream->recv(rx_buffer_ptrs[active_buffer_idx], board_config.rx_spb, rx_metadata, rx_timeout);

	const auto recv_end              = Clock::now();

	entry.uhd_recv_ns                = elapsed_ns(uhd_start, recv_end);

	const uint64_t recv_us           = std::chrono::duration_cast<std::chrono::microseconds>(recv_end - recv_start).count();

	previous_recv_end                = recv_end;
	have_previous_recv               = true;

	// ------------------------------------------------------------
	// 3. Время последнего принятого отсчёта
	// ------------------------------------------------------------

	const bool valid_samples =
		samples_per_channel > 0 && rx_metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE && rx_metadata.has_time_spec;

	if (valid_samples) {
		const double first_sample_time = rx_metadata.time_spec.get_real_secs();

		last_sample_time               = first_sample_time + static_cast<double>(samples_per_channel - 1) / board_config.rate;
	}

	// ------------------------------------------------------------
	// 4. Привязка времени USRP для entry.lag_estimate_ms
	// ------------------------------------------------------------

	const auto lag_start = Clock::now();

	if (valid_samples && (!have_device_time_reference || recv_end - device_time_reference_host >= std::chrono::seconds(1))) {
		const auto host_before     = Clock::now();

		const double device_now    = usrp->get_time_now().get_real_secs();

		const auto host_after      = Clock::now();

		device_time_reference_host = host_before + (host_after - host_before) / 2;

		device_time_reference_usrp = device_now;
		have_device_time_reference = true;
	}

	entry.lag_check_ns     = elapsed_ns(lag_start, Clock::now());

	// ------------------------------------------------------------
	// 5. Формирование RxTraceEntry
	// ------------------------------------------------------------

	const auto trace_start = Clock::now();

	entry.iteration        = rx_iteration++;

	entry.start_ns         = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_start.time_since_epoch()).count();

	entry.end_ns           = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_end.time_since_epoch()).count();

	entry.gap_us           = gap_us;
	entry.recv_us          = static_cast<int64_t>(recv_us);

	entry.samples          = samples_per_channel;
	entry.error_code       = static_cast<int>(rx_metadata.error_code);
	entry.has_time_spec    = rx_metadata.has_time_spec;

	if (rx_metadata.has_time_spec) {
		entry.sample_time = rx_metadata.time_spec.get_real_secs();
	}

	entry.last_sample_time = last_sample_time;

	if (have_device_time_reference && std::isfinite(last_sample_time)) {
		const double elapsed_s             = std::chrono::duration<double>(recv_end - device_time_reference_host).count();

		const double estimated_device_time = device_time_reference_usrp + elapsed_s;

		entry.lag_estimate_ms              = (estimated_device_time - last_sample_time) * 1000.0;

		entry.lag_reference_age_us = std::chrono::duration_cast<std::chrono::microseconds>(recv_end - device_time_reference_host).count();
	}

	entry.trace_ns            = elapsed_ns(trace_start, Clock::now());

	// ------------------------------------------------------------
	// 6. Основная обработка RX
	// ------------------------------------------------------------

	const size_t num_rx_samps = samples_per_channel * 2;

	rx_timeout                = rx_burst_pkt_time;

	if (num_rx_samps) {
		stats.cycles_completed++;
		stats.rx_recv_total_us += recv_us;
		stats.rx_recv_max_us   = std::max<uint64_t>(stats.rx_recv_max_us, recv_us);
		stats.rx_chunk_counter = *(((uint32_t *)rx_buffer_ptrs[active_buffer_idx][0]) + 1);
	}

	stats.rx_packet_cnt += num_rx_samps;

	constexpr double revolution_time = 2.0;
	constexpr double block_time      = 20 * 46.4e-6;
	constexpr size_t dump_stride     = 15;
	const size_t dump_blocks         = static_cast<size_t>(std::ceil(revolution_time / block_time));

	// ------------------------------------------------------------
	// 7. Dump queue
	// ------------------------------------------------------------

	const auto dump_start            = Clock::now();

	if (samples_per_channel > 0) {
		if (rx_dump_requested.exchange(false)) {
			for (auto & queue: dump_queue) queue.getRef()->clear();
			rx_dump_blocks_remaining = dump_blocks;
		}
		if (rx_dump_blocks_remaining > 0) {
			--rx_dump_blocks_remaining;
			if ((dump_blocks - rx_dump_blocks_remaining) % dump_stride == 0 && samples_per_channel == 4680) {
				for (int ch: {0, 1}) {
					auto ch_ptr = dump_queue[ch].getRef();
					ch_ptr->enqueue(VectorComplexS(rx_buffer_ptrs[active_buffer_idx][ch], samples_per_channel));
				}
			}
		}
	}

	entry.dump_queue_ns              = elapsed_ns(dump_start, Clock::now());

	// ------------------------------------------------------------
	// 8. Основная RX queue
	// ------------------------------------------------------------

	const auto queue_start           = Clock::now();

	// if (samples_per_channel > 0 && rx_metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE) {
	// 	for (int ch: {0, 1}) {
	// 		auto ref = rx_queue[ch].getRef();

	// 		ref->clear();

	// 		ref->enqueue(VectorComplexS(rx_buffer_ptrs[active_buffer_idx][ch], samples_per_channel));
	// 	}
	// }

	entry.rx_queue_ns                = elapsed_ns(queue_start, Clock::now());

	// ------------------------------------------------------------
	// 9. Периодическое логирование — БЕЗ ИЗМЕНЕНИЙ
	// ------------------------------------------------------------

	const auto logging_start         = Clock::now();
	static auto cached_last_pps_time = 0;
	if (stats.rx_packet_cnt % (board_config.rx_spb * 5000) == 0) {
		const auto last_pps  = usrp->get_time_last_pps();
		cached_last_pps_time = last_pps.get_full_secs();

		piCout << serial << "Recv " << num_rx_samps << "/" << board_config.rx_spb * 2 << "pkt_cnt" << stats.cycles_completed
			   << "last PPS time:" << cached_last_pps_time << "s or" << last_pps.get_tick_count(5e6)
			   << "cnt:" << *(((uint32_t *)rx_buffer_ptrs[active_buffer_idx][0]) + 1);
	}

	entry.logging_ns       = elapsed_ns(logging_start, Clock::now());

	// ------------------------------------------------------------
	// 10. Итоговая трассировка
	// ------------------------------------------------------------

	entry.receive_total_ns = elapsed_ns(function_start, Clock::now());
	entry.last_pps_time    = cached_last_pps_time;
	rx_trace.push(entry);

	return rx_metadata.error_code;
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

void U220::start_reception(double settling_time) {
	const double rate = usrp->get_rx_rate();
	auto start_time   = usrp->get_time_now() + uhd::time_spec_t(settling_time);
	rx_burst_pkt_time = std::max<float>(0.100f, (2 * user_config.rx_spb / rate));
	rx_timeout        = settling_time + rx_burst_pkt_time; // expected settling time + padding for first recv

	// setup streaming

	rx_trace.clear();
	rx_iteration               = 0;

	have_previous_recv         = false;

	last_sample_time           = std::numeric_limits<double>::quiet_NaN();

	have_device_time_reference = false;

	// print_config(board_config);
	rx_stream_cmd.num_samps    = board_config.rx_spb;
	rx_stream_cmd.stream_now   = false;
	rx_stream_cmd.time_spec    = start_time;
	// rx_stream_cmd.time_spec  = uhd::time_spec_t(0, 4640, 5e6);
	rx_stream->issue_stream_cmd(rx_stream_cmd);

	status.rx_on[0] = true;
	status.rx_on[1] = true;
	shutdown_done   = false;

	// rx_thread.start([this]() { receive(); });
	std::cout << std::endl << "Reception started for " << serial << std::endl;
}

void U220::stop_transmission() {
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

void U220::enable_external_lo() {
	if (!usrp) {
		throw std::runtime_error("U220: USRP not initialized");
	}

	auto tree              = usrp->get_device()->get_tree();

	const std::string path = "/mboards/0/lo/external";

	if (!tree->exists(path)) {
		throw std::runtime_error("U220: external LO property not found");
	}

	tree->access<int>(path).set(1);
}

void U220::ensm_to_alert() {
	auto tree              = usrp->get_device()->get_tree();
	const std::string path = "/mboards/0/mcs/command";
	tree->access<int>(path).set(4);
}

void U220::ensm_to_fdd() {
	auto tree              = usrp->get_device()->get_tree();
	const std::string path = "/mboards/0/mcs/command";
	tree->access<int>(path).set(5);
}

void U220::ensm_enable_external() {
	auto tree = usrp->get_device()->get_tree();
	tree->access<int>("/mboards/0/mcs/command").set(6);
}

uint8_t U220::get_ensm_state() {
	auto tree = usrp->get_device()->get_tree();

	return static_cast<uint8_t>(tree->access<int>("/mboards/0/ensm/state").get());
}

void uhd_recv_diagnostics::reset() {
	calls             = 0;
	samples           = 0;

	recv_total_us     = 0;
	recv_max_us       = 0;

	interval_total_us = 0;
	interval_max_us   = 0;

	partial_reads     = 0;
	zero_reads        = 0;
	overflows         = 0;
	rx_lag_last_ms    = 0.0;
	rx_lag_max_ms     = 0.0;
	rx_lag_min_ms     = 0.0;
	rx_lag_total_ms   = 0.0;
	rx_lag_count      = 0;
}

void uhd_recv_diagnostics::print(const PIString & serial) {
	if (calls == 0) return;

	const double avg_recv     = static_cast<double>(recv_total_us) / calls;
	const double avg_interval = calls > 1 ? static_cast<double>(interval_total_us) / (calls - 1) : 0.0;
	const double lag_avg      = rx_lag_count ? rx_lag_total_ms / rx_lag_count : 0.0;

	printf("\n========== UHD RECV %s ==========\n"
	       "Calls:            %llu\n"
	       "Samples:          %llu\n"
	       "Recv avg/max:     %.1f / %llu us\n"
	       "Interval avg/max: %.1f / %llu us\n"
	       "Partial reads:    %llu\n"
	       "Zero reads:       %llu\n"
	       "Overflows:        %llu\n"
	       "RX lag last/avg/min/max: %.3f / %.3f / %.3f / %.3f ms\n"
	       "RX lag count: %llu\n"
	       "====================================\n",

	       PIString2StdString(serial).c_str(),

	       (unsigned long long)calls,
	       (unsigned long long)samples,

	       avg_recv,
	       (unsigned long long)recv_max_us,

	       avg_interval,
	       (unsigned long long)interval_max_us,

	       (unsigned long long)partial_reads,
	       (unsigned long long)zero_reads,
	       (unsigned long long)overflows,
	       rx_lag_last_ms,
	       lag_avg,
	       rx_lag_min_ms,
	       rx_lag_max_ms,
	       (unsigned long long)rx_lag_count);

	reset();
}
