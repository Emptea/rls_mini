#ifndef shared_data_H
#define shared_data_H

#include "dma_channel.hpp"
#include "rlso_const.hpp"
#include "shared_data_eth.h"
#include "u220.hpp"
#include "uhd_utils.hpp"

#include <cstdint>
#include <pivaluetree.h>


#define GLOBAL (GlobalData::instance())

class GlobalData
	: public PIObject
	, public GlobalDataEth {
	PIOBJECT(GlobalData)

public:
	UHD_UTILS uhd_utils;
	PIVector<U220 *> u220_ptrs;

	const u220_config_t u220_config = {
		.rate       = 5e6,
		.freq       = 3.6e9,
		.tx_gain    = {70,   70  },
		.rx_gain    = {0,    0   },
		.tx_bw      = {56e6, 56e6},
		.rx_bw      = {56e6, 56e6},
		.ref        = "internal",
		.cpu_format = "sc16",
		.otw_format = "sc12",
		.pps        = "external",
		.tx_spb     = 545200,
		.rx_spb     = U220_SPB  // 4640
	};
	const PIString u220_args = "recv_frame_size=16360,num_recv_frames=64,send_frame_size=8192,num_send_frames=256";

	PIVector<size_t> active_boards;

	static GlobalData * instance();

	void init();
	bool sync();
	void start(double acquisition_seconds = 10.0);
	void stop();
	void processChannels();

	const PIValueTree & mainConfig() const { return main_config; }
	Protocol_RLS_Mini::RR_Kvit set_RR_Kvit();
	void received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg);
	void received_RR_Zapros(const Protocol_RLS_Mini::RR_Zapros & msg);
	void received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg);
	void received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg);
	void received_RR_Ant(const Protocol_RLS_Mini::RR_Ant & msg);
	void received_RR_TTek(const Protocol_RLS_Mini::RR_TTek & msg);
	void received_RR_AzPopr(const Protocol_RLS_Mini::RR_AzPopr & msg);
	void received_RR_DPopr_POI(const Protocol_RLS_Mini::RR_DPopr_POI & msg);
	void received_RR_AzPopr_POI(const Protocol_RLS_Mini::RR_AzPopr_POI & msg);

protected:

private:
	GlobalData();
	~GlobalData();

	void initDSP();
	void initDMAs();

	PIValueTree main_config;
	PIProtectedVariable<PIMap<int, VectorComplexS>> current_channels;
	PIThreadNotifier notifier_channels;
	PIThread process_thread;

	PIProtectedVariable<PIMap<int, VectorComplexS>> adc_channels;
	PIProtectedVariable<VectorUint> dma_channel_buf;
	VectorComplexS zero_vector;

	PIMap<int, bool> board_statuses;
	std::atomic_int dma_send_counter{0};

	union {
		uint8_t bits = 0;
		struct {
			uint8_t _reserve: 1;
			uint8_t ant     : 1;
			uint8_t izl     : 1;
			uint8_t kuizl   : 1;
			uint8_t vr      : 2;
			uint8_t kuvr    : 2;
		};
	} flags          = {0};
	uint8_t ispr_kan = 0;
	double daz       = 0;
	int16_t dd_poi   = 0; // 1 м
	double daz_poi   = 0;
	double time;
	bool req_test_point       = false;
	uint16_t req_test_channel = 0;

	dma_channel * dma_rx;
	void * dma_rx_buffers[RX_BUFFER_COUNT];

	// const PIString dma_rx_devnode                   = "/dev/dma_proxy_rx";
	const PIString dma_tx_devnodes[NUM_CHANNELS_TX] = {"/dev/dma_proxy_tx_ch0",
	                                                   "/dev/dma_proxy_tx_ch1",
	                                                   "/dev/dma_proxy_tx_ch2",
	                                                   "/dev/dma_proxy_tx_ch3",
	                                                   "/dev/dma_proxy_tx_ch4",
	                                                   "/dev/dma_proxy_tx_ch5",
	                                                   "/dev/dma_proxy_tx_ch6",
	                                                   "/dev/dma_proxy_tx_ch7"};
	dma_channel::ch_config rx_config                = {.devnode        = "/dev/dma_proxy_rx",
	                                                   .buffer_size    = BUFFER_SIZE,
	                                                   .buffer_count   = RX_BUFFER_COUNT,
	                                                   .channel_number = 0};
	dma_channel::ch_config tx_config                = {.buffer_size = BUFFER_SIZE, .buffer_count = TX_BUFFER_COUNT};
};

#endif
