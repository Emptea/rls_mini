#pragma once

#include "axi_dsp.h"

#include <cstdint>

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
	double get_apu_k1() const {
		if (!dma_enabled) return apu_k1;
		return apu_k1;
		// return axi_dsp_get_detector_level(0);
	}
	double get_apu_k2() const {
		if (!dma_enabled) return apu_k2;
		return apu_k2;
		// return axi_dsp_get_detector_level(1);
	}

	void set_apu(uint32_t k1, uint32_t k2) {
		apu_k1 = k1;
		apu_k2 = k2;
		if (!dma_enabled) return;
		axi_dsp_set_cfar_level(apu_k1);
		axi_dsp_set_median_detector_coef(apu_k2);
	}

	// ZONA — параметры расчёта зоны


	void set_zona_params(ZoneCalcStage stage, uint16_t ch_num) {
		zona_stage  = stage;
		zona_ch_num = ch_num;
	}

	ZoneCalcStage get_zona_stage() const { return zona_stage; }
	uint16_t get_zona_ch_num() const { return zona_ch_num; }

	void set_zona_val(const uint32_t (&data)[2][8]) { zona_val_sq = data[static_cast<size_t>(zona_stage)][zona_ch_num]; }
	double get_zona_val() const { return zona_val_sq; }

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
		apu_k2      = 0;

		zona_stage  = ZoneCalcStage::BeforeLou;
		zona_ch_num = 0;

		if (!dma_enabled) return;

		axi_dsp_set_compensation_mode(0);
		axi_dsp_set_compensation_ref(uint32_t(1575));
		axi_dsp_set_motion_selector(1, sdc);
		axi_dsp_set_cfar_level(apu_k1);
		axi_dsp_set_median_detector_coef(apu_k2);
		axi_dsp_apply();
	}

	void set_dma_enabled(bool value) { dma_enabled = value; }

private:
	bool dma_enabled         = false;

	bool sdc                 = false;
	bool dsa_is_on           = false;

	uint8_t ispr_kan         = 0; // Исправность приёмных каналов
	uint8_t kan              = 0;

	uint16_t dsa_vr_n        = 0;
	uint16_t dsa_vr_k        = 0;

	double apu_k1            = 36;
	double apu_k2            = 0;


	ZoneCalcStage zona_stage = ZoneCalcStage::BeforeLou;
	uint16_t zona_ch_num     = 0;
	double zona_val_sq       = 1.0;
};