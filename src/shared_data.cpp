#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "imitator_u220.h"
#include "mini_config.hpp"
#include "protocol_rls_mini.h"
#include "user_u220.h"

#include <algorithm>
#include <cmath>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)), cfg() {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_ptrs << u;
		user_u220_ptrs << nullptr;
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
	const auto & com_conf = main_config.child("com");
	techlaser.open(com_conf.childValue("techlaser").toString());
	if (cfg.dma_enabled) {
		if (dma.init() != 0) {
			fprintf(stderr, "Failed to initialize FPGA DMA\n");
			cfg.dma_enabled = false;
		}
	}
	zero_vector.resize(232 * 3, {0, 0});
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	void * tx0_buf[TX_BUFFER_COUNT];
	void * tx1_buf[TX_BUFFER_COUNT];

	for (size_t i = 0; i < u220_ptrs.size(); i++) {
		if (dit != devices.end() && StdString2PIString(dit->first) == u220_ptrs[i]->get_serial()) {
			active_boards.push_back(i);
			u220_ptrs[i]->init();
			user_u220_ptrs[i]     = new UserU220(u220_ptrs[i]->get_usrp());
			imitator_u220_ptrs[i] = new ImitatorU220(u220_ptrs[i]->get_usrp());
			dit++;
			ispr_kan |= (1 << 2 * i) | (1 << 2 * i + 1);
		}
	}

	for (const auto board_index: active_boards) {
		if (cfg.dma_enabled) {
			dma.get_all_tx_buffers(2 * board_index, tx0_buf);
			dma.get_all_tx_buffers(2 * board_index + 1, tx1_buf);
			u220_ptrs[board_index]->setup(tx0_buf, tx1_buf);
		} else {
			u220_ptrs[board_index]->setup();
		}
	}

	for (size_t i: active_boards) {
		const auto ver = user_u220_ptrs[i]->get_fpga_version();
		const auto ser = u220_ptrs[i]->get_serial();
		piCout << ser << PICoutManipulators::Hex << ver;
	}

	for (size_t i: active_boards) {
		user_u220_ptrs[i]->set_tx_dac_delay(cfg.tx_dac_delay[i * 2], cfg.tx_dac_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_tx_gainon_delay(cfg.tx_gpio_delay[i * 2], cfg.tx_gpio_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_rx_adc_delay(cfg.rx_adc_delay[i * 2], cfg.rx_adc_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_rx_en_delay(cfg.rx_en_delay[i * 2], cfg.rx_en_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_play_gpio_tx_enable(0b11);
		user_u220_ptrs[i]->set_play_gpio_rx_enable(0b11);
		user_u220_ptrs[i]->set_play_rx_insert_count(1);
		user_u220_ptrs[i]->set_tx_gainon_sg(0, cfg.tx_gainon_sg[i * 2]);
		user_u220_ptrs[i]->set_tx_gainon_sg(1, cfg.tx_gainon_sg[i * 2 + 1]);
		if (cfg.use_ext_file_sg[i * 2]) {
			user_u220_ptrs[i]->set_tx_dac_sg(0, cfg.tx_dac_sg[i * 2]);
		}
		if (cfg.use_ext_file_sg[i * 2 + 1]) {
			user_u220_ptrs[i]->set_tx_dac_sg(1, cfg.tx_dac_sg[i * 2 + 1]);
		}
		user_u220_ptrs[i]->set_tx_complex_gain(0, cfg.complex_gains[i * 2]);
		user_u220_ptrs[i]->set_tx_complex_gain(1, cfg.complex_gains[i * 2 + 1]);
	}

	enable_ad9361_external_ensm();
	generate_ad9361_cat_disable();
	1_s .sleep();
	sync_ad9361_mcs();
	generate_ad9361_cat_enable();
	print_ad9361_ensm_states();
	enable_external_lo_all();

	for (size_t i: active_boards) {
		user_u220_ptrs[i]->reset_time(0x05DC);
	}

	if (cfg.dma_enabled) {
		axi_dsp_init();
		axi_dsp_configure();
		axi_dsp_set_output_source(1, 0, 0);
		auto v = axi_dsp_get_output_source();
		piCout << "SOURCE: " << v.SOURCE << ", SOURCE_CHANNEL: " << v.SOURCE_CHANNEL << ", RANGE_GATE: " << v.RANGE_GATE << "\n";
		axi_dsp_set_channel_mask((uint32_t)ispr_kan);
		axi_dsp_apply();
	}
	first_transfer = false;
}


bool GlobalData::sync() {
	PISemaphore sem;
	PIVector<PIThread *> sync_threads;
	PIVector<bool> results(active_boards.size(), false);
	for (const auto board_index: active_boards) {
		auto * u  = u220_ptrs[board_index];
		// create thread with this functor
		// capture "i" and "u" as values, "sem" and "results" as reference (we want modify it)
		auto * st = new PIThread([board_index, u, &sem, &results] {
			sem.acquire();                    // wait for 1 resource from semaphore
			results[board_index] = u->sync(); // sync and store result to results by index
		});
		st->startOnce();    // start thread with up functor
		st->waitForStart(); // wait for thread actually starts
		sync_threads << st; // save for future delete
	}
	(100_ms).sleep();                   // ensure that all threads waits on "sem.acquire()"
	sem.release(sync_threads.size_s()); // release 4 resources (all threads, waits on "sem.acquire()", now go next)
	for (auto * t: sync_threads)        // wait for all thread to finish their functors
		t->stopAndWait();
	piDeleteAll(sync_threads); // delete threads


	return results.every([](bool r) { return r == true; }); // shortcut for check all items in "results" ( RTFM :-) )
}


void GlobalData::start() {
	startEth();
	for (const auto board_index: active_boards) {
		// u220_ptrs[board_index]->start_reception(4.64+180*0.2e-6);
		u220_ptrs[board_index]->set_time_sync();
		double start_time = 0.05;
		user_u220_ptrs[board_index]->set_play_rx_enable(1);
		u220_ptrs[board_index]->start_reception(start_time);
	}
	// 0.1_s .sleep();

	if (!active_boards.isEmpty()) {
		u220_recv_thread.start([this] {
			u220_recv();
			if (u220_recv_thread.isStopping()) u220_stop_and_drain_rx();
		});
	}
	t_start = PISystemTime::current();
	for (const auto board_index: active_boards) {
		user_u220_ptrs[board_index]->set_play_start_tx();
	}
}


void GlobalData::stop() {
	u220_recv_thread.stopAndWait();
	process_thread.stop();          // mark thread for stop
	notifier_channels.notify();     // notify thread
	process_thread.waitForFinish(); // wait for thread really finish
	stopEth();
	for (const auto board_index: active_boards) {
		u220_ptrs[board_index]->stop_reception();
		u220_ptrs[board_index]->stop_transmission();
	}

	for (size_t i: active_boards) {
		user_u220_ptrs[i]->set_play_stop_tx();
		// user_u220_ptrs[i]->reset_time(0)
	}

	piDeleteAllAndClear(u220_ptrs);
	active_boards.clear();
	if (cfg.dma_enabled) {
		axi_dsp_deinit();
	}
	t_end = PISystemTime::current();
	piCout << "====";
	auto cycles_completed = u220_ptrs[active_boards[0]]->get_stats().cycles_completed;
	if (cycles_completed > 0) {
		piCout << "Mean: transfer time = " << (t_end - t_start) / cycles_completed;
	}
	piCout << "====";
	if (cfg.dma_enabled) dma.cleanup();
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave
	if (!cfg.dma_enabled) return;
	while (!dma.can_send())
		;
	int ret = dma.send();
	if (ret != 0) {
		fprintf(stderr, "TX ERROR transaction=%zu ret=%d\n", dma.get_submitted() - 1, ret);
	}

	ret = dma.receive();
	if (ret != 0) {
		fprintf(stderr, "RX ERROR transaction=%zu ret=%d\n", dma.get_completed(), ret);
	}
	if (ret == 0) {
		const auto * buffer = static_cast<const uint32_t *>(dma.get_rx_buffer());
		if (buffer) {
			const auto * hdr   = reinterpret_cast<const rls::header *>(buffer);
			const size_t words = std::min<size_t>(rls::get_rx_words_per_buf(hdr->tp), BUFFER_SIZE / sizeof(uint32_t));
			auto ref           = dma_channel_buf.getRef();
			*ref               = PIVector<uint32_t>(buffer, words);
		}
	}
}

void GlobalData::u220_recv() {
	for (const auto board_index: active_boards) {
		u220_ptrs[board_index]->receive();
	}

	// int ret = dma.send();
	// if (ret != 0) {
	// 	fprintf(stderr, "TX ERROR transaction=%zu ret=%d\n", dma.get_submitted() - 1, ret);
	// }

	// if (!first_transfer) {
	// 	first_transfer = true;
	// } else {
	// 	int ret = dma.receive();
	// 	if (ret != 0) {
	// 		fprintf(stderr, "RX ERROR transaction=%zu ret=%d\n", dma.get_completed(), ret);
	// 	}
	// }
	// void * rx_buffer = dma.get_rx_buffer();
	auto packet_cnt = u220_ptrs[active_boards[0]]->get_stats().rx_packet_cnt;

	if (packet_cnt > 0 && packet_cnt % ((SAMPLES_PER_CYCLE + 2) * 20 * 5000) == 0) {
		for (const auto board_index: active_boards) {
			auto * board = u220_ptrs[board_index];
			auto ref1    = current_channels.getRef();
			auto ref2    = adc_channels.getRef();
			for (int channel: {0, 1}) {
				const int global_channel = 2 * board_index + channel;
				(*ref1)[global_channel] = (*ref2)[global_channel] = board->take_rx_queue_and_clear(channel);
			}
		}
		notifier_channels.notify();
	}
}

void GlobalData::u220_stop_and_drain_rx() {
	for (const auto board_index: active_boards) {
		u220_ptrs[board_index]->stop_and_drain_rx();
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
		rls::get_rx_words_per_buf(test_point);
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
		set_data(data, (232 + 2 * cfg.rx_insert_count) * 3);
	} else {
		ans.setData(zero_vector);
	}

	ans.nw = static_cast<uint16_t>(ans.words.size());
	global->sendMessage(ans);
}

Protocol_RLS_Mini::RR_Kvit GlobalData::set_RR_Kvit() {
	Protocol_RLS_Mini::RR_Kvit ans;
	ans.bits     = flags.bits;
	ans.ispr_kan = ispr_kan;
	ans.setDegreesDaz(daz);
	ans.dd_poi = dd_poi;
	ans.setDegreesDazPOI(daz_poi);
	piCout << ans.bits;
	piCout << PICoutManipulators::Bin << ans.ispr_kan;
	piCout << ans.daz;
	piCout << ans.dd_poi;
	piCout << ans.daz_poi;
	return ans;
}

void GlobalData::received_RR_Zapros(const Protocol_RLS_Mini::RR_Zapros & msg) {
	piCout << "rec msg" << "received_RR_Zapros    ";
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg) {
	piCout << "rec msg" << "received_RR_Vr        ";
	flags.kuvr = msg.par;

	if (techlaser.isOpened()) {
		if (flags.kuvr == 1) {
			techlaser.start(180);
		} else {
			techlaser.stop();
		}
		auto techlaser_state = techlaser.getState();
		flags.vr             = (techlaser_state.motor_status == Techlaser::MotorStatus::Rotating);
	} else {
		flags.vr = 0;
	}
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	piCout << "rec msg" << "received_RR_Izl       ";
	flags.kuizl                    = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_Ant(const Protocol_RLS_Mini::RR_Ant & msg) {
	piCout << "rec msg" << "received_RR_Ant       ";
	flags.ant                      = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_TTek(const Protocol_RLS_Mini::RR_TTek & msg) {
	piCout << "rec msg" << "received_RR_TTek      ";
	time                           = msg.getSeconds();
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr(const Protocol_RLS_Mini::RR_AzPopr & msg) {
	piCout << "rec msg" << "received_RR_AzPopr    ";
	daz                            = msg.getDegrees();
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_DPopr_POI(const Protocol_RLS_Mini::RR_DPopr_POI & msg) {
	piCout << "rec msg" << "received_RR_DPopr_POI ";
	dd_poi                         = msg.dd;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr_POI(const Protocol_RLS_Mini::RR_AzPopr_POI & msg) {
	piCout << "rec msg" << "received_RR_AzPopr_POI";
	daz_poi                        = msg.getDegrees();
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
	Protocol_RLS_Mini::POI_Kvit ans;
	ans.bits     = POI_flags.bits;
	ans.kan      = POI_kan;
	ans.dsa_vr_n = dsa_vr_n;
	ans.dsa_vr_k = dsa_vr_k;
	ans.setLevelApuk1(apu_k1);
	ans.setLevelApuk2(apu_k2);
	ans.zona_k1 = zona_k1;
	ans.zona_k2 = zona_k2;
	piCout << ans.bits;
	piCout << PICoutManipulators::Bin << ans.kan;
	piCout << ans.dsa_vr_n;
	piCout << ans.dsa_vr_k;
	piCout << ans.apu_k1;
	piCout << ans.apu_k2;
	piCout << ans.zona_k1;
	piCout << ans.zona_k2;
	return ans;
}


void GlobalData::received_POI_Zapros(const Protocol_RLS_Mini::POI_Zapros & msg) {
	piCout << "rec msg" << "received_POI_Zapros   ";
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Shtat(const Protocol_RLS_Mini::POI_Shtat & msg) {
	piCout << "rec msg" << "received_POI_Shtat    ";
	setShtat();
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_SDC(const Protocol_RLS_Mini::POI_SDC & msg) {
	piCout << "rec msg" << "received_POI_SDC      ";
	if (cfg.dma_enabled) {
		axi_dsp_set_motion_selector(1, msg.par);
		auto v        = axi_dsp_get_motion_selector();
		POI_flags.sdc = v.ONOFF;
	}

	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_DSA(const Protocol_RLS_Mini::POI_DSA & msg) {
	piCout << "rec msg" << "received_POI_DSA      ";
	POI_flags.dsa                   = msg.par;
	dsa_vr_n                        = msg.vr_n;
	dsa_vr_k                        = msg.vr_k;

	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_APU(const Protocol_RLS_Mini::POI_APU & msg) {
	piCout << "rec msg" << "received_POI_APU      ";
	apu_k1 = msg.getLevelk1();
	apu_k2 = msg.getLevelk2();
	if (cfg.dma_enabled) {
		axi_dsp_set_detector_level(apu_k1, 0);
		axi_dsp_set_detector_level(apu_k2, 1);
	} else {
		apu_k1 = 0;
		apu_k2 = 0;
	}

	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Zona(const Protocol_RLS_Mini::POI_Zona & msg) {
	piCout << "rec msg" << "received_POI_Zona     ";
	zona_k1                         = msg.k1;
	zona_k2                         = msg.k2;
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Kan(const Protocol_RLS_Mini::POI_Kan & msg) {
	piCout << "rec msg" << "received_POI_Kan      ";
	if (cfg.dma_enabled) {
		axi_dsp_set_channel_mask((uint32_t)msg.kan);
		POI_kan = (uint8_t)axi_dsp_get_channel_mask();
	} else {
		POI_kan = msg.kan;
	}
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::generate_ad9361_sync_in() {
	// 1. Prepare all FPGA sync generators
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->sync_ad9361_stage1();
	}

	// 2. Arm sync logic on all boards
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->sync_ad9361_stage2();
	}

	// 3. Start sync generation
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->sync_ad9361_master();
	}

	// 4. Return sync logic to idle
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->sync_ad9361_finish();
	}
}

void GlobalData::generate_ad9361_cat_enable() {
	// 1. Prepare all FPGA sync generators
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_stage1();
	}

	// 2. Arm sync logic on all boards
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_stage2();
	}

	// 3. Start sync generation
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_master();
	}

	// 4. Return sync logic to idle
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_finish();
	}
}

void GlobalData::generate_ad9361_cat_disable() {
	// 1. Prepare all FPGA sync generators
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_stage1();
	}
	// 3. Start sync generation
	for (size_t i: active_boards) {
		user_u220_ptrs[i]->enable_ad9361_master();
	}
}

void GlobalData::sync_ad9361_mcs() {
	piCout << "AD9361 MCS: stage 1\n";

	for (size_t i: active_boards) {
		u220_ptrs[i]->mcs_stage1();
	}

	piCout << "AD9361 MCS: SYNC #1\n";

	generate_ad9361_sync_in();
	1_s .sleep();
	// generate_ad9361_sync_in();
	// 1_s .sleep();

	piCout << "AD9361 MCS: stage 2\n";

	for (size_t i: active_boards) {
		u220_ptrs[i]->mcs_stage2();
	}

	piCout << "AD9361 MCS: SYNC #2\n";

	generate_ad9361_sync_in();
	1_s .sleep();
	// generate_ad9361_sync_in();
	// 1_s .sleep();

	piCout << "AD9361 MCS: finish\n";

	for (size_t i: active_boards) {
		u220_ptrs[i]->mcs_finish();
	}
}

void GlobalData::enable_ad9361_external_ensm() {
	piCout << "AD9361 ENSM: start";

	// 1. Общий ENABLE = 0
	generate_ad9361_cat_disable();
	1_s .sleep();

	piCout << "BEFORE ALERT:";
	print_ad9361_ensm_states();

	// 2. Переводим все AD9361 в ALERT через SPI
	for (size_t i: active_boards) {
		u220_ptrs[i]->ensm_to_alert();
	}

	1_s .sleep();

	piCout << "AFTER SPI ALERT:";
	print_ad9361_ensm_states();

	// 3. Включаем аппаратное управление ENSM
	for (size_t i: active_boards) {
		u220_ptrs[i]->ensm_enable_external();
	}

	1_s .sleep();

	piCout << "AFTER EXTERNAL CONTROL:";
	print_ad9361_ensm_states();

	// 4. Синхронный переход ALERT -> FDD
	generate_ad9361_cat_enable();
	1_s .sleep();

	piCout << "AFTER ENABLE = 1:";
	print_ad9361_ensm_states();

	piCout << "AD9361 ENSM: complete";
}


void GlobalData::enable_external_lo_all() {
	for (auto i: active_boards) {
		u220_ptrs[i]->enable_external_lo();
	}
}

void GlobalData::print_ad9361_ensm_states() {
	for (size_t i: active_boards) {
		const uint8_t state = u220_ptrs[i]->get_ensm_state();

		piCout << "AD9361[" << i << "] ENSM = 0x" << PICoutManipulators::Hex << static_cast<int>(state);
	}
}
