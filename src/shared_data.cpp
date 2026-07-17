#include "shared_data.h"

#include "axi_dsp.h"
#include "dma_channel.hpp"
#include "protocol_rls_mini.h"

#include <cstdint>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)) {
	main_config = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	dma_channels.resize(NUM_CHANNELS_TX + NUM_CHANNELS_RX);
	for (int i = 0; i < NUM_CHANNELS_TX + NUM_CHANNELS_RX; i++) {
		dma_channels[i] = new dma_channel();
	}

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_ptrs << u;
		int u_channels[2] = {2 * i, 2 * i + 1};


		CONNECTL(u, received, ([this, u_channels, u] { // grab local "u" and "u_channels" as copies
			                                           //  auto ref1 = current_channels.getRef();
			                                           //  auto ref2 = adc_channels.getRef();
					 for (int i: {0, 1}) {             // 0 and 1 - index in U220, doesn`t change!
						 int ch = u_channels[i];       // 0 - 7
						 dma_channels[ch + 1]->start_transfer();
						 //  (*ref1)[ch] = (*ref2)[ch] =
				         // 	 u->take_rx_queue_and_clear(i); // or something else ... grab your 0/1 channel data
					 }
					 1_ms .sleep();
					 for (int i: {0, 1}) {       // 0 and 1 - index in U220, doesn`t change!
						 int ch = u_channels[i]; // 0 - 7
						 if (dma_channels[ch + 1]->wait_for_transfer() == dma_channel::channel_buffer::proxy_status::PROXY_NO_ERROR) {
							 ispr_kan |= (1U << ch);
						 } else {
							 ispr_kan &= ~(1U << ch);
						 }
					 }
					 notifier_channels.notify();
				 }));
	}

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
	dma_channels[0]->init(rx_config);
	for (size_t i = 0; i < RX_BUFFER_COUNT; i++) {
		dma_rx_buffers[i] = dma_channels[0]->get_buffer(i);
	}
	piCout << "Rx buffers adresses are:";
	for (size_t i = 0; i < RX_BUFFER_COUNT; i++) {
		piCout << "num " << i << " " << PICoutManipulators::PICoutFormat::Hex << dma_rx_buffers[i];
	}

	for (size_t i = 0; i < NUM_CHANNELS_TX; i++) {
		tx_config.devnode = PIString2StdString(tx_devnodes[i]);
		dma_channels[i + 1]->init(tx_config);
		dma_channels[i + 1]->get_all_buffers(dma_tx_buffers[i]);
		dma_channels[i + 1]->set_num_transfers(0);
		for (size_t k = 0; k < TX_BUFFER_COUNT; k++) {
			piCout << "ch" << i << " buf" << k << " " << PICoutManipulators::PICoutFormat::Hex << dma_tx_buffers[i][k];
		}
	}
}

void GlobalData::initDSP() {
	axi_dsp_init();
	axi_dsp_set_output_source(1, 0);
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
	axi_dsp_set_compensation_mode(1);
	axi_dsp_apply();
}


void GlobalData::init() {
	initEth();
	initDSP();
	initDMAs();

	zero_vector.resize(U220_SPB, {0, 0});
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	for (size_t i = 0; i < u220_ptrs.size(); i++) {
		if (StdString2PIString(dit->first) == u220_ptrs[i]->get_serial() & dit != devices.end()) {
			void * buffer_ptrs[2 * TX_BUFFER_COUNT];
			for (int buf_num: {0, 1}) {
				buffer_ptrs[2 * buf_num]     = dma_tx_buffers[i][buf_num];
				buffer_ptrs[2 * buf_num + 1] = dma_tx_buffers[i + 1][buf_num];
			}
			u220_ptrs[i]->init(buffer_ptrs);
			active_boards.push_back(i);
			dit++;
		}
	}
	sync();
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


	return results.every([](bool r) { return r == true; }); // shortcut for check all items in "results" ( RTFM :-) )
}


void GlobalData::start() {
	startEth();
	for (size_t i = 0; i < active_boards.size(); i++) {
		// u220_ptrs[active_boards[i]]->start_reception(4.64+180*0.2e-6);
		double start_time = 4.64 + 5;
		u220_ptrs[active_boards[i]]->start_reception(start_time - 60 * 0.2e-6 - 46.4e-5);
		u220_ptrs[active_boards[i]]->start_transmission(start_time);
	}
}


void GlobalData::stop() {
	stopEth();
	for (size_t i = 0; i < active_boards.size(); i++) {
		u220_ptrs[active_boards[i]]->stop_reception();
		u220_ptrs[active_boards[i]]->stop_transmission();
	}
	for (int k = dma_channels.size() - 1; k >= 0; k--) {
		dma_channels[k]->cleanup();
		delete dma_channels[k];
		dma_channels[k] = nullptr;
	}
	piDeleteAllAndClear(u220_ptrs);
	axi_dsp_deinit();
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave
	piCout << "Start wait for transfer";

	piCout << "Start wait for transfer";
	for (int ch: active_boards) {
		// if (dma_channels[ch + 1]->wait_for_transfer() == dma_channel::channel_buffer::proxy_status::PROXY_NO_ERROR) {
		// 	ispr_kan |= (1U << ch);
		// } else {
		// 	ispr_kan &= ~(1U << ch);
		// }

		if (ch == req_test_channel) {
			req_test_point = false;
		}
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


void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	piCout << "rec msg"
		   << "received_POI_TK_Zapros";
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	piCout << "rec msg kt" << msg.kt;
	axi_dsp_set_test_point(msg.kt);
	axi_dsp_apply();
	req_test_channel = msg.kt;
	req_test_point   = true;

	switch (msg.kt) {
	case Protocol_RLS_Mini::CTRL: {
		break;
	}
	case Protocol_RLS_Mini::ADC: {
		ans.nw = 232;
		break;
	}
	case Protocol_RLS_Mini::CUT:
	case Protocol_RLS_Mini::PLL:
	case Protocol_RLS_Mini::OPH:
	case Protocol_RLS_Mini::LOU: {
		ans.nw = 141;
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

	if ((1 << msg.nkan) * ispr_kan) {
		while (req_test_point) {}
		ans.setData((uint32_t *)dma_rx_buffers[dma_channels[0]->get_buffer_id()], ans.nw);
	} else {
		ans.setData(zero_vector);
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
	piCout << "rec msg"
		   << "received_RR_Zapros    ";
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg) {
	piCout << "rec msg"
		   << "received_RR_Vr        ";
	flags.ant                      = msg.par;
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	piCout << "rec msg"
		   << "received_RR_Izl       ";
	flags.izl                      = msg.par;
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
