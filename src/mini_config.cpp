#include "mini_config.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

MiniConfig::MiniConfig() {
	load("mini.ini");
	load_settings();
	load_imitator();
	load_coeffs("tx_coeffs.csv");
	load_signal("tx_gainon.txt", tx_gainon_sg);
	load_signal("rx_en.txt", rx_en_sg);
	load_complex_signal("tx_dac.txt", tx_dac_sg);
}

void MiniConfig::trim(std::string & s) {
	const auto first = s.find_first_not_of(" \t\r\n");

	if (first == std::string::npos) {
		s.clear();
		return;
	}

	const auto last = s.find_last_not_of(" \t\r\n");
	s               = s.substr(first, last - first + 1);
}

void MiniConfig::load(const std::string & filename) {
	std::ifstream file(filename);

	if (!file) {
		throw std::runtime_error("Cannot open config: " + filename);
	}

	std::string section;
	std::string line;

	while (std::getline(file, line)) {
		const auto comment = line.find_first_of("#;");

		if (comment != std::string::npos) {
			line.erase(comment);
		}

		trim(line);

		if (line.empty()) {
			continue;
		}

		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			trim(section);
			continue;
		}

		const auto pos = line.find('=');

		if (pos == std::string::npos) {
			throw std::runtime_error("Invalid config line: " + line);
		}

		std::string key   = line.substr(0, pos);
		std::string value = line.substr(pos + 1);

		trim(key);
		trim(value);

		if (section.empty() || key.empty() || value.empty()) {
			throw std::runtime_error("Invalid config line: " + line);
		}

		data_[section][key] = value;
	}
}

uint32_t MiniConfig::get_uint(const std::string & section, const std::string & key) const {
	const auto section_it = data_.find(section);

	if (section_it == data_.end()) {
		throw std::runtime_error("Missing config section: [" + section + "]");
	}

	const auto key_it = section_it->second.find(key);

	if (key_it == section_it->second.end()) {
		throw std::runtime_error("Missing config key: " + section + "." + key);
	}

	const std::string & value = key_it->second;

	if (value.empty() || value.front() == '-') {
		throw std::runtime_error("Invalid config value: " + section + "." + key);
	}

	size_t pos        = 0;
	const auto result = std::stoull(value, &pos, 0);

	if (pos != value.size() || result > std::numeric_limits<uint32_t>::max()) {
		throw std::runtime_error("Invalid config value: " + section + "." + key);
	}

	return static_cast<uint32_t>(result);
}

int32_t MiniConfig::get_int(const std::string & section, const std::string & key) const {
	const auto section_it = data_.find(section);

	if (section_it == data_.end()) {
		throw std::runtime_error("Missing config section: [" + section + "]");
	}

	const auto key_it = section_it->second.find(key);

	if (key_it == section_it->second.end()) {
		throw std::runtime_error("Missing config key: " + section + "." + key);
	}

	const auto & value = key_it->second;

	if (value.empty()) {
		throw std::runtime_error("Invalid config value: " + section + "." + key);
	}

	size_t pos     = 0;
	int64_t result = 0;

	try {
		result = std::stoll(value, &pos, 0);
	} catch (const std::exception &) {
		throw std::runtime_error("Invalid config value: " + section + "." + key);
	}

	if (pos != value.size() || result < std::numeric_limits<int32_t>::min() || result > std::numeric_limits<int32_t>::max()) {
		throw std::runtime_error("Invalid config value: " + section + "." + key);
	}

	return static_cast<int32_t>(result);
}

std::array<uint32_t, MiniConfig::CHANNEL_COUNT> MiniConfig::get_uint_array(const std::string & section, const std::string & key) const {
	std::array<uint32_t, CHANNEL_COUNT> result{};

	const auto section_it = data_.find(section);

	if (section_it == data_.end()) {
		throw std::runtime_error("Missing config section: [" + section + "]");
	}

	const auto key_it = section_it->second.find(key);

	if (key_it == section_it->second.end()) {
		throw std::runtime_error("Missing config key: " + section + "." + key);
	}

	std::stringstream ss(key_it->second);
	std::string value;

	for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
		if (!std::getline(ss, value, ',')) {
			throw std::runtime_error("Not enough values: " + section + "." + key);
		}

		trim(value);

		if (value.empty() || value.front() == '-') {
			throw std::runtime_error("Invalid value: " + section + "." + key);
		}

		size_t pos      = 0;
		uint64_t number = 0;

		try {
			number = std::stoull(value, &pos, 0);
		} catch (const std::exception &) {
			throw std::runtime_error("Invalid value: " + section + "." + key);
		}

		if (pos != value.size() || number > std::numeric_limits<uint32_t>::max()) {
			throw std::runtime_error("Invalid value: " + section + "." + key);
		}

		result[CHANNEL_COUNT - 1 - i] = static_cast<uint32_t>(number);
	}

	if (std::getline(ss, value, ',')) {
		throw std::runtime_error("Too many values: " + section + "." + key);
	}

	return result;
}

void MiniConfig::load_settings() {
	tx_dac_delay      = get_uint_array("tx", "dac_delay");
	tx_gpio_delay     = get_uint_array("tx", "gpio_delay");
	rx_adc_delay      = get_uint_array("rx", "adc_delay");
	rx_en_delay       = get_uint_array("rx", "en_delay");
	tx_gpio_enable    = get_uint("tx", "gpio_enable");
	rx_gpio_enable    = get_uint("rx", "gpio_enable");
	rx_insert_count   = get_uint("rx", "insert_count");
	pps_dead_zone_ms  = get_uint("sync", "pps_dead_zone_ms");
	dma_enabled       = get_uint("dma", "enable") == 1;
	const auto values = get_uint_array("tx", "use_ext_file_sg");
	for (size_t ch = 0; ch < CHANNEL_COUNT; ++ch) {
		if (values[ch] > 1) throw std::runtime_error("Invalid tx.use_ext_file_sg");
		use_ext_file_sg[ch] = values[ch] == 1;
	}
}

void MiniConfig::load_signal(const std::string & filename, ChannelSignals & signals) {
	std::ifstream file(filename);

	if (!file) {
		throw std::runtime_error("Cannot open signal file: " + filename);
	}

	std::string line;
	size_t sample = 0;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (sample >= SAMPLE_COUNT || line.size() != CHANNEL_COUNT) {
			throw std::runtime_error("Invalid signal file size: " + filename);
		}

		for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
			const char value = line[CHANNEL_COUNT - 1 - channel];

			if (value != '0' && value != '1') {
				throw std::runtime_error("Invalid signal value: " + filename);
			}

			signals[channel][sample] = static_cast<uint8_t>(value - '0');
		}

		++sample;
	}

	if (sample != SAMPLE_COUNT) {
		throw std::runtime_error("Invalid signal sample count: " + filename);
	}
}

void MiniConfig::load_complex_signal(const std::string & filename, ChannelComplexSignals & signals) {
	std::ifstream file(filename);

	if (!file) {
		throw std::runtime_error("Cannot open signal file: " + filename);
	}

	std::string line;
	size_t sample = 0;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (sample >= SAMPLE_COUNT || line.size() != CHANNEL_COUNT * 8) {
			throw std::runtime_error("Invalid signal file size: " + filename);
		}

		for (size_t ch = 0; ch < CHANNEL_COUNT; ++ch) {
			const size_t offset = (CHANNEL_COUNT - 1 - ch) * 8;

			const auto im       = std::stoul(line.substr(offset, 4), nullptr, 16);
			const auto re       = std::stoul(line.substr(offset + 4, 4), nullptr, 16);

			if (im > 0xFFFF || re > 0xFFFF) {
				throw std::runtime_error("Invalid signal value: " + filename);
			}

			signals[ch][sample] = {static_cast<int16_t>(static_cast<uint16_t>(re)), static_cast<int16_t>(static_cast<uint16_t>(im))};
		}

		++sample;
	}

	if (sample != SAMPLE_COUNT) {
		throw std::runtime_error("Invalid signal sample count: " + filename);
	}
}

void MiniConfig::load_coeffs(const std::string & filename) {
	std::ifstream file(filename);
	if (!file) throw std::runtime_error("Cannot open coefficients file: " + filename);

	std::string line;
	if (!std::getline(file, line)) throw std::runtime_error("Missing coefficients header: " + filename);
	trim(line);
	const auto header_separator = line.find(';');
	if (header_separator == std::string::npos) throw std::runtime_error("Invalid coefficients header: " + filename);
	std::string amplitude_header = line.substr(0, header_separator);
	std::string phase_header     = line.substr(header_separator + 1);
	trim(amplitude_header);
	trim(phase_header);
	if (amplitude_header != "Amplitude" || phase_header != "Phase") {
		throw std::runtime_error("Invalid coefficients header: " + filename);
	}

	std::array<double, CHANNEL_COUNT> loaded_amplitudes{};
	std::array<double, CHANNEL_COUNT> loaded_phases{};
	std::vector<std::complex<int16_t>> loaded_complex_gains;
	loaded_complex_gains.reserve(CHANNEL_COUNT);
	std::vector<std::complex<int32_t>> loaded_corrections;
	loaded_corrections.reserve(CHANNEL_COUNT);
	size_t channel     = 0;
	size_t line_number = 1;
	while (std::getline(file, line)) {
		++line_number;
		trim(line);
		if (line.empty()) continue;
		if (channel >= CHANNEL_COUNT) throw std::runtime_error("Too many coefficient rows: " + filename);
		const auto separator   = line.find(';');
		const auto invalid_row = "Invalid coefficients at " + filename + ":" + std::to_string(line_number);
		if (separator == std::string::npos || line.find(';', separator + 1) != std::string::npos) {
			throw std::runtime_error(invalid_row);
		}
		auto parse = [&](std::string value) {
			trim(value);
			size_t pos = 0;
			double result;
			try {
				result = std::stod(value, &pos);
			} catch (const std::exception &) {
				throw std::runtime_error(invalid_row);
			}
			if (pos != value.size() || !std::isfinite(result)) throw std::runtime_error(invalid_row);
			return result;
		};
		loaded_amplitudes[channel] = parse(line.substr(0, separator));
		loaded_phases[channel]     = parse(line.substr(separator + 1));
		const double phase_rad     = loaded_phases[channel] * std::acos(-1.0) / 180.0;
		const std::complex<double> gain =
			std::complex(loaded_amplitudes[channel] * std::cos(phase_rad), loaded_amplitudes[channel] * std::sin(phase_rad));
		const std::complex<double> correction = 1.0 / gain;
		const double real_gain                = std::round((1 << 13) * gain.real());
		const double imag_gain                = std::round((1 << 13) * gain.imag());
		const auto in_range                   = [](double value) {
            return std::isfinite(value) && value >= std::numeric_limits<int16_t>::min() && value <= std::numeric_limits<int16_t>::max();
		};
		if (!in_range(real_gain) || !in_range(imag_gain)) throw std::runtime_error("Coefficient gain out of int16_t range: " + invalid_row);
		loaded_complex_gains.emplace_back(static_cast<int16_t>(real_gain), static_cast<int16_t>(imag_gain));
		loaded_corrections.emplace_back(static_cast<int32_t>(std::round(1 << 14) * correction.real()),
		                                static_cast<int32_t>(std::round(1 << 14) * correction.imag()));
		++channel;
	}
	if (file.bad()) throw std::runtime_error("Cannot read coefficients file: " + filename);
	if (channel != CHANNEL_COUNT) throw std::runtime_error("Not enough coefficient rows: " + filename);
	amplitudes = loaded_amplitudes;
	phases     = loaded_phases;
	complex_gains.swap(loaded_complex_gains);
	complex_corrections.swap(loaded_corrections);
}


void MiniConfig::load_imitator() {
	const auto section_it = data_.find("imitator");
	if (section_it == data_.end()) {
		throw std::runtime_error("mini.ini: missing [imitator] section; update the runtime config from config/mini.ini");
	}
	const auto & section = section_it->second;
	const auto get_value = [&](const std::string & key) -> const std::string & {
		const auto it = section.find(key);
		if (it == section.end()) {
			throw std::runtime_error("mini.ini: missing config key: imitator." + key);
		}
		return it->second;
	};
	const auto parse     = [](const std::string & text) {
        size_t end         = 0;
        const double value = std::stod(text, &end);
        if (text.find_first_not_of(" \t\r\n", end) != std::string::npos || !std::isfinite(value)) {
            throw std::runtime_error("Invalid imitator value: " + text);
        }
        return value;
	};
	const auto load_targets = [&](const std::string & key, std::array<double, TARGET_COUNT> & values) {
		std::istringstream stream(get_value(key));
		std::string item;
		for (auto & value: values) {
			if (!std::getline(stream, item, ',')) throw std::runtime_error("Not enough values: imitator." + key);
			value = parse(item);
		}
		if (std::getline(stream, item, ',') || (!get_value(key).empty() && get_value(key).back() == ',')) {
			throw std::runtime_error("Too many values: imitator." + key);
		}
	};
	load_targets("target_az", target_az);
	load_targets("target_um", target_um);
	load_targets("target_dist", target_dist);
	std::array<double, TARGET_COUNT> velocities{};
	load_targets("target_vel", velocities);
	for (size_t target = 0; target < TARGET_COUNT; ++target) {
		const double velocity = velocities[target];
		if (std::trunc(velocity) != velocity || velocity < std::numeric_limits<int16_t>::min() ||
		    velocity > std::numeric_limits<int16_t>::max()) {
			throw std::runtime_error("Invalid imitator.target_vel: expected int16_t velocity in m/s");
		}
		target_vel[target] = static_cast<int16_t>(velocity);
	}
	load_targets("target_osl", target_osl);
	opu_az = parse(get_value("opu_az"));
}
