#ifndef shared_data_H
#define shared_data_H

#include "shared_data_eth.h"
#include "u220.hpp"
#include "uhd_utils.hpp"

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
		.tx_spb     = 0,
		.rx_spb     = 0
    };
	const PIString u220_args = "recv_frame_size=4192,num_recv_frames=128,send_frame_size=8192,num_send_frames=256";

	PIVector<size_t> active_boards;
	static GlobalData * instance();

	void init();
	bool sync();
	void start();
	void stop();
	void processChannels();

	const PIValueTree & mainConfig() const { return main_config; }
	Protocol_RLS_Mini::RR_Kvit set_RR_Kvit();
	void received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg);
	void received_RR_Zapros       (const Protocol_RLS_Mini::RR_Zapros      & msg);
	void received_RR_Vr           (const Protocol_RLS_Mini::RR_Vr          & msg);
	void received_RR_Izl          (const Protocol_RLS_Mini::RR_Izl         & msg);
	void received_RR_Ant          (const Protocol_RLS_Mini::RR_Ant         & msg);
	void received_RR_TTek         (const Protocol_RLS_Mini::RR_TTek        & msg);
	void received_RR_AzPopr       (const Protocol_RLS_Mini::RR_AzPopr      & msg);
	void received_RR_DPopr_POI    (const Protocol_RLS_Mini::RR_DPopr_POI   & msg);
	void received_RR_AzPopr_POI   (const Protocol_RLS_Mini::RR_AzPopr_POI  & msg);
protected:

private:
	GlobalData();
	~GlobalData();

	PIValueTree main_config;
	PIProtectedVariable<PIMap<int, VectorComplexS>> current_channels;
	PIThreadNotifier notifier_channels;
	PIThread process_thread;

	PIProtectedVariable<PIMap<int, VectorComplexS>> adc_channels;
	VectorComplexS zero_vector;

	PIMap<int, bool> board_statuses;
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
	} flags = {0};
	uint8_t ispr_kan = 0;
	double daz      = 0;
	int16_t dd_poi   = 0; // 1 м
	double daz_poi  = 0;
	double time;
};

#endif
