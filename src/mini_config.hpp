#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class MiniConfig {
public:
	static constexpr size_t SAMPLE_COUNT  = 232 * 4;
	static constexpr size_t CHANNEL_COUNT = 8;
	static constexpr size_t TARGET_COUNT  = 4;

	explicit MiniConfig();
	void load_coeffs(const std::string & filename);

	std::array<double, CHANNEL_COUNT> amplitudes{};
	std::array<double, CHANNEL_COUNT> phases{};
	std::vector<std::complex<int16_t>> complex_gains;
	std::vector<std::complex<int32_t>> complex_corrections;

	std::array<uint32_t, CHANNEL_COUNT> tx_dac_delay{};
	std::array<uint32_t, CHANNEL_COUNT> tx_gpio_delay{};
	std::array<uint32_t, CHANNEL_COUNT> rx_adc_delay{};
	std::array<uint32_t, CHANNEL_COUNT> rx_en_delay{};
	std::array<bool, CHANNEL_COUNT> use_ext_file_sg{};
	uint32_t tx_gpio_enable   = 0;
	uint32_t rx_gpio_enable   = 0;
	uint32_t rx_insert_count  = 0;

	uint32_t pps_dead_zone_ms = 0;

	bool use_ext_lo           = true;
	bool dma_enabled          = false;

	std::array<double, TARGET_COUNT> target_az{};
	std::array<double, TARGET_COUNT> target_um{};
	std::array<double, TARGET_COUNT> target_dist{};
	std::array<int16_t, TARGET_COUNT> target_vel{};
	std::array<double, TARGET_COUNT> target_osl{};
	double opu_az = 0.0;
	std::array<bool, TARGET_COUNT> imitator_target_en{};
	std::array<bool, CHANNEL_COUNT> imitator_channels_en{};

	using Signal                = std::array<uint8_t, SAMPLE_COUNT>;
	using ComplexSignal         = std::array<std::complex<int16_t>, SAMPLE_COUNT>;
	using ChannelSignals        = std::array<Signal, CHANNEL_COUNT>;
	using ChannelComplexSignals = std::array<ComplexSignal, CHANNEL_COUNT>;
	ChannelSignals tx_gainon_sg{};
	ChannelSignals rx_en_sg{};
	ChannelComplexSignals tx_dac_sg{};

private:
	using Section    = std::map<std::string, std::string>;
	using ConfigData = std::map<std::string, Section>;

	ConfigData data_;

	void load(const std::string & filename);
	void load_settings();
	void load_signal(const std::string & filename, ChannelSignals & signals);
	void load_complex_signal(const std::string & filename, ChannelComplexSignals & signals);
	void load_imitator();

	uint32_t get_uint(const std::string & section, const std::string & key) const;
	int32_t get_int(const std::string & section, const std::string & key) const;
	std::array<uint32_t, CHANNEL_COUNT> get_uint_array(const std::string & section, const std::string & key) const;

	static void trim(std::string & s);
};
