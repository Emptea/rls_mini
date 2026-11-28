#pragma once

#include "../rls_fpga/rls.hpp"
#include "axi_dsp.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>

class CpmPoi {
public:
	static constexpr std::size_t N       = 512;
	static constexpr double FREQ         = 9.23e9;
	static constexpr double SAMPLE_FREQ  = 5e6;
	static constexpr double PULSE_PERIOD = 232.0 / SAMPLE_FREQ;
	static constexpr double LAMBDA       = 299792458.0 / FREQ;

	static constexpr auto VR_ARRAY       = [] {
        std::array<double, N> v{};
        constexpr double step = -LAMBDA / (2.0 * PULSE_PERIOD * N);

        for (std::size_t i = 0; i < N; ++i)
            v[i] = (i < N / 2 ? int(i) : int(i) - int(N)) * step;

        return v;
	}();

	// POI_TK_Kvit payload: firmware versions, board counters, then the latest DMA header and DMA packet drop counter.
#pragma pack(push, 1)
	struct Status {
		csr_ip_ver_t axu_ip_version{}; // Raw layout: MIN_VER[15:0], MAJ_VER[31:16].
		uint32_t u220_ip_version = 0;  // get_fpga_version(): low 24 bits.
		uint64_t u220_cycles     = 0;
		std::array<uint64_t, 4> u220_rx_overflows{};
		uint32_t packet_number        = 0;
		uint64_t timestamp            = 0;
		uint64_t dma_packet_drops     = 0; // Number of detected packet sequence gaps.
		uint32_t is_techlaser_present = 0;
	};
#pragma pack(pop)
	static_assert(sizeof(Status) == 72, "Unexpected POI status payload size");

	void set_ip_versions(const csr_ip_ver_t & axu, uint32_t u220) {
		std::lock_guard<std::mutex> lock(status_mutex);
		status.axu_ip_version  = axu;
		status.u220_ip_version = u220;
	}

	void set_rx_header(const rls::header & hdr) {
		std::lock_guard<std::mutex> lock(status_mutex);
		status.packet_number = hdr.packet_number;
		status.timestamp     = hdr.timestamp;
	}

	void increment_dma_packet_drops() {
		std::lock_guard<std::mutex> lock(status_mutex);
		status.dma_packet_drops++;
	}

	Status get_status() const {
		std::lock_guard<std::mutex> lock(status_mutex);
		return status;
	}

	struct Dsa {
		bool is_on;
		uint16_t vr_n;
		uint16_t vr_k;
	};

	union Sdc {
		uint16_t val;
		struct {
			uint16_t bz: 8;
			uint16_t dz: 8;
		};
	};

	enum class ZoneCalcStage : uint16_t {
		BeforeLou = 0,
		AfterLou  = 1,
	};

	struct Nes {
		uint16_t count = 0;
		std::array<uint16_t, 512> data{};

		void push(uint16_t value) {
			if (count < data.size()) data[count++] = value;
		}

		void clear() { count = 0; }
	};

	Nes nes;

	uint16_t target_idx = 0;
	std::atomic<bool> skip_dma_transaction{false};

	explicit CpmPoi(bool dma_enabled, uint32_t fap_ref_level, uint32_t adc_noise_level)
		: dma_enabled(dma_enabled)
		, fap_ref_level(fap_ref_level) {
		if (adc_noise_level == 0) {
			adc_noise_level = 10;
		}

		zona_max_val_db = 3000.0 + 50.0 * 20.0 * std::log10(adc_noise_level);
	}

	// SDC — селекция движущихся целей
	Sdc get_sdc() const {
		// if (!dma_enabled) return sdc;
		return poi_sdc;
	}

	void set_sdc(Sdc sdc) {
		poi_sdc = sdc;
		if (!dma_enabled) return;
		axi_dsp_set_motion_selector(poi_sdc.bz, poi_sdc.dz, 1);
		axi_dsp_apply();
	}

	// DSA — диапазон скоростей автокомпенсации

	void set_target_velocity_range(double min, double max) {
		if (min < 0 || max < min) return;
		target_velocity_min = min;
		target_velocity_max = max;
		config_frequency_mask.fill(0);

		for (std::size_t i = 0; i < N; ++i) {
			const double speed = std::abs(VR_ARRAY[i]);
			if (speed >= min && speed <= max) {
				config_frequency_mask[i / 32] |= uint32_t{1} << (i % 32);
			}
		}
	}

	bool get_dsa() const { return dsa_is_on; }
	uint16_t get_dsa_vr_n() const { return dsa_vr_n; }
	uint16_t get_dsa_vr_k() const { return dsa_vr_k; }

	void set_dsa(bool onoff, uint16_t vr_n, uint16_t vr_k) {
		if (onoff && vr_n > vr_k) return;
		dsa_is_on = onoff;
		dsa_vr_n  = vr_n;
		dsa_vr_k  = vr_k;
		if (!dma_enabled) return;
		skip_dma_transaction.store(true);
		apply_frequency_selection();
		axi_dsp_apply();
		skip_dma_transaction.store(false);
	}

	// APU — пороги обнаружения
	// k1 порог медианного фильтра (т.к. амлпитуда в квадрате, то порог тоже квадрат), дробное, больше нуля
	// k2 порог P в алгоритме, целое, больше нуля
	double get_apu_k1() const {
		if (!dma_enabled) return apu_k1;
		return apu_k1;
		// return axi_dsp_get_detector_level(0);
	}
	uint32_t get_apu_k2() const {
		if (!dma_enabled) return apu_k2;
		return apu_k2;
		// return axi_dsp_get_detector_level(1);
	}

	void set_apu(double k1, uint32_t k2) {
		apu_k1 = k1;
		apu_k2 = k2;
		if (!dma_enabled) return;
		axi_dsp_set_median_detector_coef(apu_k1);
		axi_dsp_set_cfar_level(0);
		axi_dsp_apply();
	}

	// ZONA — параметры расчёта зоны
	void set_zona_params(ZoneCalcStage stage, uint16_t ch_num) {
		if (ch_num >= 8) return;
		zona_stage  = stage;
		zona_ch_num = ch_num;
	}

	ZoneCalcStage get_zona_stage() const { return zona_stage; }
	uint16_t get_zona_ch_num() const { return zona_ch_num; }

	void set_zona_val(const rls::noise_formular & nf) {
		if (zona_ch_num >= 8) return;
		switch (zona_stage) {
		case ZoneCalcStage::BeforeLou: {
			zona_val_sq = nf.noise_fap[zona_ch_num];
		} break;
		case ZoneCalcStage::AfterLou: {
			zona_val_sq = nf.noise_lou[zona_ch_num];
		} break;
		}
	}

	uint16_t get_zona_val_db() const {
		if (zona_val_sq == 0) return 65535;
		const auto z = std::lround(zona_max_val_db - 500.0 * std::log10(zona_val_sq));
		return static_cast<uint16_t>(std::clamp(z, 0L, 65535L));
	}

	// ISPR_KAN — исправность приёмных каналов
	// Целое без знака, 0–255
	uint8_t get_ispr_kan() const { return ispr_kan; }
	void set_ispr_kan(uint8_t value) { ispr_kan = value; }

	// KAN — маска подключённых каналов
	uint8_t get_kan() const {
		return kan & ispr_kan;
		// return axi_dsp_get_channel_mask();
	}

	void set_kan(uint8_t value) {
		kan = value & ispr_kan;
		if (!dma_enabled) return;
		axi_dsp_set_channel_mask(value, ~ispr_kan);
		axi_dsp_apply();
	}

	// Штатные настройки ПОИ
	void set_shtat() {
		poi_sdc.val = 0;
		dsa_is_on   = false;

		dsa_vr_n    = 0;
		dsa_vr_k    = 0;

		apu_k1      = 36;
		apu_k2      = 1;

		zona_stage  = ZoneCalcStage::BeforeLou;
		zona_ch_num = 0;

		kan         = 0xFF & ispr_kan;

		if (!dma_enabled) return;

		skip_dma_transaction.store(true);
		axi_dsp_set_compensation_mode(1);
		axi_dsp_set_channel_mask(kan, ~ispr_kan);
		axi_dsp_set_output_source(0, 0, 0, 0);
		axi_dsp_set_compensation_ref(fap_ref_level);
		axi_dsp_set_motion_selector(poi_sdc.bz, poi_sdc.dz, 1);
		axi_dsp_set_median_detector_coef(apu_k1);
		axi_dsp_set_cfar_level(0);
		apply_frequency_selection();
		axi_dsp_apply();
		skip_dma_transaction.store(false);
	}

	void set_dma_enabled(bool value) { dma_enabled = value; }

	void start_fap_pr() {
		if (!dma_enabled) return;
		std::lock_guard<std::mutex> lock(fap_pr_mutex);
		fap_pr_pending = true;
	}

	bool is_fap_pr_pending() const {
		std::lock_guard<std::mutex> lock(fap_pr_mutex);
		return fap_pr_pending;
	}

	uint32_t get_fap_test() const { return fap_test.load(); }

	void set_fap_test(uint32_t test) { fap_test.store(test); }

	std::array<std::complex<float>, 8> get_fap_pr_coeffs() const {
		std::lock_guard<std::mutex> lock(fap_pr_mutex);
		return fap_pr_coeffs;
	}

	bool set_fap_pr_coeffs(const rls::compens_formular & cf) {
		std::lock_guard<std::mutex> lock(fap_pr_mutex);

		if (!fap_pr_pending) return false;

		for (size_t i = 0; i < fap_pr_coeffs.size(); ++i) {
			fap_pr_coeffs[i] = {static_cast<float>(cf.channels[i].re) / 16384.0f, static_cast<float>(cf.channels[i].im) / 16384.0f};
		}

		fap_pr_pending = false;
		return true;
	}

	void set_fap_pr_coeffs(const std::array<std::complex<float>, 8> & coeffs) {
		std::lock_guard<std::mutex> lock(fap_pr_mutex);
		fap_pr_coeffs  = coeffs;
		fap_pr_pending = false;
		if (!dma_enabled) return;

		for (size_t i = 0; i < coeffs.size(); ++i) {
			cmplx_f64 c = {.real = coeffs[i].real(), .imag = coeffs[i].imag()};
			axi_dsp_set_manual_compensation(c, i);
		}

		axi_dsp_apply();
	}

	void set_geterodin(const std::array<const cmplx_f64 *, 8> & lou_coeffs, uint16_t n_get) {
		if (dma_enabled) {
			for (uint32_t i = 0; i < 8; ++i) {
				axi_dsp_set_diagram_0(lou_coeffs[0][i], i);
				axi_dsp_set_diagram_1(lou_coeffs[1][i], i);
				axi_dsp_set_diagram_2(lou_coeffs[2][i], i);
				axi_dsp_set_diagram_3(lou_coeffs[3][i], i);
				axi_dsp_set_diagram_4(lou_coeffs[4][i], i);
				axi_dsp_set_diagram_5(lou_coeffs[5][i], i);
				axi_dsp_set_diagram_6(lou_coeffs[6][i], i);
				axi_dsp_set_diagram_7(lou_coeffs[7][i], i);
				// axi_dsp_set_band_stop_edges(i, {0, 0, 0, 0});
			}
			axi_dsp_apply();
		}
		n_geterodin = n_get;
	}

	uint16_t get_geterodin(){ return n_geterodin; }

	void init(bool dma,
	          uint8_t ispr,
	          double velocity_min,
	          double velocity_max,
	          uint32_t u220_ip_version,
	          uint32_t rx_insert_count,
	          const std::array<std::complex<float>, 8> & fap_coeffs,
	          const std::array<const cmplx_f64 *, 8> & lou_coeffs) {
		dma_enabled   = dma;
		ispr_kan      = ispr;
		fap_pr_coeffs = fap_coeffs;

		csr_ip_ver_t axu_ip_version{};

		if (dma_enabled) {
			axi_dsp_init();
			axi_dsp_configure();
			axi_dsp_set_expect_u220_del(rx_insert_count);
			axu_ip_version = axi_dsp_get_ip_ver();
			for (uint32_t i = 0; i < 8; ++i) {
				axi_dsp_set_band_stop_edges(i, {0, 39, 40, 140});
				cmplx_f64 c = {.real = fap_coeffs[i].real(), .imag = fap_coeffs[i].imag()};
				axi_dsp_set_manual_compensation(c, i);
				axi_dsp_set_diagram_0(lou_coeffs[0][i], i);
				axi_dsp_set_diagram_1(lou_coeffs[1][i], i);
				axi_dsp_set_diagram_2(lou_coeffs[2][i], i);
				axi_dsp_set_diagram_3(lou_coeffs[3][i], i);
				axi_dsp_set_diagram_4(lou_coeffs[4][i], i);
				axi_dsp_set_diagram_5(lou_coeffs[5][i], i);
				axi_dsp_set_diagram_6(lou_coeffs[6][i], i);
				axi_dsp_set_diagram_7(lou_coeffs[7][i], i);
				// axi_dsp_set_band_stop_edges(i, {0, 0, 0, 0});
			}
			axi_dsp_apply();
		}

		set_ip_versions(axu_ip_version, u220_ip_version);
		set_target_velocity_range(velocity_min, velocity_max);
		set_shtat();
	}

private:
	bool dma_enabled = false;

	Sdc poi_sdc{};

	bool dsa_is_on             = false;

	uint8_t ispr_kan           = 0xff; // Исправность приёмных каналов
	uint8_t kan                = 0xff;

	double target_velocity_min = 2.05;
	double target_velocity_max = 51.27;
	uint32_t fap_ref_level     = 1575;
	std::array<uint32_t, N / 32> config_frequency_mask{};

	uint16_t dsa_vr_n        = 0;
	uint16_t dsa_vr_k        = 0;

	double apu_k1            = 36;
	uint32_t apu_k2          = 1;


	ZoneCalcStage zona_stage = ZoneCalcStage::BeforeLou;
	uint16_t zona_ch_num     = 0;
	uint32_t zona_val_sq     = 1;
	double zona_max_val_db   = 4000;

	uint16_t n_geterodin     = 0;

	mutable std::mutex status_mutex;
	Status status;

	bool fap_pr_pending = false;
	std::atomic<uint32_t> fap_test{0};
	mutable std::mutex fap_pr_mutex;
	std::array<std::complex<float>, 8> fap_pr_coeffs = [] {
		std::array<std::complex<float>, 8> coeffs;
		coeffs.fill({1.0f, 0.0f});
		return coeffs;
	}();

	void apply_frequency_selection() {
		if (!dma_enabled) return;

		auto mask = config_frequency_mask;

		if (dsa_is_on) {
			for (std::size_t i = 0; i < N; ++i) {
				const double speed = std::abs(VR_ARRAY[i]);

				if (speed >= dsa_vr_n && speed <= dsa_vr_k) {
					mask[i / 32] &= ~(uint32_t{1} << (i % 32));
				}
			}
		}

		for (uint32_t i = 0; i < 16; ++i) {
			axi_dsp_set_frequency_selection(mask[i], i);
		}
	}
};