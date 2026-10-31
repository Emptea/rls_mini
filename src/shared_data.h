#ifndef shared_data_H
#define shared_data_H

#include "axi_dsp.h"
#include "cpm_cit.h"
#include "cpm_poi.h"
#include "cpm_rr.h"
#include "fpga_dma.hpp"
#include "imitator_u220.h"
#include "mini_config.hpp"
#include "rls.hpp"
#include "shared_data_eth.h"
#include "techlaser.h"
#include "u220.hpp"
#include "u220_group.h"
#include "uhd_utils.hpp"
#include "user_u220.h"

#include <array>
#include <pivaluetree.h>

#define GLOBAL (GlobalData::instance())

class GlobalData
	: public PIObject
	, public GlobalDataEth {
	PIOBJECT(GlobalData)

private:
	PISystemTime t_start;
	PISystemTime t_end;
	bool first_transfer = false;
	MiniConfig cfg;

public:
	const PIString u220_args =
		"recv_frame_size=16360,num_recv_frames=64,type=b200,enable_user_regs,fpga=/home/ubuntu/rls_mini/bin/antsdr_u220.bin";
	UHD_UTILS uhd_utils{"type=b200"};
	U220Group u220_group;
	ImitatorU220 imitator_u220;
	fpga_dma dma;
	// Retained after stop; index = 2 * board_index + local channel.
	std::array<VectorComplexS, 8> dump_channels;

	const u220_config_t u220_config = {
		.rate       = 5e6,
		.freq       = 3.6e9,
		.tx_gain    = {70,   70  },
		.rx_gain    = {0,    0   },
		.tx_bw      = {56e6, 56e6},
		.rx_bw      = {56e6, 56e6},
		.ref        = "internal",
		.cpu_format = "sc16",
		.otw_format = "sc16",
		.pps        = "internal",
		.tx_spb     = 545200,
		.rx_spb     = 4640 + 40  //  if you want to print hdr add 40 more samples to buf
	};

	static GlobalData * instance();

	void init();
	bool sync();
	void start();
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

	Protocol_RLS_Mini::POI_Kvit set_POI_Kvit();
	void received_POI_Zapros(const Protocol_RLS_Mini::POI_Zapros & msg);
	void received_POI_Shtat(const Protocol_RLS_Mini::POI_Shtat & msg);
	void received_POI_SDC(const Protocol_RLS_Mini::POI_SDC & msg);
	void received_POI_DSA(const Protocol_RLS_Mini::POI_DSA & msg);
	void received_POI_APU(const Protocol_RLS_Mini::POI_APU & msg);
	void received_POI_Zona(const Protocol_RLS_Mini::POI_Zona & msg);
	void received_POI_Kan(const Protocol_RLS_Mini::POI_Kan & msg);

	Protocol_RLS_Mini::PI set_PI();

	Protocol_RLS_Mini::CIT_Kvit set_CIT_Kvit();
	void received_CIT_Zapros(const Protocol_RLS_Mini::CIT_Zapros & msg);
	void received_CIT_KU(const Protocol_RLS_Mini::CIT_KU & msg);

protected:

private:
	GlobalData();
	~GlobalData();

	void write_dump_hex(const char * filename) const;
	void u220_recv();
	void u220_recv_dma();
	void u220_recv_software();
	void process_work_pac(const uint32_t * buffer);

	PIValueTree main_config;
	PIProtectedVariable<PIMap<int, VectorComplexS>> current_channels;
	PIThreadNotifier notifier_channels;
	PIThread process_thread;
	PIThread u220_recv_thread;

	PIThread cpm_thread;
	void cpm_loop();

	PIProtectedVariable<PIMap<int, VectorComplexS>> adc_channels;
	VectorComplexS zero_vector;

	double time;
	PIProtectedVariable<PIVector<uint32_t>> dma_channel_buf;
	bool req_test_point       = false;
	uint16_t req_test_channel = 0;

	Techlaser techlaser;
	CpmRr cpm_rr;
	CpmPoi cpm_poi;
	CpmCit cpm_cit;

	struct target {
		uint32_t n   = 0; // номер цели
		double time  = 0; // время обнаружения
		float az     = 0; // азимут цели. град
		float um     = 0; // Угол места, град
		float d      = 0; // Дальность, м
		double amp   = 0; // Амплитуда сигнала цели, в ед АЦП
		double porog = 0; // Порог обнаружения, в ед АЦП
		float vr     = 0; // Радиальная скорость, 1 м/с
	};

	void send_KTA_VO(struct target targ);
	void send_PI();
};

#endif
