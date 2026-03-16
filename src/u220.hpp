#ifndef U220_HPP
#define U220_HPP

#include <piprotectedvariable.h>
#include <pithread.h>
#include <stdint.h>
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
	PIString ref;
	PIString cpu_format;
	PIString otw_format;
	PIString pps;
	size_t tx_spb;
	size_t rx_spb;
} u220_config_t;

struct u220_stats {
	size_t cycles_completed    = 0;

	// RX errors
	size_t rx_overflows        = 0;
	size_t rx_timeouts         = 0;
	size_t rx_late_commands    = 0;
	size_t rx_broken_chains    = 0;
	size_t rx_alignment_errors = 0;
	size_t rx_bad_packets      = 0;
	size_t rx_packet_cnt       = 0;

	// TX errors
	size_t tx_underruns        = 0;
	size_t tx_seq_errors       = 0;
	size_t tx_late_commands    = 0;
	size_t tx_packet_cnt       = 0;
};

struct u220_status {
	bool on       = false;
	bool rx_on[2] = {false, false};
	bool tx_on[2] = {false, false};
};

void print_config(const u220_config_t & config);

class U220 {
private:
	uhd::usrp::multi_usrp::sptr usrp;

	uhd::rx_streamer::sptr rx_stream;
	PIVector<PIVector<complexf>> rx_buffer;
	PIProtectedVariable<PIQueue<PIVector<complexf>>> rx_queue[2];
	PIVector<complexf *> rx_buffer_ptrs;
	uhd::rx_metadata_t rx_metadata;
	double rx_timeout;
	uhd::stream_cmd_t rx_stream_cmd;

	uhd::tx_streamer::sptr tx_stream;
	PIVector<complexf> tx_buffer;
	PIVector<complexf *> tx_buffer_ptrs;
	uhd::tx_metadata_t tx_metadata;

	// Configuration parameters
	PIString serial;
	PIString device_args;

	u220_config_t user_config;
	u220_config_t board_config;

	u220_stats stats;
	u220_status status;

	void fill_buffer_with_wavetable(PIVector<complexf> & buffer);
	void initialize_usrp();
	void configure_tx_channel(size_t channel);
	void configure_rx_channel(size_t channel);
	void setup_tx_streamer();
	void setup_rx_streamer();
	void rx_errors_worker(uhd::rx_metadata_t::error_code_t err);

protected:
	PIThread tx_thread;
	PIThread rx_thread;

public:
	U220(const PIString & serial = "",
	     const PIString & args   = "recv_frame_size=4096,num_recv_frames=128,send_frame_size=8192,num_send_frames=512",
	     uint64_t num_samps      = 0,
	     u220_config_t config    = {
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
	void transmit();
	void stop_transmission();

	void start_reception(double settling_time);
	void receive();
	void stop_reception();

	void set_pps_source();
	void set_time_sync();
	bool check_ref_lock();
	bool check_lo_lock();

	u220_config_t get_board_config() const { return board_config; };

	PIString get_serial() const { return serial; };

	double get_tx_rate() const { return usrp ? usrp->get_tx_rate() : 0; };
	double get_tx_freq_for_ch(size_t channel) const { return usrp ? usrp->get_tx_freq(channel) : 0; };
	double get_tx_gain_for_ch(size_t channel) const { return usrp ? usrp->get_tx_gain(channel) : 0; };

	double get_rx_rate() const { return usrp ? usrp->get_rx_rate() : 0; };
	double get_rx_freq_for_ch(size_t channel) const { return usrp ? usrp->get_rx_freq(channel) : 0; };
	double get_rx_gain_for_ch(size_t channel) const { return usrp ? usrp->get_rx_gain(channel) : 0; };
	PIVector<complexf> take_rx_queue_and_clear(int index);
	PIVector<complexf> take_rx_queue(int index);
	PIVector<complexf> get_rx_queue(int index);

	void set_serial(const PIString & ser);

	void set_tx_gain(double new_gain);
	void set_rx_gain(double new_gain);
	void set_frequency(double new_freq);

	void print_status(const struct u220_status & status) {
		piCout << serial << " status";
		piCout << "  on:       " << (status.on ? "true" : "false") << "\n";
		piCout << "  rx_on:   [" << (status.rx_on[0] ? "true" : "false") << ", " << (status.rx_on[1] ? "true" : "false") << "]\n";
		piCout << "  tx_on:   [" << (status.tx_on[0] ? "true" : "false") << ", " << (status.tx_on[1] ? "true" : "false") << "]\n";
	}
};

#endif // U220_HPP