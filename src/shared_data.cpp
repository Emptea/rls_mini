#include "shared_data.h"

#include "protocol_rls_mini.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pitime.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)) {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");

	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_ptrs << u;
		int u_channels[2] = {2 * i, 2 * i + 1};


		CONNECTL(u, received, ([this, u_channels, u] { // grab local "u" and "u_channels" as copies
					 auto ref = current_channels.getRef();
					 for (int i: {0, 1}) {                            // 0 and 1 - index in U220, doesn`t change!
						 int global_channel     = u_channels[i];      // 0 - 7
						 (*ref)[global_channel] = u->get_rx_queue(i); // or something else ... grab your 0/1 channel data
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


void GlobalData::init() {
	initEth();
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	for (size_t i = 0; i < u220_ptrs.size(); i++) {
		if (StdString2PIString(dit->first) == u220_ptrs[i]->get_serial() & dit != devices.end()) {
			u220_ptrs[i]->init();
			active_boards.push_back(i);
			dit++;
		}
	}

	// for (size_t i = 0; i < active_boards.size(); i++) {
	//	u220_ptrs[active_boards[i]]->start_sync();
	// }
}


bool GlobalData::sync() {
	PISemaphore sem;
	PIVector<PIThread *> sync_threads;
	PIVector<bool> results(u220_ptrs.size(), false);
	for (int i = 0; i < u220_ptrs.size_s(); ++i) {
		auto * u  = u220_ptrs[i];
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
	(100_ms).sleep(); // ensure that all threads waits on "sem.acquire()"
	sem.release(sync_threads.size_s()); // release 4 resources (all threads, waits on "sem.acquire()", now go next)
	for (auto * t: sync_threads)        // wait for all thread to finish their functors
		t->stopAndWait();
	piDeleteAll(sync_threads); // delete threads


	return results.every([](bool r) { return r == true; }); // shortcut for check all items in "results" ( RTFM :-) )
}


void GlobalData::start() {
	startEth();
	for (size_t i = 0; i < active_boards.size(); i++) {
		u220_ptrs[active_boards[i]]->start_reception(5);
		u220_ptrs[active_boards[i]]->start_transmission(5);
	}
}


void GlobalData::stop() {
	stopEth();
	for (size_t i = 0; i < active_boards.size(); i++) {
		u220_ptrs[active_boards[i]]->stop_reception();
		u220_ptrs[active_boards[i]]->stop_transmission();
	}
	piDeleteAllAndClear(u220_ptrs);
}

void GlobalData::processChannels() {
	notifier_channels.wait();
	if (process_thread.isStopping()) return; // if stop() called simply leave

	PIMap<int, VectorComplexF> channels;

	{ // start work with "getRef"
		auto ref = current_channels.getRef();
		for (int ch = 0; ch < 8; ++ch) {
			if ((*ref)[ch].isEmpty()) return; // at least one channel is empty, leave
		}

		channels = *ref; // copy data

		for (int ch = 0; ch < 8; ++ch) {
			(*ref)[ch].clear(); // clear input data
		}
	} // desctuct "ref", release current_channels

	// work with your data (channels)
	adc_channels = channels;
}


void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	piCout << "rec msg" << "received_POI_TK_Zapros";
	Protocol_RLS_Mini::POI_TK_Kvit ans;

	switch (msg.kt) {
	case Protocol_RLS_Mini::CTRL: {
		break;
	}
	case Protocol_RLS_Mini::ADC: {
		VectorComplexF * data = &adc_channels[msg.nkan];
		ans.nw                = data->size();
		ans.setData(adc_channels[msg.nkan]);
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
