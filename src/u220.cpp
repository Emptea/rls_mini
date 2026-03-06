#include "u220.hpp"

#include "rlso_const.hpp"
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


static std::vector<std::complex<float>> init_wavetable() {
	std::vector<std::complex<float>> result;
	result.reserve(wave_table_far.size() + SAMPLES_WAIT_AFTER_FAR + wave_table_close.size() + SAMPLES_WAIT_AFTER_CLOSE);

	result.insert(result.end(), wave_table_far.begin(), wave_table_far.end());
	result.insert(result.end(), SAMPLES_WAIT_AFTER_FAR, std::complex<float>(0.0f, 0.0f));
	result.insert(result.end(), wave_table_close.begin(), wave_table_close.end());
	result.insert(result.end(), SAMPLES_WAIT_AFTER_CLOSE, std::complex<float>(0.0f, 0.0f));

	return result;
}

void U220::fill_buffer_with_wavetable(std::vector<std::complex<float>> & buffer) {
	static const std::vector<std::complex<float>> wave_table = init_wavetable();

	if (wave_table.empty()) {
		throw std::invalid_argument("Wave table cannot be empty");
	}

	size_t table_size = wave_table.size();

	for (size_t i = 0; i < buffer.size(); ++i) {
		buffer[i] = wave_table[i % table_size];
	}
}


U220::U220(const std::string & args, uint64_t num_samps, u220_config_t config)
	: serial("")
	, device_args(args)
	, total_num_samps(num_samps)
	, user_config(config)
	, rx_stream_cmd((num_samps == 0) ? uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS
                                     : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE) {}

U220::~U220() {}

void U220::init() {
	initialize_usrp();
	setup_tx_streamer();
	setup_rx_streamer();
}

void U220::initialize_usrp() {
	std::cout << std::endl;
	std::cout << boost::format("Creating the usrp device with: %s...") % device_args << std::endl;

	usrp = uhd::usrp::multi_usrp::make(device_args);

	if (!user_config.ref.empty()) {
		usrp->set_clock_source(user_config.ref);
	}

	usrp->set_tx_rate(user_config.rate);
	board_config.rate = usrp->get_tx_rate();

	// Configure both channels (0 and 1)
	for (size_t ch = 0; ch < 2; ch++) {
		configure_channel(ch);
	}

	std::this_thread::sleep_for(std::chrono::seconds(1));
}

void U220::configure_channel(size_t channel) {
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

void U220::setup_tx_streamer() {
	// Create transmit streamer
	uhd::stream_args_t stream_args("fc32", user_config.otw_format);
	stream_args.channels = {0, 1};
	tx_stream            = usrp->get_tx_stream(stream_args);

	// Allocate buffer
	if (user_config.tx_spb == 0) {
		user_config.tx_spb  = tx_stream->get_max_num_samps() * 20;
		user_config.tx_spb  = ((user_config.tx_spb + SAMPLES_PER_CYCLE - 1) / SAMPLES_PER_CYCLE) * SAMPLES_PER_CYCLE;
		board_config.tx_spb = user_config.tx_spb;
	}

	tx_buffer.resize(user_config.tx_spb);
	tx_buffer_ptrs = {&tx_buffer.front(), &tx_buffer.front()};

	// Pre-fill buffer with waveform
	fill_buffer_with_wavetable(tx_buffer);
}

void U220::setup_rx_streamer() {
	uhd::stream_args_t stream_args(user_config.cpu_format, user_config.otw_format);
	stream_args.channels = {0, 1};
	rx_stream            = usrp->get_rx_stream(stream_args);

	if (user_config.rx_spb == 0) {
		user_config.rx_spb  = rx_stream->get_max_num_samps() * 20;
		user_config.rx_spb  = ((user_config.rx_spb + SAMPLES_PER_CYCLE - 1) / SAMPLES_PER_CYCLE) * SAMPLES_PER_CYCLE;
		board_config.rx_spb = user_config.rx_spb;
	}

	rx_buffer.resize(2, std::vector<std::complex<float>>(board_config.rx_spb));
	rx_buffer_ptrs.resize(2);
	for (size_t ch = 0; ch < 2; ch++) {
		rx_buffer_ptrs[ch] = &rx_buffer[ch].front();
	}
}

void U220::set_pps_source() {
	usrp->set_time_source(user_config.pps);
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

bool U220::start_transmission(double start_time) {
	if (!usrp || !tx_stream) {
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
	}

	board_config.pps = usrp->get_time_source(0);
	board_config.ref = usrp->get_clock_source(0);

	std::cout << "Real board configuration for " << serial << std::endl;
	print_config(board_config);

	// Setup tx_metadata
	tx_metadata.start_of_burst = true;
	tx_metadata.end_of_burst   = false;
	tx_metadata.has_time_spec  = true;
	tx_metadata.time_spec      = uhd::time_spec_t(start_time);

	return true;
}

void U220::start_reception(double settling_time) {
	rx_timeout               = settling_time + 0.1f; // expected settling time + padding for first recv

	// setup streaming
	rx_stream_cmd.num_samps  = total_num_samps;
	rx_stream_cmd.stream_now = false;
	rx_stream_cmd.time_spec  = uhd::time_spec_t(settling_time);
	rx_stream->issue_stream_cmd(rx_stream_cmd);
}

void U220::transmit(const bool * stop_signal) {
	uint64_t num_acc_samps = 0;
	std::cout << std::endl << "Transmission started for " << serial << std::endl;

	while (true) {
		if (*stop_signal) {
			break;
		}

		if (total_num_samps > 0 && num_acc_samps >= total_num_samps) {
			std::cout << "Completed transmission of " << total_num_samps << " samples" << std::endl;
			break;
		}

		// Send buffer contents
		num_acc_samps += tx_stream->send(tx_buffer_ptrs, tx_buffer.size(), tx_metadata);
		fill_buffer_with_wavetable(tx_buffer);

		tx_metadata.start_of_burst = false;
		tx_metadata.has_time_spec  = false;
		tx_metadata.time_spec      = usrp->get_time_now() + uhd::time_spec_t(0.05);
	}
}

void U220::receive(const bool * stop_signal) {
	uint64_t num_acc_samps = 0;
	std::cout << std::endl << "Reception started for " << serial << std::endl;
	bool overflow_message = true;

	while (true) {
		if (*stop_signal) {
			break;
		}

		if (total_num_samps > 0 && num_acc_samps >= total_num_samps) {
			std::cout << "Completed reception of " << total_num_samps << " samples" << std::endl;
			break;
		}

		size_t num_rx_samps = rx_stream->recv(rx_buffer_ptrs, board_config.rx_spb, rx_metadata, rx_timeout);
		rx_timeout          = 0.1f; // small timeout for subsequent recv

		if (rx_metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
			std::cout << boost::format("Timeout while streaming") << std::endl;
			break;
		}
		if (rx_metadata.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
			if (overflow_message) {
				overflow_message = false;
				std::cerr << boost::format("Got an overflow indication. Please consider the following:\n"
				                           "  Your write medium must sustain a rate of %fMB/s.\n"
				                           "  Dropped samples will not be written to the file.\n"
				                           "  Please modify this example for your purposes.\n"
				                           "  This message will not appear again.\n") %
								 (usrp->get_rx_rate() * 8.0 / 1e6);
			}
			continue;
		}
		if (rx_metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
			throw std::runtime_error(str(boost::format("Receiver error %s") % rx_metadata.strerror()));
		}

		num_acc_samps += num_rx_samps;
		std::cout << "Received " << num_rx_samps << " and total of " << num_acc_samps << std::endl;
	}
}

void U220::stop_transmission() {
	if (tx_stream) {
		tx_metadata.end_of_burst = true;
		tx_stream->send("", 0, tx_metadata);
		std::cout << "Stream stopped." << std::endl;
	} else {
		std::cout << "No stream to stop." << std::endl << std::endl;
	}
}

void U220::stop_reception() {
	rx_stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
	rx_stream->issue_stream_cmd(rx_stream_cmd);
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
		board_config.freq = usrp->get_tx_freq(ch);
	}
}

void U220::set_serial(const std::string & ser) {
	serial = ser;
	device_args.append(",serial=");
	device_args.append(ser);
	std::cout << device_args << std::endl;
}

