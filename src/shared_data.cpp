#include "shared_data.h"

#include "../rls_fpga/dma-proxy.h"
#include "imitator_u220.h"
#include "mini_config.hpp"
#include "protocol_rls_mini.h"
#include "user_u220.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pisemaphore.h>
#include <pistring_std.h>
#include <pitime.h>
#include <pivaluetree_conversions.h>
#include <thread>

GlobalData::GlobalData()
	: GlobalDataEth(this)
	, cfg()
	, u220_group(cfg)
	, imitator_u220(u220_group)
	, cpm_rr(techlaser, imitator_u220)
	, cpm_poi(cfg.dma_enabled)
	, cpm_cit(imitator_u220) {
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
	const auto & com_conf = main_config.child("com");
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
		axi_dsp_set_compensation_mode(1);
		axi_dsp_set_expect_u220_del(cfg.rx_insert_count);
		axi_dsp_set_output_source(0, 0, 0);
		auto v = axi_dsp_get_output_source();
		piCout << "SOURCE: " << v.SOURCE << ", SOURCE_CHANNEL: " << v.SOURCE_CHANNEL << ", RANGE_GATE: " << v.RANGE_GATE;
		axi_dsp_set_channel_mask((uint32_t)cpm_rr.get_ispr_kan());
		axi_dsp_apply();
	}
	first_transfer = false;
}

void GlobalData::start() {
	startEth();
	cpm_thread.start([this]() { cpm_loop(); });

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
	cpm_cit.set_shtat(cfg);
	// imitator_u220.set_opu(0.0, 0.0);
	cpm_rr.set_vr(CpmRr::RotationCommand::Simulator);

	imitator_u220.apply(0.0);

	global->sendMessage(set_POI_Kvit());
	global->sendMessage(set_CIT_Kvit());
	global->sendMessage(set_RR_Kvit());


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
	const size_t first_board   = u220_group.active_boards()[0];
	static size_t u220_cycles  = 0;
	static bool pending_buffer = false;
	static int u220_buffer_idx = 0;
	auto packet_cnt            = u220_group.board(first_board)->get_stats().rx_packet_cnt;

	if (!u220_cycles) {
		t_start = PISystemTime::current();
	}

	if (dma.can_send()) {
		if (pending_buffer) {
			int ret         = dma.send();
			u220_buffer_idx = dma.get_submitted() % TX_BUFFER_COUNT;
			if (ret != 0) {
				fprintf(stderr, "TX ERROR transaction=%zu ret=%d\n", dma.get_submitted() - 1, ret);
			}
		}
		pending_buffer = true;
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
		u220_recv_thread.stopAndWait();
	}

	const auto * buffer = static_cast<const uint32_t *>(dma.get_rx_buffer());
	if (buffer) {
		const auto * hdr = reinterpret_cast<const rls::header *>(buffer);

		if (hdr->tp != rls::TP_WORK) {
			const size_t words = rls::get_rx_words_per_buf(hdr->tp, cfg.rx_insert_count);

			if (words > 0 && words <= BUFFER_SIZE / sizeof(uint32_t)) {
				auto ref = dma_channel_buf.getRef();
				*ref     = PIVector<uint32_t>(buffer, words);
			}
		} else {
			process_work_pac(buffer);
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
		// if (u220_cycles > 0) {
		// 	notifier_channels.notify();
		// }
	}
}

void GlobalData::process_work_pac(const uint32_t * buffer) {
	constexpr size_t work_header_size = sizeof(rls::header) + sizeof(rls::work_posthdr);

	static_assert(sizeof(rls::header) == 24, "Invalid header size");
	static_assert(sizeof(rls::work_posthdr) == 8, "Invalid work_posthdr size");
	static_assert(sizeof(rls::work_packet) == 16, "Invalid work_packet size");
	static_assert(BUFFER_SIZE >= work_header_size, "DMA buffer too small");

	struct rls::header * hdr          = (rls::header *)buffer;
	struct rls::work_posthdr * work   = (struct rls::work_posthdr *)(hdr + 1);
	struct rls::work_packet * packets = reinterpret_cast<struct rls::work_packet *>(work + 1);
	auto nes                          = work->n_work_packets;
	auto time_delayed                 = static_cast<uint32_t>(std::round((double)(hdr->packet_number - work->packet_number) * 46.4e-3));
	static uint16_t n_taget           = 0;
	for (size_t i = 0; i < nes; i++) {
		struct rls::work_packet & packet = packets[i];
		struct target targ;
		targ.n                                   = n_taget++;
		targ.time                                = cpm_rr.get_compensated_time();
		targ.az                                  = cpm_rr.get_compensated_az();

		auto um_idx                              = packet.main_diagram_number;
		constexpr std::array<float, 8> um_values = {
			5.3125f,
			15.9375f,
			26.5625f,
			37.1875f,
			47.8125f,
			58.4375f,
			69.0625f,
			79.6875f,
		};
		// targ.um        = (double)111packet.main_amplitude / (double)packet.neighbor_amplitude;
		targ.um        = um_values[um_idx];
		auto range_idx = packet.range;
		// TODO: перенести в константы
		size_t tx_sig_len;
		if (range_idx < 101) {
			tx_sig_len = 40;
		} else {
			tx_sig_len = 6;
		}
		double range                       = (range_idx + tx_sig_len / 2) / 5e6 * 299792458.0 / 2;
		targ.d                             = range;
		targ.porog                         = packet.rank_out * cpm_poi.get_apu_k1();
		targ.amp                           = (double)packet.main_amplitude;

		constexpr double carrier_frequency = 3.6e9;
		constexpr double c                 = 299792458.0;
		constexpr double fft_size          = 512.0;
		constexpr double pulse_period      = 46.4e-6;
		const double wavelength            = c / carrier_frequency;
		const double velocity_step         = wavelength / pulse_period / (2.0 * fft_size);
		const uint32_t freq_idx            = packet.frequency_channel;
		const int doppler_bin              = freq_idx < 256 ? -static_cast<int>(freq_idx) : 512 - static_cast<int>(freq_idx);
		targ.vr                            = doppler_bin * velocity_step;
		cpm_poi.nes.push(static_cast<uint16_t>(range));
		send_KTA_VO(targ);

		printf("Target #%zu: "
		       "range=%u, "
		       "main_amplitude=%u, "
		       "neighbor_amplitude=%u, "
		       "main_diagram_number=%u, "
		       "neighbor_diagram_side=%u, "
		       "frequency_channel=%u, "
		       "rank_out=%u\n",
		       i + 1,
		       static_cast<unsigned>(packet.range),
		       packet.main_amplitude,
		       packet.neighbor_amplitude,
		       static_cast<unsigned>(packet.main_diagram_number),
		       static_cast<unsigned>(packet.neighbor_diagram_side),
		       static_cast<unsigned>(packet.frequency_channel),
		       packet.rank_out);
	}
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
		send_PI();

		// Проверка RR — 10 Гц
		if (++tick >= 10) {
			tick            = 0;

			auto current_vr = cpm_rr.get_vr();

			if (current_vr != previous_vr) {
				previous_vr = current_vr;
				global->sendMessage(set_RR_Kvit());
			}
		}
		// Проверка stat — 1 Гц
		if (++tick2 >= 100) {
			tick2 = 0;
			// for (const auto i: u220_group.active_boards()) {
			// 	auto * fpga = u220_group.fpga(i);
			// 	for (size_t radio = 0; radio < 2; ++radio) {
			// 		fpga->log_rx_playback_status(radio);
			// 		fpga->log_rx_playback_phase(radio);
			// 		fpga->log_rx_playback_enable(radio);
			// 		fpga->log_rx_playback_correction(radio);
			// 		fpga->log_rx_playback_delay(radio);
			// 		fpga->log_rx_playback_amplitude(radio);
			// 		fpga->log_rx_playback_dn_offset(radio);
			// 	}
			// 	fpga->log_rx_playback_dn_counter();
			// 	fpga->log_rx_playback_start_delay();
			// }
		}

		next += period;

		// Не догоняем пропущенные интервалы
		if (next < clock::now()) next = clock::now();

		std::this_thread::sleep_until(next);
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
	if (msg.kt != Protocol_RLS_Mini::CTRL && msg.reg_takt == 1) {
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

	// piCout << kvit;

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
	cpm_rr.set_current_time(msg.getSeconds());
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
	ans.n = targ.n;
	ans.setT(targ.time);
	ans.setDegreesAz(targ.az);
	ans.setDegreesUm(targ.um);
	ans.setDistance(targ.d);
	ans.setAmpPorogSp(targ.amp, targ.porog);
	ans.setVelocity(targ.vr);
	global->sendMessage(ans);
}


void GlobalData::send_PI() {
	Protocol_RLS_Mini::PI pi = set_PI();
	PIByteArray data         = piSerialize(pi);
	data.append(cpm_poi.nes.data.data(), pi.nes * sizeof(uint16_t));
	global->sendMessage(data, Protocol_RLS_Mini::PI::Port);
}

Protocol_RLS_Mini::PI GlobalData::set_PI() {
	Protocol_RLS_Mini::PI pi;

	pi.setDegreesAzTek(cpm_rr.get_current_az());
	pi.setSeconds(cpm_rr.get_compensated_time());
	pi.setDegreesAz(cpm_rr.get_compensated_az());

	pi.zona = cpm_poi.get_zona_val();
	pi.nes  = cpm_poi.nes.count;

	if (cpm_rr.take_rotation_completed()) {
		cpm_poi.nes.clear();
	}

	return pi;
}


Protocol_RLS_Mini::POI_Kvit GlobalData::set_POI_Kvit() {
	Protocol_RLS_Mini::POI_Kvit kvit;

	kvit.sdc      = cpm_poi.get_sdc();
	kvit.dsa      = cpm_poi.get_dsa();
	kvit.kan      = cpm_poi.get_kan();

	kvit.dsa_vr_n = cpm_poi.get_dsa_vr_n();
	kvit.dsa_vr_k = cpm_poi.get_dsa_vr_k();

	if (cpm_poi.get_apu_k1() > 0)
		kvit.setLevelApuk1(cpm_poi.get_apu_k1());
	else
		piCout << "Error: apu_k1 == 0";

	if (cpm_poi.get_apu_k2() > 0)
		kvit.setLevelApuk2(cpm_poi.get_apu_k2());
	else
		piCout << "Error: apu_k2 == 0";

	kvit.zona_k1 = static_cast<uint16_t>(cpm_poi.get_zona_stage());
	kvit.zona_k2 = cpm_poi.get_zona_ch_num();

	return kvit;
}


Protocol_RLS_Mini::CIT_Kvit GlobalData::set_CIT_Kvit() {
	Protocol_RLS_Mini::CIT_Kvit kvit;

	const auto targets                        = cpm_cit.get_targets();

	Protocol_RLS_Mini::ImitatorTarget * dst[] = {&kvit.it1, &kvit.it2};

	for (size_t i = 0; i < 2; ++i) {
		const auto & src = targets[i];

		dst[i]->reg      = static_cast<uint8_t>(src.reg);
		dst[i]->on       = src.on;
		dst[i]->setDegreesAz(src.az);
		dst[i]->setMeters(src.distance);
		dst[i]->setDegreesUm(src.um);
		dst[i]->setVelocity(src.vr);
		dst[i]->setAttenuation(src.osl);
	}

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
	// TODO: msg.getLevelk2() - непонятно почему дБ если у нас шататно ноль
	// а порог может быть нулем
	cpm_poi.set_apu(msg.getLevelk1(), 0);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Zona(const Protocol_RLS_Mini::POI_Zona & msg) {
	piCout << "rec msg" << "received_POI_Zona";
	piCout << msg;
	if (msg.k1 <= 1 && msg.k2 <= 7) {
		cpm_poi.set_zona_params(static_cast<CpmPoi::ZoneCalcStage>(msg.k1), msg.k2);
	} else {
		piCout << "Error: k1 =" << msg.k1 << "; k2 =" << msg.k2;
	}
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_POI_Kan(const Protocol_RLS_Mini::POI_Kan & msg) {
	piCout << "rec msg" << "received_POI_Kan";
	piCout << msg;
	cpm_poi.set_kan(msg.kan);
	Protocol_RLS_Mini::POI_Kvit ans = set_POI_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_CIT_Zapros(const Protocol_RLS_Mini::CIT_Zapros & msg) {
	piCout << "rec msg" << "received_CIT_Zapros";
	piCout << msg;
	Protocol_RLS_Mini::CIT_Kvit ans = set_CIT_Kvit();
	global->sendMessage(ans);
}


void GlobalData::received_CIT_KU(const Protocol_RLS_Mini::CIT_KU & msg) {
	piCout << "rec msg" << "received_CIT_KU";
	piCout << msg;

	const Protocol_RLS_Mini::ImitatorTarget * src[] = {&msg.it1, &msg.it2};

	CpmCit::Targets targets{};

	for (size_t i = 0; i < 2; ++i) {
		if (src[i]->reg > 1) {
			return;
		}

		auto & dst   = targets[i];

		dst.reg      = static_cast<CpmCit::Register>(src[i]->reg);
		dst.on       = src[i]->on != 0;
		dst.az       = src[i]->getDegreesAz();
		dst.distance = src[i]->getMeters();
		dst.um       = src[i]->getDegreesUm();
		dst.vr       = src[i]->getVelocity();
		dst.osl      = src[i]->getAttenuation();
	}

	cpm_cit.set_targets(targets, 2);

	global->sendMessage(set_CIT_Kvit());
}