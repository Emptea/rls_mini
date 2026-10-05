#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "protocol_rls_mini.h"
#include "user_regs.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)) {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_ptrs << u;
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
	if (dma.init() != 0) {
		fprintf(stderr, "Failed to initialize FPGA DMA\n");
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
			dit++;
			ispr_kan |= (1 << 2 * i) | (1 << 2 * i + 1);
		}
	}

	for (const auto board_index: active_boards) {
		dma.get_all_tx_buffers(2 * board_index, tx0_buf);
		dma.get_all_tx_buffers(2 * board_index + 1, tx1_buf);
		u220_ptrs[board_index]->setup(tx0_buf, tx1_buf);
	}

	sync_ad9361_mcs();

	axi_dsp_init();
	axi_dsp_configure();
	axi_dsp_set_output_source(1, 0, 0);
	auto v = axi_dsp_get_output_source();
	piCout << "SOURCE: " << v.SOURCE << ", SOURCE_CHANNEL: " << v.SOURCE_CHANNEL << ", RANGE_GATE: " << v.RANGE_GATE << "\n";
	axi_dsp_set_channel_mask((uint32_t)ispr_kan);
	axi_dsp_apply();
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
		u220_ptrs[board_index]->start_transmission();
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
	piDeleteAllAndClear(u220_ptrs);
	active_boards.clear();
	axi_dsp_deinit();
	t_end = PISystemTime::current();
	piCout << "====";
	auto cycles_completed = u220_ptrs[active_boards[0]]->get_stats().cycles_completed;
	if (cycles_completed > 0) {
		piCout << "Mean: transfer time = " << (t_end - t_start) / cycles_completed;
	}
	piCout << "====";
	dma.cleanup();
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave

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

	unsigned int * rx_buffer = (unsigned int *) dma.get_rx_buffer();
	dma_rx_queue.emplace(rx_buffer, (BUFFER_SIZE / sizeof(unsigned int)) * RX_BUFFER_COUNT);
	if ((current_vals.tp != rls::TP_WORK) && data_updated) {
		while (dma_rx_queue.size() > 1) {
			dma_rx_queue.pop();
		}
	}
	data_updated = true;
}

void GlobalData::u220_recv() {
	for (const auto board_index: active_boards) {
		u220_ptrs[board_index]->receive();
	}

	auto packet_cnt = u220_ptrs[active_boards[0]]->get_stats().rx_packet_cnt;
	notifier_channels.notify();
}

void GlobalData::u220_stop_and_drain_rx() {
	for (const auto board_index: active_boards) {
		u220_ptrs[board_index]->stop_and_drain_rx();
	}
}

void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	// piCout << "rec msg"
	// 	   << "received_POI_TK_Zapros";
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	// piCout << "rec msg kt" << msg.kt;
	axi_dsp_set_output_source(msg.kt, msg.nkan, msg.reg_takt);
	axi_dsp_apply();
	current_vals.tp         = msg.kt;
	current_vals.ch         = msg.nkan;
	current_vals.range_gate = msg.reg_takt;

	if (data_updated) {
		data_updated = false;
	}

	int data_cnt = rls::get_rx_words_per_buf(msg.kt);
	while (!data_updated)
		;

	auto & data = dma_rx_queue.front();
	if (!data.isEmpty()) {
		// ans.setData(&data[HDR_SIZE], data_cnt);
		ans.setData(data, data_cnt);
	} else {
		ans.setData(zero_vector);
	}
	dma_rx_queue.pop();
	ans.nw = static_cast<uint16_t>(data_cnt);
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
		sync_ad9361_stage1(u220_ptrs[i]->get_usrp());
	}

	// 2. Arm sync logic on all boards
	for (size_t i: active_boards) {
		sync_ad9361_stage2(u220_ptrs[i]->get_usrp());
	}

	// 3. Start sync generation
	for (size_t i: active_boards) {
		sync_ad9361_master(u220_ptrs[i]->get_usrp());
	}

	// 4. Return sync logic to idle
	for (size_t i: active_boards) {
		sync_ad9361_finish(u220_ptrs[i]->get_usrp());
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

	piCout << "AD9361 MCS: stage 2\n";

	for (size_t i: active_boards) {
		u220_ptrs[i]->mcs_stage2();
	}

	piCout << "AD9361 MCS: SYNC #2\n";

	generate_ad9361_sync_in();
	1_s .sleep();

	piCout << "AD9361 MCS: finish\n";

	for (size_t i: active_boards) {
		u220_ptrs[i]->mcs_finish();
	}

	piCout << "AD9361 MCS: complete\n";
}
