#pragma once

#include <chrono>
#include <piprotectedvariable.h>
#include <piserial.h>

class Techlaser: public PIObject {
	PIOBJECT(Techlaser)

public:
	Techlaser();
	~Techlaser();

	struct State {
		float current_angle = 0.f; // 0 - 360
		float current_speed = 0.f; // °/c
		std::chrono::steady_clock::time_point angle_receive_timestamp{};
	};

	bool open(const PIString & serial);
	void close();
	bool isOpened() const { return ser.isOpened(); }
	bool isClosed() const { return ser.isClosed(); }

	State getState() const;
	double getAngleAgo(std::chrono::steady_clock::time_point target_time) const;
	double getAngleCurrent() const { return getState().current_angle; }

private:
	struct AngleReceiver {
		static constexpr float LSB = 360.0f / 8192.0f;

		uint8_t high               = 0;
		bool has_high              = false;

		bool push(uint8_t byte, float & angle) {
			if ((byte & 0xC0) == 0x80) {
				high     = byte;
				has_high = true;
				return false;
			}
			if (!has_high || (byte & 0x80)) return false;
			const uint16_t raw_angle = (static_cast<uint16_t>(high & 0x3F) << 7) | (byte & 0x7F);
			angle                    = raw_angle * LSB;
			has_high                 = false;
			return true;
		}

		void reset() { has_high = false; }
	};

	void processAngleBytes(const uchar * data, ssize_t size);

	AngleReceiver angle_receiver;
	PISerial ser;
	mutable PIProtectedVariable<State> state;
	float speed_ref_angle = 0.0f;
	std::chrono::steady_clock::time_point speed_ref_time{};
	bool have_speed_ref = false;
};
