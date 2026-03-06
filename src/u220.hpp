#ifndef U220_HPP
#define U220_HPP

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <string>
#include <thread>
#include <uhd/exception.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/thread.hpp>

typedef struct u220_config {
	double rate;
	double freq;
	double tx_gain[2];
	double rx_gain[2];
	double tx_bw[2];
	double rx_bw[2];
	std::string ref;
	std::string cpu_format;
	std::string otw_format;
	std::string pps;
	size_t tx_spb;
	size_t rx_spb;
} u220_config_t;

void print_config(const u220_config_t & config);

class U220 {
private:
	uhd::usrp::multi_usrp::sptr usrp;

	uhd::rx_streamer::sptr rx_stream;
	std::vector<std::vector<std::complex<float>>> rx_buffer;
	std::vector<std::complex<float> *> rx_buffer_ptrs;
	uhd::rx_metadata_t rx_metadata;
	double rx_timeout;
	uhd::stream_cmd_t rx_stream_cmd;

	uhd::tx_streamer::sptr tx_stream;
	std::vector<std::complex<float>> tx_buffer;
	std::vector<std::complex<float> *> tx_buffer_ptrs;
	uhd::tx_metadata_t tx_metadata;

	// Configuration parameters
	std::string serial;
	std::string device_args;

	u220_config_t user_config;
	u220_config_t board_config;

	uint64_t total_num_samps;

	void fill_buffer_with_wavetable(std::vector<std::complex<float>> & buffer);
	void initialize_usrp();
	void configure_channel(size_t channel);
	void setup_tx_streamer();
	void setup_rx_streamer();

	std::atomic<bool> rx_running{false};

public:
	U220(const std::string & args = "recv_frame_size=4096,num_recv_frames=128,send_frame_size=8192,num_send_frames=512",
	     uint64_t num_samps       = 0,
	     u220_config_t config     = {
             5e6,
             3.6e9,
             {0.0},
             {0.0},
             {56e6, 56e6},
             {56e6, 56e6},
             "",
             "fc32",
             "sc16",
             "internal",
             0,
             0
    });

	~U220();

	void init();

	bool start_transmission(double start_time);
	void transmit(const bool * stop_signal = nullptr);
	void stop_transmission();

	void start_reception(double settling_time);
	void receive(const bool * stop_signal = nullptr);
	void stop_reception();

	void set_pps_source();
	void set_time_sync();
	bool check_ref_lock();
	bool check_lo_lock();
	
	u220_config_t get_board_config() const { return board_config; }
	
	// uhd::usrp::multi_usrp::sptr get_usrp() const { return usrp; }
	// uhd::rx_streamer::sptr get_rx_stream() const { return rx_stream; }
	// uhd::tx_streamer::sptr get_tx_stream() const { return tx_stream; }
	
	std::string get_serial() const { return serial; }

	double get_tx_rate() const { return usrp ? usrp->get_tx_rate() : 0; }
	double get_tx_freq_for_ch(size_t channel) const { return usrp ? usrp->get_tx_freq(channel) : 0; }
	double get_tx_gain_for_ch(size_t channel) const { return usrp ? usrp->get_tx_gain(channel) : 0; }
	const std::vector<std::complex<float>*>& get_tx_buffers() const noexcept {
		return tx_buffer_ptrs;
	}
	
	double get_rx_rate() const { return usrp ? usrp->get_rx_rate() : 0; }
	double get_rx_freq_for_ch(size_t channel) const { return usrp ? usrp->get_rx_freq(channel) : 0; }
	double get_rx_gain_for_ch(size_t channel) const { return usrp ? usrp->get_rx_gain(channel) : 0; }
	const std::vector<std::complex<float>*>& get_rx_buffers() const noexcept {
		return rx_buffer_ptrs;
	}

	void set_serial(const std::string & ser);

	void set_tx_gain(double new_gain);
	void set_rx_gain(double new_gain);
	void set_frequency(double new_freq);
};

#endif // U220_HPP