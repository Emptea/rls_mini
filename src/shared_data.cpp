#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "mini_config.hpp"
#include "protocol_rls_mini.h"
#include "user_u220.h"

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
			user_u220_ptrs[i] = new UserU220(u220_ptrs[i]->get_usrp());
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

	uint32_t cnt = 0;
	cnt++;
	piCout << cnt;
	for (size_t i: active_boards) {
		const auto ver = user_u220_ptrs[i]->get_fpga_version();
		const auto ser = u220_ptrs[i]->get_serial();
		piCout << ser << PICoutManipulators::Hex << ver;
	}
	5_s .sleep();

	for (size_t i: active_boards) {
		user_u220_ptrs[i]->set_tx_dac_delay(cfg.tx_dac_delay[i * 2], cfg.tx_dac_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_tx_gainon_delay(cfg.tx_gpio_delay[i * 2], cfg.tx_gpio_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_rx_adc_delay(cfg.rx_adc_delay[i * 2], cfg.rx_adc_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_rx_en_delay(cfg.rx_en_delay[i * 2], cfg.rx_en_delay[i * 2 + 1]);
		user_u220_ptrs[i]->set_play_gpio_tx_enable(0b11);
		user_u220_ptrs[i]->set_play_gpio_rx_enable(0b11);
		user_u220_ptrs[i]->set_play_rx_insert_count(1);
		// user_u220_ptrs[i]->set_tx_gainon_sg(0, cfg.tx_gainon_sg[i * 2]);
		// user_u220_ptrs[i]->set_tx_gainon_sg(1, cfg.tx_gainon_sg[i * 2 + 1]);
		// user_u220_ptrs[i]->set_tx_dac_sg(0, cfg.tx_dac_sg[i * 2]);
		// user_u220_ptrs[i]->set_tx_dac_sg(1, cfg.tx_dac_sg[i * 2 + 1]);
		user_u220_ptrs[i]->set_tx_complex_gain(0, cfg.complex_gains[i * 2]);
		user_u220_ptrs[i]->set_tx_complex_gain(1, cfg.complex_gains[i * 2 + 1]);
	}

	// 5_s .sleep();
	cnt++;
	piCout << cnt;

	// for (size_t i = 0; i < 10; i++) {
	enable_ad9361_external_ensm();
	generate_ad9361_cat_disable();
	print_ad9361_ensm_states();
	1_s .sleep();
	sync_ad9361_mcs();
	generate_ad9361_cat_enable();
	print_ad9361_ensm_states();
	5_s .sleep();
	// }
	enable_external_lo_all();

	// enable_ad9361_external_ensm();

	// // 5_s .sleep();
	// for (size_t i = 0; i < 16; i++) {
	// 	// u220_ptrs[0]->ensm_to_alert();
	// 	// 1_s .sleep();
	// 	// u220_ptrs[0]->ensm_to_fdd();
	// 	toggle_ad9361_cat_enable();
	// 	10_s .sleep();
	// }


	cnt++;
	piCout << cnt;

	for (size_t i: active_boards) {
		user_u220_ptrs[i]->reset_time(0x05DC);
	}
	for (size_t i: active_boards) {
		// user_u220_ptrs[i]->set_play_start_tx();
		// user_u220_ptrs[i]->set_play_rx_enable(1);
	}

	5_s .sleep();
	cnt++;
	piCout << cnt;

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
	// sync();
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
	// PIMap<int, VectorComplexS> channels;
	// bool all_channels = true;
	// { // start work with "getRef"
	// 	auto ref = current_channels.getRef();
	// 	for (int ch = 0; ch < 8; ++ch) {
	// 		if ((*ref)[ch].isEmpty()) {
	// 			ispr_kan &= ~(1U << ch);
	// 			all_channels = false;
	// 		} else {
	// 			ispr_kan |= (1U << ch);
	// 		}
	// 	}

	// 	if (~all_channels) return;
	// 	channels = *ref; // copy data

	// 	for (int ch = 0; ch < 8; ++ch) {
	// 		if (!(*ref)[ch].isEmpty()) {
	// 			(*ref)[ch].clear(); // clear input data
	// 		}
	// 	}
	// } // desctuct "ref", release current_channels

	// work with your data (channels)
	// adc_channels = channels;
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

void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	piCout << "rec msg" << "received_POI_TK_Zapros";
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	piCout << "rec msg kt" << msg.kt;

	switch (msg.kt) {
	case Protocol_RLS_Mini::CTRL: {
		break;
	}
	case Protocol_RLS_Mini::ADC: {
		VectorComplexS data;
		{
			auto ref = adc_channels.getRef();
			data     = (*ref)[msg.nkan];
		}
		ans.nw = data.size();
		if (data.isEmpty()) {
			ans.setData(zero_vector);
		} else {
			ans.setData(data, 232 * 3);
		}
		break;
	}
	case Protocol_RLS_Mini::PHD: {
		break;
	}
	case Protocol_RLS_Mini::PLL: {
		break;
	}
	case Protocol_RLS_Mini::OPH: {
		break;
	}
	case Protocol_RLS_Mini::LOU: {
		break;
	}
	case Protocol_RLS_Mini::KN: {
		break;
	}
	case Protocol_RLS_Mini::AD: {
		break;
	}
	case Protocol_RLS_Mini::APU: {
		break;
	}
	}
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
	flags.ant                      = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	piCout << "rec msg" << "received_RR_Izl       ";
	flags.izl                      = msg.par;
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

void GlobalData::toggle_ad9361_cat_enable() {
	piCout << "Press ENTER to switch ENSM -> ALERT\n";
	std::cin.get();

	generate_ad9361_cat_disable();
	print_ad9361_ensm_states();

	piCout << "Press ENTER to switch ENSM -> FDD\n";
	std::cin.get();

	generate_ad9361_cat_enable();
	print_ad9361_ensm_states();
}
