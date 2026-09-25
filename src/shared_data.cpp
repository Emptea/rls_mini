#include "shared_data.h"

#include "misc.h"
#include "protocol_rls_mini.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>

namespace fs = std::filesystem;

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)) {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	dma_channel * rx               = new dma_channel();
	dma_rx                         = rx;

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_ptrs << u;
		int u_channels[2] = {2 * i, 2 * i + 1};
	}

	CONNECTL(rx, received, ([this, rx] { // grab local "u" and "u_channels" as copies
				 auto ref1 = dma_channel_buf.getRef();
				 rx->take_rx_queue_and_clear(*ref1); // or something else ... grab your 0/1 channel data
				 notifier_channels.notify();
			 }));

	process_thread.start([this] { processChannels(); });
}


GlobalData::~GlobalData() {
	process_thread.stop();          // mark thread for stop
	notifier_channels.notify();     // notify thread
	process_thread.waitForFinish(); // wait for thread really finish
}


GlobalData * GlobalData::instance() {
	static GlobalData ret;
	return &ret;
}


void GlobalData::initDMAs() {
	PIString dir_path_str = "data/" + StdString2PIString(misc_get_date());
	fs::path dir_path     = PIString2StdString(dir_path_str);

	if (fs::create_directories(dir_path)) {
		piCout << "Created directory" << dir_path_str;
	}
	piCout << "Save to directory" << dir_path_str;

	dma_rx->init(rx_config);
	PIString filename = dir_path_str + "/rls_mini_";
	filename += StdString2PIString(misc_get_datetime());
	filename += ".hex";

	// dma_rx->set_save_to_buf();
	dma_rx->set_save_to_file(filename, BUFFER_SIZE / sizeof(unsigned int));
	for (size_t i = 0; i < RX_BUFFER_COUNT; i++) {
		dma_rx_buffers[i] = dma_rx->get_buffer(i);
	}
	piCout << "Rx buffers adresses are:";
	for (size_t i = 0; i < RX_BUFFER_COUNT; i++) {
		piCout << "num " << i << " " << PICoutManipulators::PICoutFormat::Hex << dma_rx_buffers[i];
	}
}

void GlobalData::initDSP() {
	axi_dsp_init();
	axi_dsp_set_output_source(1, 7, 0);
	auto v = axi_dsp_get_output_source();
	piCout << "SOURCE: " << v.SOURCE << ", SOURCE_CHANNEL: " << v.SOURCE_CHANNEL << "\n";
	cmplx_f64 manual_comp   = {.real = 1, .imag = 0};
	cmplx_f64 diagrams_even = {.real = 1, .imag = 0};
	cmplx_f64 diagrams_odd  = {.real = 0, .imag = 1};
	for (size_t i = 0; i < NUM_CHANNELS_TX; i++) {
		axi_dsp_set_manual_compensation(manual_comp, i);
		axi_dsp_set_diagram_0(diagrams_even, i);
		axi_dsp_set_diagram_1(diagrams_odd, i);
		axi_dsp_set_diagram_2(diagrams_even, i);
		axi_dsp_set_diagram_3(diagrams_odd, i);
		axi_dsp_set_diagram_4(diagrams_even, i);
		axi_dsp_set_diagram_5(diagrams_odd, i);
		axi_dsp_set_diagram_6(diagrams_even, i);
		axi_dsp_set_diagram_7(diagrams_odd, i);
	}
	axi_dsp_set_compensation_mode(0);
	setShtat();
	axi_dsp_apply();
}

void GlobalData::init() {
	initEth();
	initDSP();
	initDMAs();

	const auto & com_conf = global->mainConfig().child("com");
	techlaser.open(com_conf.childValue("techlaser").toString());

	piCout << "Start U220 init";
	zero_vector.resize(U220_SPB, {0, 0});
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	for (size_t i = 0; i < u220_ptrs.size(); i++) {
		if (StdString2PIString(dit->first) == u220_ptrs[i]->get_serial() & dit != devices.end()) {
			dma_channel::ch_config dma_tx_configs[2] = {
				{
                 .devnode        = PIString2StdString(dma_tx_devnodes[2 * i]),
                 .buffer_size    = TX_BUF_SIZE,
                 .buffer_count   = TX_BUFFER_COUNT,
                 .channel_number = 2 * i,
				 },
				{
                 .devnode        = PIString2StdString(dma_tx_devnodes[2 * i + 1]),
                 .buffer_size    = TX_BUF_SIZE,
                 .buffer_count   = TX_BUFFER_COUNT,
                 .channel_number = 2 * i + 1,
				 },
			};
			u220_ptrs[i]->init(dma_tx_configs);
			active_boards.push_back(i);
			ispr_kan |= (1 << 2 * i);
			ispr_kan |= (1 << 2 * i + 1);
			dit++;
		}
	}
	axi_dsp_set_channel_mask((uint32_t)ispr_kan);
	piCout << "Active channels mask:" << PICoutManipulators::Bin << ispr_kan;
	if (!sync()) {
		throw std::runtime_error("U220 synchronization failed");
	}
}


bool GlobalData::sync() {
	PISemaphore sem;
	PIVector<PIThread *> sync_threads;
	PIVector<bool> results(active_boards.size(), false);
	for (int i = 0; i < active_boards.size_s(); ++i) {
		auto * u  = u220_ptrs[active_boards[i]];
		// create thread with this functor
		// capture "i" and "u" as values, "sem" and "results" as reference (we want modify it)
		auto * st = new PIThread([i, u, &sem, &results] {
			sem.acquire();          // wait for 1 resource from semaphore
			results[i] = u->sync(); // sync and store result to results by index
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


	if (!results.every([](bool r) { return r; })) return false;
	if (active_boards.isEmpty()) return true;

	// Observe one PPS edge, then arm all boards for the following edge.
	auto * reference    = u220_ptrs[active_boards[0]];
	const auto last_pps = reference->get_time_last_pps();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (reference->get_time_last_pps() == last_pps) {
		if (std::chrono::steady_clock::now() >= deadline) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto edge = std::chrono::steady_clock::now();
	for (auto index: active_boards)
		u220_ptrs[index]->reset_time_next_pps();
	// Reject a slow command sequence that could have crossed another PPS edge.
	if (std::chrono::steady_clock::now() - edge > std::chrono::milliseconds(500)) return false;
	std::this_thread::sleep_until(edge + std::chrono::milliseconds(1100));
	for (auto index: active_boards) {
		if (u220_ptrs[index]->get_time_last_pps() != uhd::time_spec_t(0.0)) return false;
	}
	return true;
}


void GlobalData::start() {
	startEth();
	// double start_time = 4.64 + 5;
	double start_time = 3;
	// Validate all boards before issuing any acquisition command.
	for (auto index: active_boards) {
		auto * board      = u220_ptrs[index];
		const double rate = board->get_rx_rate();
		if (rate != u220_ptrs[active_boards[0]]->get_rx_rate()) {
			throw std::invalid_argument("Synchronized acquisition requires equal sample rates");
		}
		start_time = std::max(start_time, board->get_time_now().get_real_secs() + 2.0);
	}
	std::cout << boost::format("Scheduled continuous RX start: %.9f device seconds\n") % (start_time - 60 * 0.2e-6 - 46.4e-5);
	// Release the first turn only after every board has its timed command and worker.
	const auto order = std::make_shared<RxOrder>(active_boards.size());
	try {
		for (size_t i = 0; i < active_boards.size(); i++) {
			u220_ptrs[active_boards[i]]->start_continuous_reception(start_time - 60 * 0.2e-6 - 46.4e-5, order, i);
		}
		order->start();
	} catch (...) {
		order->cancel(); // Wake already-started workers if a later board fails to start.
		throw;
	}
	piSleep(PISystemTime::fromSeconds(start_time - 0.5));
	dma_rx->start();
}


void GlobalData::stop() {
	stopEth();
	int num_transfers = dma_rx->get_pending_transfer_target();
	for (auto index: active_boards)
		num_transfers = std::max(num_transfers, u220_ptrs[index]->get_dma_pending_transfer_target());
	// Leave enough headroom to apply the limit to every running channel before any one reaches it.
	num_transfers += RX_BUFFER_COUNT + TX_BUFFER_COUNT;
	dma_rx->set_num_transfers(num_transfers);
	for (auto index: active_boards)
		u220_ptrs[index]->set_dma_num_transfers(num_transfers);
	piCout << "Stopping all DMA channels after" << num_transfers << "transfers";

	// Workers stop the USRPs and drain USB independently of DMA RX completion.
	for (auto index: active_boards)
		u220_ptrs[index]->wait_for_reception();
	dma_rx->waitForFinish();
	piCout << "DMA RX stopped";
	for (size_t i = 0; i < active_boards.size(); i++) {
		u220_ptrs[active_boards[i]]->stop_reception();
		ispr_kan &= (1 << 2 * active_boards[i]);
		ispr_kan &= (1 << 2 * active_boards[i] + 1);
		axi_dsp_set_channel_mask((uint32_t)ispr_kan);
		piCout << "Active channels mask:" << PICoutManipulators::Bin << ispr_kan;

		// u220_ptrs[active_boards[i]]->stop_transmission();
	}
	piCout << "U220 stopped";
	piDeleteAllAndClear(u220_ptrs);
	axi_dsp_deinit();
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave

	auto ref            = dma_channel_buf.getRef();
	VectorUint buffer   = *ref;
	struct header * hdr = (header *)&buffer;
	if (hdr->tp == TP_WORK) {
		struct work_posthdr * work   = (struct work_posthdr *)(hdr + 1);
		struct work_packet * packets = reinterpret_cast<struct work_packet *>(work + 1);
		nes                          = work->n_work_packets;
		time_delayed                 = static_cast<uint32_t>(std::round((double)(hdr->packet_number - work->packet_number) * 46.4e-3));
		for (size_t i = 0; i < nes; i++) {
			struct work_packet & packet = packets[i];
			targets[i].D                = packet.range;
			targets[i].vr               = static_cast<uint32_t>(std::round(VEL_MULT * (double)packet.frequency_channel));
			targets[i].um               = (double)packet.main_amplitude / (double)packet.neighbor_amplitude;
		}
	}
}


void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	piCout << "rec msg"
		   << "received_POI_TK_Zapros";
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	piCout << "rec msg kt" << msg.kt;
	axi_dsp_set_output_source(msg.kt, msg.nkan, msg.reg_takt);
	axi_dsp_apply();
	req_test_channel = msg.kt;
	req_test_point   = true;

	int data_cnt;
	switch (msg.kt) {
	case TP_WORK: {
		data_cnt = sizeof(work_posthdr);
		break;
	}
	case TP_BYPASS: {
		data_cnt = (N_SAMPS_IN_PACK + HDR_SIZE);
		break;
	}
	case TP_CUT:
	case TP_FAPCH:
	case TP_LOU: {
		data_cnt = (164);
		break;
	}
	case TP_SF:
	case TP_MAX:
	case TP_RANK:
	case TP_APU: {
		data_cnt = (141);
		break;
	}
	case TP_DDR:
	case TP_FFT:
	case TP_WEIGHT_OUT: {
		data_cnt = 512;
		break;
	}
	case TP_FIND: {
		data_cnt = 141 * 5;
		break;
	}
	case TP_FAPCH_COEFFS: {
		data_cnt = (8);
		break;
	}
	default: {
		break;
	}
	}

	VectorUint data;
	auto ref = dma_channel_buf.getRef();
	data     = (*ref);

	if (!ref->isEmpty()) {
		// ans.setData(&data[HDR_SIZE], data_cnt);
		ans.setData(data, data_cnt);
	} else {
		ans.setData(zero_vector);
	}
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

void GlobalData::received_RR_Zapros(const Protocol_RLS_Mini::RR_Zapros & msg) {
	piCout << "rec msg"
		   << "received_RR_Zapros    ";
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg) {
	piCout << "rec msg"
		   << "received_RR_Vr        ";
	flags.kuvr = msg.par;

	if (flags.kuvr == 1) {
		techlaser.start(180);
	} else {
		techlaser.stop();
	}
	auto techlaser_state           = techlaser.getState();
	flags.vr                       = (techlaser_state.motor_status == Techlaser::MotorStatus::Rotating);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	piCout << "rec msg"
		   << "received_RR_Izl       ";
	flags.kuizl                    = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_Ant(const Protocol_RLS_Mini::RR_Ant & msg) {
	piCout << "rec msg"
		   << "received_RR_Ant       ";
	flags.ant                      = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_TTek(const Protocol_RLS_Mini::RR_TTek & msg) {
	piCout << "rec msg"
		   << "received_RR_TTek      ";
	time                           = msg.getSeconds();
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr(const Protocol_RLS_Mini::RR_AzPopr & msg) {
	piCout << "rec msg"
		   << "received_RR_AzPopr    ";
	daz                            = msg.getDegrees();
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_DPopr_POI(const Protocol_RLS_Mini::RR_DPopr_POI & msg) {
	piCout << "rec msg"
		   << "received_RR_DPopr_POI ";
	dd_poi                         = msg.dd;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}
void GlobalData::received_RR_AzPopr_POI(const Protocol_RLS_Mini::RR_AzPopr_POI & msg) {
	piCout << "rec msg"
		   << "received_RR_AzPopr_POI";
	daz_poi                        = msg.getDegrees();
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
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
	axi_dsp_set_motion_selector(1, msg.par);
	auto v                          = axi_dsp_get_motion_selector();
	POI_flags.sdc                   = v.ONOFF;

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
	axi_dsp_set_detector_level(apu_k1, 0);
	axi_dsp_set_detector_level(apu_k2, 1);

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
	axi_dsp_set_channel_mask((uint32_t)msg.kan);
	POI_kan                         = (uint8_t)axi_dsp_get_channel_mask();
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}
