#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "axu_header_logger.hpp"
#include "imitator_u220.h"
#include "mini_config.hpp"
#include "protocol_rls_mini.h"
#include "rls_const.hpp"
#include "rx_cycle_trace.hpp"
#include "user_u220.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>


bool set_rx_realtime_priority() {
	sched_param param{};
	param.sched_priority = 30;
	const int err        = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	if (err != 0) {
		piCout << "RX SCHED_FIFO error:" << strerror(err);
		return false;
	}
	piCout << "RX SCHED_FIFO priority = 30";
	return true;
}

GlobalData::GlobalData()
	: GlobalDataEth(this)
	, cfg()
	, u220_group(cfg)
	, imitator_u220(u220_group)
	, cpm_rr(techlaser, imitator_u220, u220_group)
	, cpm_poi(cfg.dma_enabled, cfg.fap_ref_level, cfg.adc_noise_level)
	, cpm_cit(imitator_u220) {
	main_config                    = PIValueTreeConversions::fromTextFile("rls_mini.conf");
	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	for (int i = 0; i < serial_list.size(); i++) {
		U220 * u = new U220(serial_list[i], u220_args, 0, u220_config);
		u220_group.add_board(u);
	}
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
		if (dma.init(cfg.rx_insert_count) != 0) {
			fprintf(stderr, "Failed to initialize FPGA DMA\n");
			cfg.dma_enabled = false;
		} else {
			piCout << "DMA: init";
		}
	}

	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	void * tx0_buf[TX_BUFFER_COUNT];
	void * tx1_buf[TX_BUFFER_COUNT];

	uint8_t ispr_kan = 0;
	for (size_t i = 0; i < u220_group.board_count(); ++i) {
		auto * board = u220_group.board(i);
		if (!board) continue;
		const std::string serial = PIString2StdString(board->get_serial());
		if (devices.find(serial) == devices.end()) {
			piCout << "U220 not connected:" << board->get_serial();
			continue;
		}
		piCout << "Activating U220:" << board->get_serial() << "index:" << i;
		u220_group.activate_board(i);
		ispr_kan |= static_cast<uint8_t>((1u << (2 * i)) | (1u << (2 * i + 1)));
	}

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
	const auto u220_ip_version = u220_group.fpga(u220_group.active_boards()[0])->get_fpga_version();
	cpm_poi.init(cfg.dma_enabled, ispr_kan, cfg.target_velocity_min, cfg.target_velocity_max, u220_ip_version, cfg.rx_insert_count);

	u220_group.sync_ad9361();
	// нужна задержка чтобы pps сбросился иначе в reset_time не сработает
	2_s .sleep();
	u220_group.reset_time(0x05DC);

	first_transfer = false;
}

void GlobalData::start() {
	startEth();
	if (cfg.log_axu_hdr && cfg.dma_enabled) {
		axu_header_logger.start();
	}
	cpm_thread.start([this]() { cpm_loop(); });

	if (u220_group.active_boards().isEmpty()) {
		return;
	}
	u220_group.reset_time_synchronizer();

	for (const auto board_index: u220_group.active_boards()) {
		// u220_group.board(board_index)->start_reception(4.64+180*0.2e-6);
		u220_group.board(board_index)->set_time_sync();
		double start_time = 0.05;
		u220_group.fpga(board_index)->set_play_rx_enable(1);
		u220_group.board(board_index)->start_reception(start_time);
	}

	// 0.1_s .sleep();
	if (cfg.dma_enabled) {
		u220_recv_thread.start([this] {
			static thread_local bool initialized = false;

			if (!initialized) {
				initialized = true;
				set_rx_realtime_priority();
			}

			u220_recv_dma();

			if (u220_recv_thread.isStopping()) {
				u220_group.stop_and_drain_rx();
			}
		});
	} else {
		u220_recv_thread.start([this] {
			static thread_local bool initialized = false;

			if (!initialized) {
				initialized = true;
				set_rx_realtime_priority();
			}

			u220_recv_software();
		});
	}

	rx_processing_running.store(true);

	rx_processing_thread = std::thread([this] {
		if (cfg.dma_enabled) {
			while (rx_processing_running.load()) {
				process_rx_buffers_dma();
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
		} else {
			while (rx_processing_running.load()) {
				process_rx_buffers_software();
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
		}
	});

	piCout << "Imitator init";
	cpm_cit.set_shtat(cfg);
	imitator_u220.restart_opu();
	imitator_u220.apply(0);
	// cpm_rr.set_vr(CpmRr::RotationCommand::Simulator);

	imitator_u220.apply(0.0);

	auto poi_kvit = set_POI_Kvit();
	auto cit_kvit = set_CIT_Kvit();
	auto rr_kvit  = set_RR_Kvit();
	// piCout << poi_kvit;
	// piCout << cit_kvit;
	// piCout << rr_kvit;
	global->sendMessage(poi_kvit);
	global->sendMessage(cit_kvit);
	global->sendMessage(rr_kvit);


	// 1_s .sleep();

	auto start_time = u220_group.board(u220_group.active_boards()[0])->get_usrp()->get_time_now();
	piCout << "TX start at" << start_time.get_full_secs();

	for (const auto board_index: u220_group.active_boards()) {
		u220_group.fpga(board_index)->set_play_start_tx();
	}
}

void GlobalData::stop() {
	cpm_thread.stopAndWait();
	u220_recv_thread.stopAndWait();
	// dump_rx_traces(rx_cycle_trace.snapshot());
	// dump_time_sync_traces();
	t_end = PISystemTime::current();

	if (cfg.dma_enabled) {
		while (dma.get_completed() < dma.get_submitted()) {
			if (dma.receive() != 0) break;
		}
	}

	rx_processing_running.store(false);
	if (rx_processing_thread.joinable()) {
		rx_processing_thread.join();
	}

	stopEth();

	for (const auto board_index: u220_group.active_boards()) {
		u220_group.board(board_index)->stop_reception();
		u220_group.board(board_index)->stop_transmission();
	}

	for (size_t i: u220_group.active_boards()) {
		u220_group.fpga(i)->set_tx_mode(0, 0);
		u220_group.fpga(i)->set_play_stop_tx();
	}

	// write_dump_hex("rx_dump.hex");
	const auto cycles_completed =
		u220_group.active_boards().isEmpty() ? 0 : u220_group.board(u220_group.active_boards()[0])->get_stats().cycles_completed;

	if (cfg.dma_enabled) {
		piCout << "DDR slow reg value:" << axi_dsp_get_status().DDR_STORAGE_SLOW;
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
	axu_header_logger.stop();
}

void GlobalData::dump_time_sync_traces() {
	namespace fs = std::filesystem;

	const auto timestamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	const fs::path log_dir = "logs";

	std::error_code ec;
	fs::create_directories(log_dir, ec);

	if (ec) {
		piCout << "ERROR: cannot create logs directory:" << StdString2PIString(ec.message());
		return;
	}

	std::vector<BoardTimeSyncTraceEntry> all_entries;

	for (const auto board_index: u220_group.active_boards()) {
		auto * board             = u220_group.board(board_index);

		const std::string serial = PIString2StdString(board->get_serial());

		const auto snapshot      = board->get_time_sync_trace();

		for (const auto & entry: snapshot) {
			all_entries.push_back({serial, entry});
		}
	}

	const fs::path filename = log_dir / ("time_sync_" + std::to_string(timestamp) + ".csv");

	write_time_sync_csv(all_entries, filename.string());
}

void GlobalData::u220_recv_software() {
	if (u220_group.begin_rx_dump_cycle()) {
		u220_group.receive_rx_dump();
		return;
	}

	using Clock             = std::chrono::steady_clock;

	static bool tid_printed = false;

	if (!tid_printed) {
		tid_printed = true;
		piCout << "U220 RX TID:" << syscall(SYS_gettid);
	}

	static int buffer_idx     = 0;
	static uint64_t iteration = 0;
	static Clock::time_point previous_start{};

	const auto cycle_start           = Clock::now();
	const int64_t cycle_cpu_start_ns = rx_thread_cpu_time_ns();
	RxCycleTraceEntry cycle{};
	cycle.iteration = iteration++;

	cycle.start_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(cycle_start.time_since_epoch()).count();

	if (previous_start != Clock::time_point{}) {
		cycle.cycle_period_us = std::chrono::duration_cast<std::chrono::microseconds>(cycle_start - previous_start).count();
	}

	previous_start         = cycle_start;

	bool overflow_detected = false;

	for (const auto board_index: u220_group.active_boards()) {
		auto * board     = u220_group.board(board_index);

		const auto start = Clock::now();
		const auto error = board->receive(buffer_idx);
		const auto end   = Clock::now();

		if (error == uhd::rx_metadata_t::ERROR_CODE_NONE) {
			std::lock_guard<std::mutex> lock(pending_tk_mutex);

			if (pending_tk.active && pending_tk.kt == rls::TP_SDR && pending_tk.nkan / 2 == board_index) {
				std::lock_guard<std::mutex> sdr_lock(sdr_rx.mutex);

				sdr_rx.buffer     = {board->get_rx_buffer(buffer_idx, pending_tk.nkan % 2), pending_tk.words};
				sdr_rx.ready      = true;
				pending_tk.active = false;
			}
		}


		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

		if (board_index < 4) {
			cycle.board_receive_us[board_index] = elapsed;
			cycle.board_error[board_index]      = static_cast<int>(error);
		}

		cycle.recv_total_us += elapsed;

		if (elapsed > 5000) {
			piCout << "SLOW U220 RECEIVE:" << board->get_serial() << elapsed << "us";
		}

		if (error == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
			overflow_detected = true;
		}
	}

	const auto cycle_end           = std::chrono::steady_clock::now();
	const int64_t cycle_cpu_end_ns = rx_thread_cpu_time_ns();
	const int64_t cycle_us         = std::chrono::duration_cast<std::chrono::microseconds>(cycle_end - cycle_start).count();
	const int64_t cpu_us           = (cycle_cpu_end_ns - cycle_cpu_start_ns) / 1000;
	const int64_t off_cpu_us       = cycle_us > cpu_us ? cycle_us - cpu_us : 0;
	cycle.cycle_us                 = cycle_us;
	cycle.cpu_us                   = cpu_us;
	cycle.off_cpu_us               = off_cpu_us;
	cycle.overhead_us              = cycle.cycle_us - cycle.recv_total_us;

	// Обязательно записываем каждый цикл, включая цикл overflow.
	rx_cycle_trace.push(cycle);

	if (overflow_detected) {
		dump_rx_traces(rx_cycle_trace.snapshot());
	}

	buffer_idx = (buffer_idx + 1) % TX_BUFFER_COUNT;
}

void GlobalData::u220_recv_dma() {
	const size_t first_board   = u220_group.active_boards()[0];
	static size_t u220_cycles  = 0;
	static bool pending_buffer = false;
	static int u220_buffer_idx = 0;
	auto packet_cnt            = u220_group.board(first_board)->get_stats().rx_packet_cnt;

	if (!u220_cycles) {
		t_start = PISystemTime::current();
	}

	static size_t counter                  = 0;
	static uint64_t sum_processing_backlog = 0;
	static size_t max_processing_backlog   = 0;
	static size_t max_total_backlog        = 0;

	const size_t processed                 = dma.get_rx_processed();
	const size_t processing_backlog        = dma.get_completed() - processed;
	const size_t total_backlog             = dma.get_submitted() - processed;

	sum_processing_backlog += processing_backlog;
	max_processing_backlog = std::max(max_processing_backlog, processing_backlog);
	max_total_backlog      = std::max(max_total_backlog, total_backlog);

	// if (++counter % 1000 == 0) {
	// 	fprintf(stderr,
	// 	        "RX backlog avg=%.2f max=%zu total_max=%zu headroom=%zu\n",
	// 	        static_cast<double>(sum_processing_backlog) / counter,
	// 	        max_processing_backlog,
	// 	        max_total_backlog,
	// 	        max_total_backlog < RX_BUFFER_COUNT ? RX_BUFFER_COUNT - max_total_backlog : 0);
	// }

	if (dma.can_send()) {
		if (pending_buffer) {
			int ret         = dma.send();
			u220_buffer_idx = dma.get_submitted() % TX_BUFFER_COUNT;
			if (ret != 0) {
				fprintf(stderr, "TX ERROR transaction=%zu ret=%d\n", dma.get_submitted() - 1, ret);
			}
		}
		pending_buffer = !cpm_poi.skip_dma_transaction.load();
		for (const auto board_index: u220_group.active_boards()) {
			const auto error = u220_group.board(board_index)->receive(u220_buffer_idx);
			pending_buffer &= (error == uhd::rx_metadata_t::ERROR_CODE_NONE);
		}
		u220_cycles = u220_group.board(first_board)->get_stats().cycles_completed;
		return;
	}

	int ret = dma.receive();
	if (ret != 0) {
		fprintf(stderr, "RX ERROR transaction=%zu ret=%d\n", dma.get_completed(), ret);
		u220_recv_thread.stop();
	}
}

void GlobalData::process_rx_buffers_software() {
	sdr_buffer_info info;
	{
		std::lock_guard<std::mutex> lock(sdr_rx.mutex);
		if (!sdr_rx.ready) return;
		info         = sdr_rx.buffer;
		sdr_rx.ready = false;
	}
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	ans.setData(info.buffer, static_cast<int>(info.samples));
	global->sendMessage(ans);
}

void GlobalData::process_rx_buffers_dma() {
	while (dma.get_rx_ready_count() > 0) {
		const auto info     = dma.get_rx_buffer();
		const auto * buffer = info.buffer;
		const auto * hdr    = reinterpret_cast<const rls::header *>(buffer);
		if (cfg.log_axu_hdr) {
			axu_header_logger.write(*hdr);
		}
		static size_t last_packet_number = 0;
		size_t cur_packet_number         = hdr->packet_number;
		if (((cur_packet_number - last_packet_number)) != 20) {
			piCout << "Pac drop, n_cur_pac: " << PICoutManipulators::Hex << cur_packet_number << "; n_last_pac: " << last_packet_number;
			cpm_poi.increment_dma_packet_drops();
		}
		last_packet_number = cur_packet_number;

		cpm_poi.set_rx_header(*hdr);
		static size_t cnt = 0;
		if (hdr->tp == rls::TP_WORK) {
			process_work_pac(buffer);
		} else {
			process_kt_pac(buffer);
		}
		dma.mark_rx_processed();
	}
}

void GlobalData::process_work_pac(const uint32_t * buffer) {
	static_assert(sizeof(rls::header) == 24, "Invalid header size");
	static_assert(sizeof(rls::work_posthdr) == 8, "Invalid work_posthdr size");
	static_assert(sizeof(rls::noise_formular) == 68, "Invalid noise_formular size");

	const auto * pac = reinterpret_cast<const rls::work_packet *>(buffer);
	cpm_poi.set_zona_val(pac->nf);
	if (cpm_poi.set_fap_pr_coeffs(pac->cf)) {
		global->sendMessage(set_FAP_PR_KVIT());
	}
	auto nes              = pac->posthdr.n_formulars;
	size_t packet_number  = pac->hdr.packet_number;
	auto * board          = u220_group.board(u220_group.active_boards()[0]);
	const double uhd_time = board->rx_buffer_time_info.get_time(packet_number);
	const auto host_time  = u220_group.to_host_time(uhd_time);
	auto targ_az          = cpm_rr.get_compensated_az(host_time);
	if (nes != 0) {
		// auto targ_az = cpm_rr.get_compensated_az();

		for (size_t i = 0; i < nes; i++) {
			const auto & f = pac->formulars[i];
			auto range_idx = f.range;
			if (range_idx < 13 && range_idx > 130) {
				continue;
			}

			struct target targ {};
			targ.n      = cpm_poi.target_idx++;
			targ.time   = cpm_rr.get_compensated_time();
			targ.az     = targ_az;

			auto um_idx = f.main_diagram_number;
			// targ.um        = (double)111packet.main_amplitude / (double)packet.neighbor_amplitude;
			targ.um     = rls::UM_VALUES[um_idx];
			targ.d      = rls::range_index_to_meters(range_idx) + cpm_rr.get_dd_poi();
			targ.porog  = f.rank_out * cpm_poi.get_apu_k1();
			targ.amp    = (double)f.main_amplitude;
			targ.vr     = rls::freq_index_to_velocity(f.frequency_channel);
			// rls::print_work(&pac->posthdr);
			send_KTA_VO(targ);
		}
	}
	static size_t cnt = 0;
	if ((cnt++ & 0x1) == 0x1) {
		const auto & range_pac = *get_range_packet(pac);
		send_PI(range_pac, targ_az);
		if (range_pac.n_ranges != 0) {
			// piCout << packet_number;
			// rls::print_ranges(&range_pac);
		}
	}
}

void GlobalData::process_kt_pac(const uint32_t * buffer) {
	const auto * hdr = reinterpret_cast<const rls::header *>(buffer);
	size_t words     = 0;
	{
		std::lock_guard<std::mutex> lock(pending_tk_mutex);
		if (!pending_tk.active || hdr->tp != pending_tk.kt || hdr->range != pending_tk.rg || hdr->channel != pending_tk.nkan) {
			return;
		}
		words             = pending_tk.words;
		pending_tk.active = false;
		// cpm_poi.skip_dma_transaction.store(true);
		axi_dsp_set_output_source(0, 0, 0, 0);
		axi_dsp_apply();
		// cpm_poi.skip_dma_transaction.store(false);
	}
	Protocol_RLS_Mini::POI_TK_Kvit ans;
	ans.setData(buffer, words);
	global->sendMessage(ans);
}

void GlobalData::cpm_loop() {
	using clock           = std::chrono::steady_clock;

	constexpr auto period = std::chrono::milliseconds(10);

	auto next             = clock::now();
	auto previous_vr      = cpm_rr.get_vr();
	uint32_t tick         = 0;
	uint32_t tick2        = 0;

	while (!cpm_thread.isStopping()) {
		// PI — 100 Гц
		// Проверка RR — 10 Гц
		if (++tick >= 10) {
			tick = 0;

			if (cpm_rr.get_vr() != CpmRr::RotationState::Simulator) {
				cpm_rr.sync_opu();
			}

			auto current_vr = cpm_rr.get_vr();

			if (current_vr != previous_vr) {
				previous_vr = current_vr;
				global->sendMessage(set_RR_Kvit());
			}
		}
		// Проверка stat — 1 Гц
		if (++tick2 >= 100) {
			tick2 = 0;
			for (const auto i: u220_group.active_boards()) {
				auto * fpga = u220_group.fpga(i);
				for (size_t radio = 0; radio < 2; ++radio) {
					// fpga->log_rx_playback_status(radio);
					// fpga->log_rx_playback_phase(radio);
					// fpga->log_rx_playback_enable(radio);
					// fpga->log_rx_playback_correction(radio);
					// fpga->log_rx_playback_delay(radio);
					// fpga->log_rx_playback_amplitude(radio);
					// fpga->log_rx_playback_dn_offset(radio);
				}
				// fpga->log_play_ctrl();
				fpga->log_tx_fap_val();
				// fpga->log_rx_playback_start_delay();
			}
		}

		next += period;

		// Не догоняем пропущенные интервалы
		if (next < clock::now()) next = clock::now();

		std::this_thread::sleep_until(next);
	}
}

void GlobalData::dump_rx_traces(const std::vector<RxCycleTraceEntry> & cycle_entries) {
	if (overflow_dumped) {
		return;
	}
	overflow_dumped = true;

	namespace fs    = std::filesystem;
	const auto timestamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	const std::string suffix = std::to_string(timestamp);
	const fs::path log_dir   = "logs";
	std::error_code ec;
	fs::create_directories(log_dir, ec);
	if (ec) {
		piCout << "ERROR: cannot create logs directory:" << StdString2PIString(ec.message());
		return;
	}

	const fs::path rx_filename    = log_dir / ("rx_overflow_" + suffix + ".csv");
	const fs::path cycle_filename = log_dir / ("rx_cycle_overflow_" + suffix + ".csv");
	const bool cycle_saved        = write_rx_cycle_csv(cycle_entries, cycle_filename.string());
	if (!cycle_saved) {
		piCout << "ERROR: cannot save" << StdString2PIString(cycle_filename.string());
	}

	std::vector<BoardRxTraceEntry> all_entries;
	for (const auto board_index: u220_group.active_boards()) {
		auto * board             = u220_group.board(board_index);
		const std::string serial = PIString2StdString(board->get_serial());
		const auto snapshot      = board->get_rx_trace();
		for (const auto & entry: snapshot) {
			all_entries.push_back({serial, entry});
		}
	}
	write_rx_trace_csv(std::move(all_entries), rx_filename.string());
}

void GlobalData::received_POI_TK_Zapros(const Protocol_RLS_Mini::POI_TK_Zapros & msg) {
	log_cpm_msg(__func__, msg);
	if (msg.reg_takt == 0 || msg.kt == 0) {
		auto status         = cpm_poi.get_status();
		const auto & active = u220_group.active_boards();
		if (!active.isEmpty()) status.u220_cycles = u220_group.board(active[0])->get_stats().cycles_completed;
		for (size_t i = 0; i < u220_group.board_count() && i < status.u220_rx_overflows.size(); ++i)
			status.u220_rx_overflows[i] = u220_group.board(i)->get_stats().rx_overflows;
		{
			std::lock_guard<std::mutex> lock(pending_tk_mutex);
			pending_tk.active = false;
		}
		Protocol_RLS_Mini::POI_TK_Kvit ans;
		ans.setData(&status, 1);
		ans.nw = static_cast<uint16_t>(ans.words.size());
		global->sendMessage(ans);
		return;
	}
	if (cfg.dma_enabled) {
		std::lock_guard<std::mutex> lock(pending_tk_mutex);
		const auto tp = static_cast<rls::tp>(msg.kt);
		if (tp == rls::TP_SDR) return;
		pending_tk.kt       = msg.kt;
		pending_tk.rg       = rls::kt_uses_rg(tp) ? msg.rg : 0;
		pending_tk.nkan     = rls::kt_uses_channel(tp) ? msg.nkan : 0;
		pending_tk.reg_takt = msg.reg_takt;
		pending_tk.words    = rls::get_rx_words_per_buf(msg.kt, cfg.rx_insert_count);
		pending_tk.active =
			msg.reg_takt == 1 && msg.kt != rls::TP_WORK && pending_tk.words > 0 && pending_tk.words <= BUFFER_SIZE / sizeof(uint32_t);

		// cpm_poi.skip_dma_transaction.store(true);
		axi_dsp_set_output_source(pending_tk.kt, 0, pending_tk.nkan, pending_tk.rg);
		axi_dsp_apply();
		// cpm_poi.skip_dma_transaction.store(false);
	} else {
		std::lock_guard<std::mutex> lock(pending_tk_mutex);

		pending_tk.kt     = msg.kt;
		pending_tk.nkan   = msg.nkan;
		pending_tk.words  = (232 + 2 * cfg.rx_insert_count) * 3;
		pending_tk.active = msg.kt == rls::TP_SDR && msg.reg_takt == 1 && msg.nkan < 8 && (cpm_poi.get_ispr_kan() & (1u << msg.nkan));
	}
}

Protocol_RLS_Mini::RR_Kvit GlobalData::set_RR_Kvit() {
	Protocol_RLS_Mini::RR_Kvit kvit;
	kvit.kuvr     = static_cast<uint8_t>(cpm_rr.get_kuvr());
	kvit.vr       = static_cast<uint8_t>(cpm_rr.get_vr());
	kvit.kuizl    = cpm_rr.get_kuizl();
	kvit.izl      = cpm_rr.get_izl();
	kvit.ant      = cpm_rr.get_ant();
	kvit.ispr_kan = cpm_poi.get_ispr_kan();
	kvit.setDegreesDaz(cpm_rr.get_daz());
	kvit.dd_poi = static_cast<int16_t>(cpm_rr.get_dd_poi());
	kvit.setDegreesDazPOI(cpm_rr.get_daz_poi());
	return kvit;
}

void GlobalData::received_RR_Zapros(const Protocol_RLS_Mini::RR_Zapros & msg) {
	log_cpm_msg(__func__, msg);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Vr(const Protocol_RLS_Mini::RR_Vr & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_vr(static_cast<CpmRr::RotationCommand>(msg.par));
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_Izl(const Protocol_RLS_Mini::RR_Izl & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_izl(msg.par == 1);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	// TODO: убрать из фин версиии
	// if (!cfg.dma_enabled) {
	// 	u220_group.start_rx_dump();
	// }
	global->sendMessage(ans);
}

void GlobalData::received_RR_Ant(const Protocol_RLS_Mini::RR_Ant & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_ant(msg.par == 1);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_TTek(const Protocol_RLS_Mini::RR_TTek & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_current_time(msg.getSeconds());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_AzPopr(const Protocol_RLS_Mini::RR_AzPopr & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_daz(msg.getDegrees());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_DPopr_POI(const Protocol_RLS_Mini::RR_DPopr_POI & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_dd_poi(msg.dd);
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_RR_AzPopr_POI(const Protocol_RLS_Mini::RR_AzPopr_POI & msg) {
	log_cpm_msg(__func__, msg);
	cpm_rr.set_daz_poi(msg.getDegrees());
	Protocol_RLS_Mini::RR_Kvit ans = set_RR_Kvit();
	global->sendMessage(ans);
}

Protocol_RLS_Mini::FAP_PR_KVIT GlobalData::set_FAP_PR_KVIT() {
	Protocol_RLS_Mini::FAP_PR_KVIT kvit;
	const auto & coeffs         = cpm_poi.get_fap_pr_coeffs();
	constexpr double rad_to_deg = 180.0 / 3.14159265358979323846;
	for (size_t i = 0; i < coeffs.size(); ++i) {
		kvit.channels[i].setDegrees(std::arg(coeffs[i]) * rad_to_deg);
		kvit.channels[i].setDb(20.0 * std::log10(std::abs(coeffs[i])));
	}
	return kvit;
}

void GlobalData::received_FAP_PR_ZAPROS(const Protocol_RLS_Mini::FAP_PR_ZAPROS & msg) {
	log_cpm_msg(__func__, msg);
	global->sendMessage(set_FAP_PR_KVIT());
}

void GlobalData::received_FAP_PR_KU(const Protocol_RLS_Mini::FAP_PR_KU & msg) {
	log_cpm_msg(__func__, msg);
	cpm_poi.start_fap_pr();
}

void GlobalData::received_FAP_PR_TEST(const Protocol_RLS_Mini::FAP_PR_TEST & msg) {
	log_cpm_msg(__func__, msg);
	if (msg.msg_code != 2 && msg.msg_code != 3) return;

	// TODO: включить или выключить режим проверки фазирования ПР.
	// При включении отправить FAP_PR_KVIT с TEST=1
	// и рассчитанными проверочными значениями.
	// При выключении восстановить текущие поправки
	// и отправить FAP_PR_KVIT с TEST=0.
}

void GlobalData::received_FAP_PR_POPR(const Protocol_RLS_Mini::FAP_PR_POPR & msg) {
	log_cpm_msg(__func__, msg);
	std::array<std::complex<float>, 8> coeffs;
	constexpr float deg_to_rad = 3.14159265358979323846f / 180.0f;
	for (size_t i = 0; i < coeffs.size(); ++i) {
		const float phase     = msg.channels[i].getDegrees() * deg_to_rad;
		const float amplitude = std::pow(10.0f, msg.channels[i].getDb() / 20.0f);
		coeffs[i]             = std::polar(amplitude, phase);
	}
	cpm_poi.set_fap_pr_coeffs(coeffs);
	global->sendMessage(set_FAP_PR_KVIT());
}

void GlobalData::send_KTA_VO(struct target targ) {
	Protocol_RLS_Mini::KTA_VO ans;
	ans.n = targ.n;
	ans.setT(targ.time);
	ans.setDegreesAz(targ.az);
	ans.setDegreesUm(targ.um);
	ans.setDistance(targ.d);
	ans.setAmpPorogSp(targ.amp, targ.porog);
	ans.setVelocity(targ.vr);
	// piCout << ans;
	global->sendMessage(ans);
}

void GlobalData::send_PI(const rls::range_packet & range_pac, float targ_az) {
	Protocol_RLS_Mini::PI pi;
	pi.setDegreesAzTek(targ_az);
	pi.setSeconds(cpm_rr.get_compensated_time());
	pi.setDegreesAz(targ_az);
	pi.zona             = cpm_poi.get_zona_val_db();
	pi.nes              = range_pac.n_ranges;
	PIByteArray data    = piSerialize(pi);
	const size_t offset = data.size();
	data.resize(offset + pi.nes * sizeof(uint16_t));
	auto * ranges = reinterpret_cast<uint16_t *>(data.data() + offset);
	for (size_t i = 0; i < pi.nes; ++i) {
		double range = rls::range_index_to_meters(range_pac.ranges[i]);
		range += cpm_rr.get_dd_poi();
		ranges[i] = static_cast<uint16_t>(range);
	}
	static size_t cnt = 0;
	if ((cnt++ & 0xFF) == 0xFF) {
		piCout << pi;
	}

	global->sendMessage(data, Protocol_RLS_Mini::PI::Port);
}

Protocol_RLS_Mini::PI GlobalData::set_PI() {
	Protocol_RLS_Mini::PI pi;
	pi.setDegreesAzTek(cpm_rr.get_current_az());
	pi.setSeconds(cpm_rr.get_compensated_time());
	pi.setDegreesAz(cpm_rr.get_compensated_az());
	pi.zona = cpm_poi.get_zona_val_db();
	pi.nes  = cpm_poi.nes.count;
	return pi;
}

Protocol_RLS_Mini::POI_Kvit GlobalData::set_POI_Kvit() {
	Protocol_RLS_Mini::POI_Kvit kvit;
	kvit.sdc      = cpm_poi.get_sdc().bz & 0x01;
	kvit.dsa      = cpm_poi.get_dsa();
	kvit.kan      = cpm_poi.get_kan();
	kvit.dsa_vr_n = cpm_poi.get_dsa_vr_n();
	kvit.dsa_vr_k = cpm_poi.get_dsa_vr_k();
	kvit.setLevelApuk1(cpm_poi.get_apu_k1());
	kvit.setLevelApuk2(cpm_poi.get_apu_k2());
	kvit.zona_k1 = static_cast<uint8_t>(cpm_poi.get_zona_stage());
	kvit.zona_k2 = cpm_poi.get_zona_ch_num();
	return kvit;
}

Protocol_RLS_Mini::CIT_Kvit GlobalData::set_CIT_Kvit() {
	const auto targets = cpm_cit.get_targets();
	Protocol_RLS_Mini::CIT_Kvit kvit;
	Protocol_RLS_Mini::ImitatorTarget * dst[] = {&kvit.it1, &kvit.it2};
	for (size_t i = 0; i < 2; ++i) {
		const auto & src = targets[i];
		dst[i]->on       = src.on;
		dst[i]->reg      = static_cast<uint8_t>(src.dn_enable);
		dst[i]->setDegreesAz(src.az);
		dst[i]->setMeters(src.distance);
		dst[i]->setDegreesUm(src.um);
		dst[i]->setVelocity(src.vr);
		dst[i]->setAttenuation(src.osl);
	}
	return kvit;
}

void GlobalData::received_POI_Zapros(const Protocol_RLS_Mini::POI_Zapros & msg) {
	log_cpm_msg(__func__, msg);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_Shtat(const Protocol_RLS_Mini::POI_Shtat & msg) {
	log_cpm_msg(__func__, msg);
	cpm_poi.set_shtat();
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_SDC(const Protocol_RLS_Mini::POI_SDC & msg) {
	log_cpm_msg(__func__, msg);
	CpmPoi::Sdc sdc{.val = msg.bits};
	cpm_poi.set_sdc(sdc);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_DSA(const Protocol_RLS_Mini::POI_DSA & msg) {
	log_cpm_msg(__func__, msg);
	cpm_poi.set_dsa(msg.par == 1, msg.vr_n, msg.vr_k);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_APU(const Protocol_RLS_Mini::POI_APU & msg) {
	log_cpm_msg(__func__, msg);
	auto k2 = msg.getLevelk2();
	cpm_poi.set_apu(msg.getLevelk1(), k2);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_Zona(const Protocol_RLS_Mini::POI_Zona & msg) {
	log_cpm_msg(__func__, msg);
	if (msg.k1 <= 1 && msg.k2 <= 7) {
		cpm_poi.set_zona_params(static_cast<CpmPoi::ZoneCalcStage>(msg.k1), msg.k2);
	} else {
		piCout << "Error: k1 =" << msg.k1 << "; k2 =" << msg.k2;
	}
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_POI_Kan(const Protocol_RLS_Mini::POI_Kan & msg) {
	log_cpm_msg(__func__, msg);
	cpm_poi.set_kan(msg.kan);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_CIT_Zapros(const Protocol_RLS_Mini::CIT_Zapros & msg) {
	log_cpm_msg(__func__, msg);
	Protocol_RLS_Mini::CIT_Kvit ans = set_CIT_Kvit();
	global->sendMessage(ans);
}

void GlobalData::received_CIT_KU(const Protocol_RLS_Mini::CIT_KU & msg) {
	log_cpm_msg(__func__, msg);
	const Protocol_RLS_Mini::ImitatorTarget * src[] = {&msg.it1, &msg.it2};
	CpmCit::Targets targets{};
	for (size_t i = 0; i < 2; ++i) {
		auto & dst    = targets[i];
		dst.on        = src[i]->on != 0;
		dst.dn_enable = src[i]->reg == 1;
		dst.az        = src[i]->getDegreesAz();
		dst.distance  = src[i]->getMeters();
		dst.um        = src[i]->getDegreesUm();
		dst.vr        = src[i]->getVelocity();
		dst.osl       = src[i]->getAttenuation();
	}
	cpm_cit.set_targets(targets, 2);
	global->sendMessage(set_CIT_Kvit());
}
