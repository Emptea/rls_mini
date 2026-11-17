#pragma once

#include "rls.hpp"

#include <array>
#include <cstddef>

namespace rls {

// Общие физические константы
inline constexpr double LIGHT_SPEED             = 299792458.0;
inline constexpr double CARRIER_FREQUENCY       = 9.23e9;
inline constexpr double SAMPLE_RATE             = 5e6;

// Параметры обработки
inline constexpr double PULSE_PERIOD            = 46.4e-6;
inline constexpr size_t FFT_SIZE                = 512;
inline constexpr size_t FFT_HALF_SIZE           = FFT_SIZE / 2;

// Параметры сигнала
inline constexpr size_t TX_LEN_NEAR             = 40;
inline constexpr size_t TX_LEN_FAR              = 6;
inline constexpr size_t RX_LEN_NEAR             = 101;
inline constexpr size_t RX_LEN_FAR              = 40;

// Углы места для диаграмм
inline constexpr std::array<float, 8> UM_VALUES = {
	5.3125f,
	15.9375f,
	26.5625f,
	37.1875f,
	47.8125f,
	58.4375f,
	69.0625f,
	79.6875f,
};

// Производные константы
inline constexpr double WAVELENGTH    = LIGHT_SPEED / CARRIER_FREQUENCY;
inline constexpr double VELOCITY_STEP = WAVELENGTH / PULSE_PERIOD / FFT_SIZE / 2.0;

inline constexpr double range_index_to_meters(size_t index) {
	const bool near        = index < RX_LEN_NEAR;
	const size_t range_idx = near ? index : index - RX_LEN_NEAR;
	const size_t tx_len    = near ? TX_LEN_NEAR : TX_LEN_FAR;
	return (range_idx + tx_len / 2.0) * LIGHT_SPEED / (2.0 * SAMPLE_RATE);
}

inline constexpr double freq_index_to_velocity(size_t index) {
	const int doppler_bin = index < rls::FFT_HALF_SIZE ? -static_cast<int>(index) : rls::FFT_SIZE - static_cast<int>(index);
	return doppler_bin * rls::VELOCITY_STEP;
}


} // namespace rls