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
		.rate    = 5e6,
		.freq    = 3.6e9,
		.tx_gain = {0,    0   },
		.rx_gain = {0,    0   },
		.tx_bw   = {56e6, 56e6},
		.rx_bw   = {56e6, 56e6},
		.ref     = "internal",
		.cpu_format     = "sc16",
		.otw_format     = "sc16",
		.pps     = "internal",
		.tx_spb  = 0,
		.rx_spb  = 0
    };
	const PIString u220_args = "recv_frame_size=4096,num_recv_frames=128,send_frame_size=8192,num_send_frames=256";

	PIVector<size_t> active_boards;
	static GlobalData * instance();

	void init();
	void start();
	void stop();

	const PIValueTree & mainConfig() const { return main_config; }



protected:

private:
	GlobalData();
	~GlobalData();

	PIValueTree main_config;
};

#endif
