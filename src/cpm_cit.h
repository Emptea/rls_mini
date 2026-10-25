#pragma once

#include <array>
#include <cstdint>
#include <mutex>

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
};
