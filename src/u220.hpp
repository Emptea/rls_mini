#ifndef U220_HPP
#define U220_HPP

#include "../rls_fpga/dma-proxy.h"
#include "rlso_const.hpp"
#include "rx_trace.hpp"

#include <atomic>
#include <piprotectedvariable.h>
#include <pithread.h>
#include <stdint.h>
#include <uhd/exception.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/utils/thread.hpp>

#define PRINT_U220_STATS(s) piCout << "Cycles:" << s.cycles_completed << " RX:" << s.rx_packet_cnt << "(" << s.rx_bad_packets << "err)";
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
	uint64_t rx_recv_total_us  = 0;
	uint64_t rx_recv_max_us    = 0;
	uint32_t rx_chunk_counter  = 0;

	// TX errors
	size_t tx_underruns        = 0;
	size_t tx_seq_errors       = 0;
	size_t tx_late_commands    = 0;
	size_t tx_packet_cnt       = 0;
};

struct uhd_recv_diagnostics {
	using Clock                   = std::chrono::steady_clock;

	Clock::time_point last_report = Clock::now();
	Clock::time_point last_recv{};

	bool first_recv                                      = true;

	uint64_t calls                                       = 0;
	uint64_t samples                                     = 0;

	uint64_t recv_total_us                               = 0;
	uint64_t recv_max_us                                 = 0;

	uint64_t interval_total_us                           = 0;
	uint64_t interval_max_us                             = 0;

	uint64_t partial_reads                               = 0;
	uint64_t zero_reads                                  = 0;
	uint64_t overflows                                   = 0;

	double rx_lag_last_ms                                = 0.0;
	double rx_lag_max_ms                                 = 0.0;
	double rx_lag_min_ms                                 = 0.0;
	double rx_lag_total_ms                               = 0.0;

	uint64_t rx_lag_count                                = 0;

	std::chrono::steady_clock::time_point last_lag_check = std::chrono::steady_clock::now();

	void reset();
	void print(const PIString & serial);
};

struct u220_status {
	bool on       = false;
	bool rx_on[2] = {false, false};
	bool tx_on[2] = {false, false};
};

void print_config(const u220_config_t & config);

class UhdTimeSynchronizer;
class U220: public PIObject {
	PIOBJECT(U220)

private:
	uhd::usrp::multi_usrp::sptr usrp;
	uhd_recv_diagnostics recv_diag;

	TraceBuffer<RxTraceEntry, 32768> rx_trace;
	TraceBuffer<TimeSyncTraceEntry, 8192> time_sync_trace;


	uint64_t rx_iteration = 0;

	std::chrono::steady_clock::time_point previous_recv_end{};
	bool have_previous_recv = false;

	double last_sample_time = std::numeric_limits<double>::quiet_NaN();

	// Привязка времени USRP к steady_clock
	std::chrono::steady_clock::time_point device_time_reference_host{};
	double device_time_reference_usrp = 0.0;
	bool have_device_time_reference   = false;

	uhd::rx_streamer::sptr rx_stream;
	PIVector<VectorComplexS> rx_buffer;
	PIProtectedVariable<PIQueue<VectorComplexS>> rx_queue[2];
	PIVector<complexs *> rx_buffer_ptrs[TX_BUFFER_COUNT];
	std::atomic_int active_buffer_idx{0};
	uhd::rx_metadata_t rx_metadata;
	size_t rx_samples = 0;
	double rx_timeout;
	float rx_burst_pkt_time;
	uhd::stream_cmd_t rx_stream_cmd;

	uhd::tx_streamer::sptr tx_stream;
	VectorComplexS tx_buffer;
	PIVector<complexs *> tx_buffer_ptrs;
	uhd::tx_metadata_t tx_metadata;

	// Configuration parameters
	PIString serial;
	PIString device_args;

	u220_config_t user_config;
	u220_config_t board_config;

	u220_stats stats;
	u220_status status;

	bool shutdown_done                       = false;
	UhdTimeSynchronizer * time_synchronizer_ = nullptr;
	size_t last_packet_number = 0;

	void fill_buffer_with_wavetable(VectorComplexS & buffer);
	void initialize_usrp();
	void configure_tx_channel(size_t channel);
	void configure_rx_channel(size_t channel);
	void setup_tx_streamer();
	void setup_rx_streamer();
	void setup_rx_streamer(void ** tx0_buf, void ** tx1_buf);
	void rx_errors_worker(uhd::rx_metadata_t::error_code_t err);

protected:
	PIThread sync_thread;
	PIThread tx_thread;
	PIThread rx_thread;

public:
	std::vector<RxTraceEntry> get_rx_trace() const { return rx_trace.snapshot(); }
	U220(const PIString & serial = "",
	     const PIString & args   = "recv_frame_size=4104,num_recv_frames=128,send_frame_size=8192,num_send_frames=512",
	     uint64_t num_samps      = 0,
	     u220_config_t config    = {
             5e6,
             3.6e9,
             {0.0},
             {0.0},
             {56e6, 56e6},
             {56e6, 56e6},
             "",
             "sc16",
             "sc16",
             "internal",
             0,
             0
    });

	~U220();

	void init();
	void setup(void ** tx0_buf, void ** tx1_buf);
	void setup();

	void start_sync();
	bool sync();

	void transmit();
	void stop_transmission();

	void start_reception(double settling_time);
	uhd::rx_metadata_t::error_code_t receive(const int active_buffer_idx);
	void stop_and_drain_rx();
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
	VectorComplexS take_rx_queue_and_clear(int index);
	VectorComplexS take_rx_queue(int index);
	VectorComplexS get_rx_queue(int index);

	void set_serial(const PIString & ser);

	void set_tx_gain(double new_gain);
	void set_rx_gain(double new_gain);
	void set_frequency(double new_freq);

	struct u220_status get_status() { return status; };
	struct u220_stats get_stats() { return stats; };
	void print_status() {
		piCout << serial << " status";
		piCout << "  on:       " << (status.on ? "true" : "false") << "\n";
		piCout << "  rx_on:   [" << (status.rx_on[0] ? "true" : "false") << ", " << (status.rx_on[1] ? "true" : "false") << "]\n";
		piCout << "  tx_on:   [" << (status.tx_on[0] ? "true" : "false") << ", " << (status.tx_on[1] ? "true" : "false") << "]\n";
	}

	void mcs_stage1();
	void mcs_stage2();
	void mcs_finish();
	void enable_external_lo();
	void ensm_to_alert();
	void ensm_to_fdd();
	void ensm_enable_external();
	uint8_t get_ensm_state();

	uhd::usrp::multi_usrp::sptr get_usrp() { return usrp; }

	size_t get_rx_samples() const { return rx_samples; }
	const complexs * get_rx_buffer(int buffer_idx, int channel) const { return rx_buffer_ptrs[buffer_idx][channel]; }
	std::vector<TimeSyncTraceEntry> get_time_sync_trace() const { return time_sync_trace.snapshot(); }
	void set_time_synchronizer(UhdTimeSynchronizer * synchronizer) { time_synchronizer_ = synchronizer; }
};

#endif // U220_HPP
