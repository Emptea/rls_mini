#pragma once

#include "imitator_u220.h"
#include "mini_config.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

class CpmCit {
public:
	enum class Register : uint8_t {
		POA = 0,
		PBA = 1
	};

	struct Target {
		Register reg   = Register::POA; // Регистр: POA или PBA
		bool on        = false;         // Цель включена/выключена
		float az       = 0.0f;          // Азимут, градусы
		float distance = 0.0f;          // Дальность, м
		float um       = 0.0f;          // Угол места, градусы
		float vr       = 0.0f;          // Радиальная скорость, м/с
		float osl      = 0.0f;          // Ослабление, дБ
	};

	struct Detection {
		size_t index;
		Target target;
	};

	std::vector<Detection> update_azimuth(float current_az) {
		std::lock_guard<std::mutex> lock(mutex);

		std::vector<Detection> result;

		if (!az_initialized) {
			previous_az    = current_az;
			az_initialized = true;
			return result;
		}

		const auto normalize = [](float az) {
			az = std::fmod(az, 360.0f);
			return az < 0.0f ? az + 360.0f : az;
		};

		const float previous = normalize(previous_az);
		const float current  = normalize(current_az);
		const float delta    = normalize(current - previous);

		for (size_t i = 0; i < targets.size(); ++i) {
			const auto & target = targets[i];

			if (!target.on) {
				continue;
			}

			const float target_delta = normalize(target.az - previous);

			if (target_delta > 0.0f && target_delta <= delta) {
				result.push_back({i, target});
			}
		}

		previous_az = current_az;
		return result;
	}

	using Targets = std::array<Target, 4>;

	explicit CpmCit(ImitatorU220 & imitator): imitator(imitator) {}

	void set_targets(const Targets & value, size_t count = 4) {
		std::lock_guard<std::mutex> lock(mutex);
		std::array<ImitatorU220::TargetParams, 4> params{};

		uint8_t enable_mask = 0;

		for (size_t i = 0; i < count; ++i) {
			const auto & t = value[i];
			params[i]      = {t.az, t.um, t.distance, static_cast<int16_t>(std::lround(t.vr)), t.osl};

			if (t.on) enable_mask |= static_cast<uint8_t>(1U << i);
		}

		imitator.set_params(params);
		imitator.set_targets_enabled(enable_mask);
		imitator.apply(0.0);
	}

	Targets get_targets() const {
		std::lock_guard<std::mutex> lock(mutex);
		return targets;
	}

	void set_shtat(const MiniConfig & cfg) {
		Targets defaults{};

		for (size_t i = 0; i < defaults.size(); ++i) {
			defaults[i].reg      = Register::POA;
			defaults[i].on       = cfg.imitator_target_en[i];
			defaults[i].az       = static_cast<float>(cfg.target_az[i]);
			defaults[i].distance = static_cast<float>(cfg.target_dist[i]);
			defaults[i].um       = static_cast<float>(cfg.target_um[i]);
			defaults[i].vr       = static_cast<float>(cfg.target_vel[i]);
			defaults[i].osl      = static_cast<float>(cfg.target_osl[i]);
		}

		set_targets(defaults);
	}


private:
	ImitatorU220 & imitator;
	mutable std::mutex mutex;
	Targets targets{};

	std::array<bool, 2> sent_in_rotation{};
	float previous_az   = 0.0f;
	bool az_initialized = false;
};
