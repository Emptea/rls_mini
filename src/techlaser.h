#ifndef techlaser_h
#define techlaser_h

#include <pipacketextractor.h>
#include <piprotectedvariable.h>
#include <piserial.h>

class Techlaser: public PIObject {
	PIOBJECT(Techlaser)

public:
	Techlaser();
	~Techlaser();

	enum class DeviceStatus {
		Invalid  = -1,
		NotReady = 0, // Не готов (устройство переходит в него после возникновения ошибок, которые можно получить по команде $b#)
		SelfDiag = 1, // Идет процесс самодиагностики
		Ready    = 2, // Готов (может принимать команды позиционирования, в остальных состояниях они отбрасываются)
		Heating  = 3, // Обогрев
	};

	enum class MotorStatus {
		Invalid  = -1,
		Idle     = 0, // Бездействие (остановка без удержания)
		Hold     = 1, // Удержание позиции
		Stopping = 2, // Остановка (переходный статус)
		Starting = 3, // Разгон
		Braking  = 4, // Торможение
		Rotating = 5, // Равномерное движение
	};

	struct State {
		DeviceStatus device_status = DeviceStatus::Invalid;
		MotorStatus motor_status   = MotorStatus::Invalid;
		bool motor_error           = false;
		float current_angle        = 0.f; // 0 - 360
		float current_speed        = 0.f; // °/c
		PISystemTime angle_receive_timestamp;
	};

	// open serial, starts receive
	bool open(const PIString & serial);

	// stop requests, receive and close serial
	void close();

	bool isOpened() const { return ser.isOpened(); }
	bool isClosed() const { return ser.isClosed(); }

	// start rotate and requests
	void start(float rotate_speed_deg_s);

	// stop rotate
	void stop();

	// current state
	State getState() const;
	double getAngleAgo(PISystemTime ago) const;

private:
	void packetReceived(PIByteArray msg);

	PIThread req_thread;
	PISerial ser;
	PIPacketExtractor pext;
	PITimeMeasurer angle_receive_tm;
	mutable PIProtectedVariable<State> state;
};

#endif // techlaser_h
