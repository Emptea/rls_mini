#pragma once

#include "../rls_fpga/rls.hpp"
#include "axi_dsp.h"

#include <array>
#include <cstdint>
#include <mutex>

// TODO: нужно ли axi_dsp_apply();

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
		uint32_t packet_number    = 0;
		uint64_t timestamp        = 0;
		uint64_t dma_packet_drops = 0; // Number of detected packet sequence gaps.
	};
#pragma pack(pop)
	static_assert(sizeof(Status) == 68, "Unexpected POI status payload size");

	void set_ip_versions(csr_ip_ver_t axu, uint32_t u220) {
		std::lock_guard<std::mutex> lock(status_mutex);
		status.axu_ip_version  = axu;
		status.u220_ip_version = u220;
	}

	void set_rx_header(const rls::header & hdr) {
		std::lock_guard<std::mutex> lock(status_mutex);
		status.packet_number = hdr.packet_number;
		status.timestamp     = hdr.timestamp;
	}

	void increment_dma_packet_drops() { status.dma_packet_drops++; }

	Status get_status() const {
		std::lock_guard<std::mutex> lock(status_mutex);
		return status;
	}

	struct Dsa {
		bool is_on;
		uint16_t vr_n;
		uint16_t vr_k;
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

	explicit CpmPoi(bool dma_enabled): dma_enabled(dma_enabled) {}

	// SDC — селекция движущихся целей
	bool get_sdc() const {
		if (!dma_enabled) return sdc;
		return sdc;
		// return axi_dsp_get_motion_selector().ONOFF;
	}

	void set_sdc(bool value) {
		if (!dma_enabled) return;

		axi_dsp_set_motion_selector(1, value);
		sdc = value;
	}

	// DSA — диапазон скоростей автокомпенсации
	bool get_dsa() const { return dsa_is_on; }
	uint16_t get_dsa_vr_n() const { return dsa_vr_n; }
	uint16_t get_dsa_vr_k() const { return dsa_vr_k; }

	void set_dsa(bool onoff, uint16_t vr_n, uint16_t vr_k) {
		dsa_is_on = onoff;
		dsa_vr_n  = vr_n;
		dsa_vr_k  = vr_k;

		std::array<uint32_t, N / 32> mask;
		mask.fill(~uint32_t{0});

		for (std::size_t i = 0; i < N; ++i) {
			const double speed = VR_ARRAY[i] < 0 ? -VR_ARRAY[i] : VR_ARRAY[i];

			if (speed >= dsa_vr_n && speed <= dsa_vr_k) mask[i / 32] &= ~(uint32_t{1} << (i % 32));
		}

		for (uint32_t reg_index = 0; reg_index < 16; ++reg_index) {
			axi_dsp_set_frequency_selection(mask[reg_index], reg_index);
		}
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
		axi_dsp_set_cfar_level(apu_k2);
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
	uint32_t get_zona_val() const { return zona_val_sq; }

	// ISPR_KAN — исправность приёмных каналов
	// Целое без знака, 0–255
	uint8_t get_ispr_kan() const { return ispr_kan; }
	void set_ispr_kan(uint8_t value) { ispr_kan = value; }

	// KAN — маска подключённых каналов
	uint8_t get_kan() const {
		if (dma_enabled) return kan & ispr_kan;
		return kan & ispr_kan;
		// return axi_dsp_get_channel_mask();
	}

	void set_kan(uint8_t value) {
		kan = value & ispr_kan;
		if (!dma_enabled) return;
		axi_dsp_set_channel_mask(uint32_t(value));
	}

	// Штатные настройки ПОИ
	void set_shtat() {
		sdc       = true;
		dsa_is_on = false;

		set_kan(0xFF);

		dsa_vr_n    = 0;
		dsa_vr_k    = 0;

		apu_k1      = 36;
		apu_k2      = 1;

		zona_stage  = ZoneCalcStage::BeforeLou;
		zona_ch_num = 0;

		if (!dma_enabled) return;

		axi_dsp_set_compensation_mode(0);
		axi_dsp_set_compensation_ref(uint32_t(1575));
		axi_dsp_set_motion_selector(1, sdc);
		axi_dsp_set_median_detector_coef(apu_k1);
		axi_dsp_set_cfar_level(apu_k2);
		axi_dsp_apply();
	}

	void set_dma_enabled(bool value) { dma_enabled = value; }

private:
	mutable std::mutex status_mutex;
	Status status;

	bool dma_enabled         = false;

	bool sdc                 = false;
	bool dsa_is_on           = false;

	uint8_t ispr_kan         = 0xff; // Исправность приёмных каналов
	uint8_t kan              = 0xff;

	uint16_t dsa_vr_n        = 0;
	uint16_t dsa_vr_k        = 0;

	double apu_k1            = 36;
	uint32_t apu_k2          = 1;


	ZoneCalcStage zona_stage = ZoneCalcStage::BeforeLou;
	uint16_t zona_ch_num     = 0;
	uint32_t zona_val_sq     = 1;
};