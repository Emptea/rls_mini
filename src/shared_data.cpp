#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "imitator_u220.h"
#include "mini_config.hpp"
#include "protocol_rls_mini.h"
#include "user_u220.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), cfg(), u220_group(cfg), imitator_u220(u220_group) {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_group.add_board(u);
	}

	process_thread.start([this] { processChannels(); });
}


GlobalData::~GlobalData() {}


GlobalData * GlobalData::instance() {
	static GlobalData ret;
	return &ret;
}

void GlobalData::init() {
	initEth();
	cpm_rr.on_state_changed = [this]() { global->sendMessage(set_RR_Kvit()); };
	const auto & com_conf   = main_config.child("com");
	techlaser.open(com_conf.childValue("techlaser").toString());
	if (cfg.dma_enabled) {
		if (dma.init(cfg.rx_insert_count) != 0) {
			fprintf(stderr, "Failed to initialize FPGA DMA\n");
			cfg.dma_enabled = false;
		}
	}
	cpm_poi.set_dma_enabled(cfg.dma_enabled);
	zero_vector.resize(232 * 3, {0, 0});
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	void * tx0_buf[TX_BUFFER_COUNT];
	void * tx1_buf[TX_BUFFER_COUNT];


	uint8_t ispr_kan = 0;
	for (size_t i = 0; i < u220_group.board_count(); ++i) {
		if (dit != devices.end() && StdString2PIString(dit->first) == u220_group.board(i)->get_serial()) {
			u220_group.activate_board(i);
			++dit;
			ispr_kan |= (1 << (2 * i)) | (1 << (2 * i + 1));
		}
	}
	cpm_rr.set_ispr_kan(ispr_kan);

	if (u220_group.active_boards().isEmpty()) {
		piCout << "No active U220 boards";
		t_start = PISystemTime::current();
		return;
	}

	for (const auto board_index: u220_group.active_boards()) {
		if (cfg.dma_enabled) {
			dma.get_all_tx_buffers(2 * board_index, tx0_buf);
			dma.get_all_tx_buffers(2 * board_index + 1, tx1_buf);

			u220_group.board(board_index)->setup(tx0_buf, tx1_buf);
		} else {
			u220_group.board(board_index)->setup();
		}
	}

	u220_group.setup();
	u220_group.sync_ad9361();
	// нужна задержка чтобы pps сбросился иначе в reset_time не сработает
	2_s .sleep();
	u220_group.reset_time(0x05DC);

	if (cfg.dma_enabled) {
		axi_dsp_init();
		axi_dsp_configure();
		axi_dsp_set_expect_u220_del(cfg.rx_insert_count);
		axi_dsp_set_output_source(1, 0, 0);
		auto v = axi_dsp_get_output_source();
		piCout << "SOURCE: " << v.SOURCE << ", SOURCE_CHANNEL: " << v.SOURCE_CHANNEL << ", RANGE_GATE: " << v.RANGE_GATE;
		axi_dsp_set_channel_mask((uint32_t)cpm_rr.get_ispr_kan());
		axi_dsp_apply();
	}
	first_transfer = false;
}

void GlobalData::start() {
	startEth();
	cpm_rr.start();

	if (u220_group.active_boards().isEmpty()) {
		return;
	}

	for (const auto board_index: u220_group.active_boards()) {
		// u220_group.board(board_index)->start_reception(4.64+180*0.2e-6);
		u220_group.board(board_index)->set_time_sync();
		double start_time = 0.05;
		u220_group.fpga(board_index)->set_play_rx_enable(1);
		u220_group.board(board_index)->start_reception(start_time);
	}

	// 0.1_s .sleep();

	u220_recv_thread.start([this] {
		u220_recv();
		if (u220_recv_thread.isStopping()) u220_group.stop_and_drain_rx();
	});

	piCout << "Imitator init";

	std::array<ImitatorU220::TargetParams, 4> targets{};

	for (size_t i = 0; i < 4; ++i) {
		targets[i] = {cfg.target_az[i], cfg.target_um[i], cfg.target_dist[i], cfg.target_vel[i], cfg.target_osl[i]};
	}

	imitator_u220.set_params(targets);
	imitator_u220.set_opu(cfg.opu_az, 180.0);
	imitator_u220.apply(0.0);

	auto start_time = u220_group.board(u220_group.active_boards()[0])->get_usrp()->get_time_now();
	piCout << "TX start at" << start_time.get_full_secs();

	for (const auto board_index: u220_group.active_boards()) {
		u220_group.fpga(board_index)->set_play_start_tx();
	}
}


void GlobalData::stop() {
	cpm_rr.stop();
	u220_recv_thread.stopAndWait();
	process_thread.stop();          // mark thread for stop
	notifier_channels.notify();     // notify thread
	process_thread.waitForFinish(); // wait for thread really finish
	t_end = PISystemTime::current();

	if (cfg.dma_enabled) {
		while (dma.get_completed() < dma.get_submitted()) {
			if (dma.receive() != 0) break;
		}
	}

	stopEth();

	for (const auto board_index: u220_group.active_boards()) {
		u220_group.board(board_index)->stop_reception();
		u220_group.board(board_index)->stop_transmission();
	}

	for (size_t i: u220_group.active_boards()) {
		u220_group.fpga(i)->set_play_stop_tx();
	}

	for (auto & channel: dump_channels) {
		channel.clear();
	}
	for (size_t i: u220_group.active_boards()) {
		u220_group.fpga(i)->set_play_stop_tx();
		for (int channel = 0; channel < 2; ++channel) {
			const size_t global_channel = 2 * i + channel;
			if (global_channel < dump_channels.size()) {
				dump_channels[global_channel] = u220_group.board(i)->take_dump_queue_and_clear(channel);
			}
		}
	}
	write_dump_hex("rx_dump.hex");
	const auto cycles_completed =
		u220_group.active_boards().isEmpty() ? 0 : u220_group.board(u220_group.active_boards()[0])->get_stats().cycles_completed;

	if (cfg.dma_enabled) {
		axi_dsp_deinit();
	}


	piCout << "====";

	if (!u220_group.active_boards().isEmpty()) {
		const size_t i        = u220_group.active_boards()[0];
		auto cycles_completed = u220_group.board(i)->get_stats().cycles_completed;
		piCout << "U220 cycles completed:" << cycles_completed;
		if (cycles_completed > 0) {
			piCout << "Mean: transfer time = " << (t_end - t_start) / cycles_completed;
		}
	}

	piCout << "====";

	if (cfg.dma_enabled) {
		dma.cleanup();
	}
}

void GlobalData::write_dump_hex(const char * filename) const {
	std::ofstream file(filename, std::ios::binary | std::ios::trunc);
	if (!file) {
		piCout << "Cannot open RX dump:" << filename;
		return;
	}
	size_t count = 0;
	for (const auto & channel: dump_channels)
		count = std::max(count, static_cast<size_t>(channel.size()));
	constexpr char hex[] = "0123456789abcdef";
	std::array<char, 65> line{};
	line[64] = '\n';
	for (size_t sample = 0; sample < count; ++sample) {
		size_t pos = 0;
		for (int channel = 7; channel >= 0; --channel) {
			uint16_t parts[2]{};
			if (sample < static_cast<size_t>(dump_channels[channel].size())) {
				// sc16 buffer layout is real int16 followed by imaginary int16.
				static_assert(sizeof(complexs) == sizeof(parts), "Expected packed sc16 samples");
				std::memcpy(parts, &dump_channels[channel][sample], sizeof(parts));
			}
			for (int part: {1, 0}) {
				for (int shift = 12; shift >= 0; shift -= 4)
					line[pos++] = hex[(parts[part] >> shift) & 0xF];
			}
		}
		file.write(line.data(), line.size());
	}
	file.close();
	if (!file) piCout << "Failed writing RX dump:" << filename;
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave
	if (!cfg.dma_enabled) return;
	const auto * buffer = static_cast<const uint32_t *>(dma.get_rx_buffer());
	if (buffer) {
		const auto * hdr   = reinterpret_cast<const rls::header *>(buffer);
		const size_t words = std::min<size_t>(rls::get_rx_words_per_buf(hdr->tp), BUFFER_SIZE / sizeof(uint32_t));
		auto ref           = dma_channel_buf.getRef();
		*ref               = PIVector<uint32_t>(buffer, words);
	}
}

void GlobalData::u220_recv() {
	const size_t first_board = u220_group.active_boards()[0];
	auto packet_cnt          = u220_group.board(first_board)->get_stats().rx_packet_cnt;
	auto u220_cycles         = u220_group.board(first_board)->get_stats().cycles_completed;

	if (!u220_cycles) {
		t_start = PISystemTime::current();
	}

	if (dma.can_send()) {
		for (const auto board_index: u220_group.active_boards()) {
			u220_group.board(board_index)->receive();
		}
		// }
		int ret = dma.send();
		if (ret != 0) {
			fprintf(stderr, "TX ERROR transaction=%zu ret=%d\n", dma.get_submitted() - 1, ret);
		}
		return;
	}

	int ret = dma.receive();
	if (ret != 0) {
		fprintf(stderr, "RX ERROR transaction=%zu ret=%d\n", dma.get_completed(), ret);
		u220_recv_thread.stopAndWait();
	}

	if (u220_group.active_boards().isEmpty()) return;


	if (packet_cnt > 0 && packet_cnt % ((SAMPLES_PER_CYCLE + 2) * 20 * 5000) == 0) {
		for (const auto board_index: u220_group.active_boards()) {
			auto * board = u220_group.board(board_index);
			auto ref1    = current_channels.getRef();
			auto ref2    = adc_channels.getRef();

			for (int channel: {0, 1}) {
				const int global_channel = 2 * board_index + channel;
				(*ref1)[global_channel] = (*ref2)[global_channel] = board->take_rx_queue_and_clear(channel);
			}
		}
	}
	if (u220_cycles > 0) {
		notifier_channels.notify();
	}
}

static constexpr rls::tp map_kt_to_dsp(uint16_t control_point) {
	switch (control_point) {
	case Protocol_RLS_Mini::CTRL: return rls::TP_WORK;
	case Protocol_RLS_Mini::PHD: return rls::TP_BYPASS;
	case Protocol_RLS_Mini::FAP: return rls::TP_FAPCH;
	case Protocol_RLS_Mini::LOU: return rls::TP_LOU;
	case Protocol_RLS_Mini::SF: return rls::TP_SF;
	case Protocol_RLS_Mini::KN: return rls::TP_MAX;
	case Protocol_RLS_Mini::AD: return rls::TP_FFT;
	case Protocol_RLS_Mini::APU: return rls::TP_APU;
	case Protocol_RLS_Mini::CUT: return rls::TP_CUT;
	case Protocol_RLS_Mini::DDR: return rls::TP_DDR;
	case Protocol_RLS_Mini::FIND: return rls::TP_FIND;
	case Protocol_RLS_Mini::RANK: return rls::TP_RANK;
	case Protocol_RLS_Mini::FAP_COEFFS: return rls::TP_FAPCH_COEFFS;
	case Protocol_RLS_Mini::WEIGHT_OUT: return rls::TP_WEIGHT_OUT;
	case Protocol_RLS_Mini::MTI: return rls::TP_MTI;
	default: return rls::TP_WORK;
	}
}


void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	piCout << "rec msg" << "received_POI_TK_Zapros";
	piCout << msg;
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	// piCout << "rec msg kt" << msg.kt;
	const auto test_point = map_kt_to_dsp(msg.kt);
	if (cfg.dma_enabled) {
		axi_dsp_set_output_source(test_point, msg.nkan, msg.reg_takt);
		axi_dsp_apply();
	}
	req_test_channel = msg.kt;
	req_test_point   = true;

	int data_cnt     = 0;
	if (msg.kt != Protocol_RLS_Mini::CTRL) {
		data_cnt = rls::get_rx_words_per_buf(test_point, cfg.rx_insert_count);
	} else {
		// TODO get all dsp regs state and pack them into one packet
	}

	const auto set_data = [&](const auto & data, size_t count) {
		if (data.isEmpty()) {
			ans.setData(zero_vector);
			return;
		}
		ans.setData(data, std::min(data.size(), count));
	};

	if (cfg.dma_enabled) {
		PIVector<uint32_t> data;
		{
			auto ref = dma_channel_buf.getRef();
			data     = *ref;
		}
		set_data(data, data_cnt);
	} else if (test_point == rls::TP_BYPASS) {
		VectorComplexS data;
		{
			auto ref = adc_channels.getRef();
			data     = (*ref)[msg.nkan];
		}
		data_cnt = (232 + 2 * cfg.rx_insert_count) * 3;
		set_data(data, data_cnt);
	} else {
		data_cnt = 232;
		ans.setData(zero_vector);
	}

	ans.nw = static_cast<uint16_t>(data_cnt);
	global->sendMessage(ans);
}

Protocol_RLS_Mini::RR_Kvit GlobalData::set_RR_Kvit() {
	Protocol_RLS_Mini::RR_Kvit kvit;

	kvit.kuvr     = static_cast<uint8_t>(cpm_rr.get_kuvr());
	kvit.vr       = static_cast<uint8_t>(cpm_rr.get_vr());

	kvit.kuizl    = cpm_rr.get_kuizl();
	kvit.izl      = cpm_rr.get_izl();
	kvit.ant      = cpm_rr.get_ant();

	kvit.ispr_kan = cpm_rr.get_ispr_kan();

	kvit.setDegreesDaz(cpm_rr.get_daz());
	kvit.dd_poi = static_cast<int16_t>(cpm_rr.get_dd_poi());
	kvit.setDegreesDazPOI(cpm_rr.get_daz_poi());

	piCout << kvit;

	return kvit;
}

void GlobalData::received_RR_Zapros(const Protocol_RLS_Mini::RR_Zapros & msg) {
	piCout << "rec msg" << "received_RR_Zapros    ";
	piCout << msg;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg) {
	piCout << "rec msg" << "received_RR_Vr        ";
	piCout << msg;
	cpm_rr.set_vr(static_cast<CpmRr::RotationCommand>(msg.par));
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	piCout << "rec msg" << "received_RR_Izl       ";
	piCout << msg;
	cpm_rr.set_izl(msg.par == 1);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Ant(const Protocol_RLS_Mini::RR_Ant & msg) {
	piCout << "rec msg" << "received_RR_Ant       ";
	piCout << msg;
	cpm_rr.set_ant(msg.par == 1);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_TTek(const Protocol_RLS_Mini::RR_TTek & msg) {
	piCout << "rec msg" << "received_RR_TTek      ";
	piCout << msg;
	cpm_rr.set_time(msg.getSeconds());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr(const Protocol_RLS_Mini::RR_AzPopr & msg) {
	piCout << "rec msg" << "received_RR_AzPopr    ";
	piCout << msg;
	cpm_rr.set_daz(msg.getDegrees());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_DPopr_POI(const Protocol_RLS_Mini::RR_DPopr_POI & msg) {
	piCout << "rec msg" << "received_RR_DPopr_POI ";
	piCout << msg;
	cpm_rr.set_dd_poi(msg.dd);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr_POI(const Protocol_RLS_Mini::RR_AzPopr_POI & msg) {
	piCout << "rec msg" << "received_RR_AzPopr_POI";
	piCout << msg;
	cpm_rr.set_daz_poi(msg.getDegrees());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::send_KTA_VO(struct target targ) {
	Protocol_RLS_Mini::KTA_VO ans;
	ans.az = targ.az;
	ans.setDegreesUm(targ.um);
	ans.D     = targ.D;
	ans.porog = targ.porog;
	ans.sp    = targ.sp;
	ans.amp   = targ.amp;
	ans.vr    = targ.vr;
	ans.dum   = targ.dum;
	ans.tn    = targ.tn;
	global->sendMessage(ans);
}


void GlobalData::send_PI() {
	Protocol_RLS_Mini::PI ans;

	global->sendMessage(ans);
}


Protocol_RLS_Mini::POI_Kvit GlobalData::set_POI_Kvit() {
	Protocol_RLS_Mini::POI_Kvit kvit;

	kvit.sdc      = cpm_poi.get_sdc();
	kvit.dsa      = cpm_poi.get_dsa();
	kvit.kan      = cpm_poi.get_kan();

	kvit.dsa_vr_n = cpm_poi.get_dsa_vr_n();
	kvit.dsa_vr_k = cpm_poi.get_dsa_vr_k();

	kvit.setLevelApuk1(cpm_poi.get_apu_k1());
	kvit.setLevelApuk2(cpm_poi.get_apu_k2());

	kvit.zona_k1 = cpm_poi.get_zona_k1();
	kvit.zona_k2 = cpm_poi.get_zona_k2();

	return kvit;
}


void GlobalData::received_POI_Zapros(const Protocol_RLS_Mini::POI_Zapros & msg) {
	piCout << "rec msg" << "received_POI_Zapros   ";
	piCout << msg;
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Shtat(const Protocol_RLS_Mini::POI_Shtat & msg) {
	piCout << "rec msg" << "received_POI_Shtat    ";
	piCout << msg;
	cpm_poi.set_shtat();
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_SDC(const Protocol_RLS_Mini::POI_SDC & msg) {
	piCout << "rec msg" << "received_POI_SDC      ";
	piCout << msg;
	cpm_poi.set_sdc(msg.par == 1);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_DSA(const Protocol_RLS_Mini::POI_DSA & msg) {
	piCout << "rec msg" << "received_POI_DSA      ";
	piCout << msg;
	cpm_poi.set_dsa(msg.par == 1, msg.vr_n, msg.vr_k);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_APU(const Protocol_RLS_Mini::POI_APU & msg) {
	piCout << "rec msg" << "received_POI_APU      ";
	piCout << msg;
	cpm_poi.set_apu(msg.getLevelk1(), msg.getLevelk2());
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Zona(const Protocol_RLS_Mini::POI_Zona & msg) {
	piCout << "rec msg" << "received_POI_Zona     ";
	piCout << msg;
	cpm_poi.set_zona(msg.k1, msg.k2);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Kan(const Protocol_RLS_Mini::POI_Kan & msg) {
	piCout << "rec msg" << "received_POI_Kan      ";
	piCout << msg;
	cpm_poi.set_kan(msg.kan);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}
