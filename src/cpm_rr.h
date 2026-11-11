#pragma once

#include "imitator_u220.h"
#include "techlaser.h"
#include "u220_group.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>


class CpmRr {
public:
	// Полученная команда управления вращением (KUVR)
	enum class RotationCommand : uint8_t {
		Off       = 0, // Выключить вращение и имитатор вращения
		On        = 1, // Включить вращение антенны
		Simulator = 2  // Включить имитатор вращения
	};

	// Фактическое состояние вращения (VR)
	enum class RotationState : uint8_t {
		Off       = 0, // Вращение и имитатор выключены
		On        = 1, // Вращение включено
		Simulator = 2, // Имитатор вращения включён
		Unknown   = 3  // Состояние не определено
	};

	explicit CpmRr(Techlaser & techlaser, ImitatorU220 & imitator_u220, U220Group & u220_group)
		: techlaser(techlaser)
		, imitator_u220(imitator_u220)
		, u220_group(u220_group) {
		set_current_time(0.0);
	}
	~CpmRr() {}
	CpmRr(const CpmRr &)             = delete;
	CpmRr & operator=(const CpmRr &) = delete;


	// KUVR — последняя полученная команда управления вращением
	// VR — фактическое состояние вращения антенны
	RotationState get_vr() const {
		if (use_imit_opu) return imitator_u220.is_opu_running() ? RotationState::Simulator : RotationState::Off;
		return RotationState::Unknown;
	}

	RotationCommand get_kuvr() const { return kuvr; }
	void set_vr(RotationCommand value) {
		kuvr = value;
		switch (value) {
		case RotationCommand::Off: {
			imitator_u220.stop_opu();
			imitator_u220.apply(0);
		} break;
		case RotationCommand::On: {
			use_imit_opu = false;
			sync_opu();
		} break;
		case RotationCommand::Simulator: {
			use_imit_opu = true;
			imitator_u220.restart_opu();
			imitator_u220.apply(0);
		} break;
		default: break;
		}
	}

	// KUIZL — последняя полученная команда управления излучением
	// IZL — фактическое состояние излучения
	// false — выключить, true — включить
	bool get_izl() const { return u220_group.get_play_gpio_tx_enable(); }
	bool get_kuizl() const { return kuizl; }
	void set_izl(bool value) {
		kuizl = value;
		u220_group.set_play_gpio_tx_enable(value);
	}

	// ANT — состояние антенны
	// false — отключена, true — подключена
	bool get_ant() const { return ant; }
	void set_ant(bool val) { ant = val; }

	// DAZ — азимутальная поправка к датчику, градусы
	// В протоколе: int16_t, ЦМР = 0.0054931640625°
	float get_daz() const { return daz; }
	void set_daz(float value) { daz = value; }

	// DD_POI — поправка по дальности ПОИ, метры
	// В протоколе: int16_t, ЦМР = 1 м
	float get_dd_poi() const { return dd_poi; }
	void set_dd_poi(float value) { dd_poi = value; }

	// DAZ_POI — азимутальная поправка ПОИ, градусы
	// В протоколе: int16_t, ЦМР = 0.0054931640625°
	float get_daz_poi() const { return daz_poi; }
	void set_daz_poi(float value) { daz_poi = value; }


	void set_current_time(double value) { time_offset = value - PISystemTime::current(true).toSeconds(); }
	double get_current_time() const { return PISystemTime::current(true).toSeconds() + time_offset; }
	double get_compensated_time() const { return get_current_time() - signal_processing_delay; }
	void set_signal_processing_delay(double value) { signal_processing_delay = value; }

	float get_current_az() const {
		auto az = use_imit_opu ? imitator_u220.get_opu_az() : techlaser.getAngleCurrent();
		if (last_opu_az > 270.0 && az < 90.0) {
			rotation_completed = true;
		}
		last_opu_az = az;

		az += daz;
		az = std::fmod(az, 360.0);
		if (az < 0.0) az += 360.0;
		return az;
	}

	float get_compensated_az() const {
		const auto delay       = std::chrono::duration<double>(signal_processing_delay.load());

		const auto sample_time = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(-delay);

		return get_compensated_az(sample_time);
	}

	float get_compensated_az(std::chrono::steady_clock::time_point sample_time) const {
		const double ago_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - sample_time).count();

		auto az              = use_imit_opu ? imitator_u220.get_opu_azago(ago_sec) : techlaser.getAngleAgo(sample_time);

		az += daz + daz_poi;
		az = std::fmod(az, 360.0);
		if (az < 0.0) az += 360.0;

		return az;
	}

	bool take_rotation_completed() const {
		const bool result  = rotation_completed;
		rotation_completed = false;
		return result;
	}

	void sync_opu() {
		const auto s   = techlaser.getState();
		const auto now = std::chrono::steady_clock::now();

		if (s.angle_receive_timestamp == std::chrono::steady_clock::time_point{}) {
			return;
		}

		if (now - s.angle_receive_timestamp > std::chrono::milliseconds(200)) {
			return;
		}

		const double angle = techlaser.getAngleAgo(now);
		imitator_u220.set_opu(angle, s.current_speed);
	}

private:
	Techlaser & techlaser;
	ImitatorU220 & imitator_u220;
	U220Group & u220_group;

	RotationCommand kuvr            = RotationCommand::Off; // Последняя команда вращения
	RotationState vr                = RotationState::Off;   // Текущее состояние вращения

	bool kuizl                      = false; // Последняя команда излучения
	bool ant                        = false; // Состояние подключения антенны

	bool use_imit_opu               = false;

	mutable double last_opu_az      = 0;
	mutable bool rotation_completed = false;

	float daz                       = 0.0f; // Азимутальная поправка датчика, градусы
	float dd_poi                    = 0.0f; // Поправка дальности ПОИ, метры
	float daz_poi                   = 0.0f; // Азимутальная поправка ПОИ, градусы

	std::atomic<double> time_offset{0.0};
	std::atomic<double> signal_processing_delay{
		0.0239424}; // (paramRLSmini.N_ddr/2+paramRLSmini.M_az/2*paramRLSmini.frqScl)*paramRLSmini.pulsePeriod
};
