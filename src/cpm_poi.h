#pragma once

#include "axi_dsp.h"

#include <cstdint>

// TODO: нужно ли axi_dsp_apply();

class CpmPoi {
public:
	struct Dsa {
		bool is_on;
		uint16_t vr_n;
		uint16_t vr_k;
	};

	enum class ZoneCalcStage : uint16_t {
		BeforeLou = 0,
		AfterLou  = 1,
	};

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
		axi_dsp_set_detector_level(apu_k1, 0);
		axi_dsp_set_detector_level(apu_k2, 1);
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

	// TODO: где считать? Нужен ли Set?
	uint16_t get_nes() const { return 0; }
	uint16_t set_nes() const { return 0; }


	// KAN — маска подключённых каналов
	uint8_t get_kan() const {
		if (dma_enabled) return kan;
		return kan;
		// return axi_dsp_get_channel_mask();
	}

	void set_kan(uint8_t value) {
		kan = value;
		if (!dma_enabled) return;
		axi_dsp_set_channel_mask(uint32_t(value));
	}

	// Штатные настройки ПОИ
	void set_shtat() {
		sdc         = true;
		dsa_is_on   = false;

		kan         = 0xFF;

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
		axi_dsp_set_detector_level(apu_k1, 0);
		axi_dsp_set_detector_level(apu_k2, 1);
		axi_dsp_set_channel_mask(kan);
	}

	void set_dma_enabled(bool value) { dma_enabled = value; }

private:
	bool dma_enabled         = false;

	bool sdc                 = false;
	bool dsa_is_on           = false;

	uint8_t kan              = 0;

	uint16_t dsa_vr_n        = 0;
	uint16_t dsa_vr_k        = 0;

	double apu_k1            = 36;
	double apu_k2            = 0;


	ZoneCalcStage zona_stage = ZoneCalcStage::BeforeLou;
	uint16_t zona_ch_num     = 0;
	double zona_val_sq       = 1.0;
};