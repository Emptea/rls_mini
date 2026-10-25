#pragma once

#include "techlaser.h"

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

	explicit CpmRr(Techlaser & techlaser): techlaser(techlaser) { set_current_time(0.0); }
	~CpmRr() {}
	CpmRr(const CpmRr &)             = delete;
	CpmRr & operator=(const CpmRr &) = delete;


	// KUVR — последняя полученная команда управления вращением
	// VR — фактическое состояние вращения антенны
	RotationState get_vr() const {
		const auto state = techlaser.getState();

		if (state.motor_error || state.motor_status == Techlaser::MotorStatus::Invalid) return RotationState::Unknown;

		switch (state.motor_status) {
		case Techlaser::MotorStatus::Starting:
		case Techlaser::MotorStatus::Rotating:
		case Techlaser::MotorStatus::Braking:
		case Techlaser::MotorStatus::Stopping: return RotationState::On;
		case Techlaser::MotorStatus::Idle:
		case Techlaser::MotorStatus::Hold: return RotationState::Off;

		default: return RotationState::Unknown;
		}
	}
	RotationCommand get_kuvr() const { return kuvr; }
	void set_vr(RotationCommand value) {
		kuvr = value;
		switch (value) {
		case RotationCommand::Off: techlaser.stop(); break;
		case RotationCommand::On: techlaser.start(180.f); break;
		case RotationCommand::Simulator: break;
		default: break;
		}
	}

	// KUIZL — последняя полученная команда управления излучением
	// IZL — фактическое состояние излучения
	// false — выключить, true — включить
	bool get_izl() const { return izl; }
	bool get_kuizl() const { return kuizl; }
	void set_izl(bool value) {
		kuizl = value;
		izl   = value;
	}

	// ANT — состояние антенны
	// false — отключена, true — подключена
	bool get_ant() const { return ant; }
	void set_ant(bool val) { ant = val; }

	// ISPR_KAN — исправность приёмных каналов
	// Целое без знака, 0–255
	uint8_t get_ispr_kan() const { return ispr_kan; }
	void set_ispr_kan(uint8_t value) { ispr_kan = value; }

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

	float get_current_az() const { return techlaser.getAngleCurrent() + daz; }
	float get_compensated_az() const { return techlaser.getAngleAgo(PISystemTime::fromSeconds(signal_processing_delay)) + daz; }

private:
	Techlaser & techlaser;

	RotationCommand kuvr = RotationCommand::Off; // Последняя команда вращения
	RotationState vr     = RotationState::Off;   // Текущее состояние вращения

	bool kuizl           = false; // Последняя команда излучения
	bool izl             = false; // Текущее состояние излучения
	bool ant             = false; // Состояние подключения антенны

	uint8_t ispr_kan     = 0; // Исправность приёмных каналов

	float daz            = 0.0f; // Азимутальная поправка датчика, градусы
	float dd_poi         = 0.0f; // Поправка дальности ПОИ, метры
	float daz_poi        = 0.0f; // Азимутальная поправка ПОИ, градусы

	std::atomic<double> time_offset{0.0};
	std::atomic<double> signal_processing_delay{0.01};
};
