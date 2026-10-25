#pragma once

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

	void set_targets(const Targets & value) {
		std::lock_guard<std::mutex> lock(mutex);
		targets = value;
	}

	Targets get_targets() const {
		std::lock_guard<std::mutex> lock(mutex);
		return targets;
	}

private:
	mutable std::mutex mutex;
	Targets targets{};

	std::array<bool, 2> sent_in_rotation{};
	float previous_az   = 0.0f;
	bool az_initialized = false;
};
