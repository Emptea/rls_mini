#include "techlaser.h"

#include <piliterals_string.h>
#include <piliterals_time.h>


Techlaser::Techlaser() {
	pext.setSplitMode(PIPacketExtractor::HeaderAndFooter);
	pext.setHeader({'$'});
	pext.setFooter({'#'});

	// serial read send to packet extractor
	CONNECTL(&ser, threadedReadEvent, ([this](const uchar * readed, ssize_t size) { pext.appendData(readed, size); }));

	// when packet extractor parse message, send it to packetReceived()
	CONNECTL(&pext, packetReceived, ([this](const uchar * readed, ssize_t size) { packetReceived(PIByteArray(readed, size)); }));

	// our requests to device
	req_thread.setSlot([this] {
		static PIVector<PIByteArray> requests = {
			"$a#"_a.toAscii(), // Получить состояние оси поворота
			"$b#"_a.toAscii(), // Получить флаги ошибок оси поворота
			"$c#"_a.toAscii(), // Получить текущую позицию оси поворота
			"$d#"_a.toAscii(), // Получить текущую скорость оси поворота
			"$e#"_a.toAscii(), // Получить статус занятости оси поворота (текущее действие)
		};
		for (const auto & r: requests)
			ser.write(r);
	});
	req_thread.needLockRun(true);
}


Techlaser::~Techlaser() {
	close();
}


bool Techlaser::open(const PIString & serial) {
	close();
	ser.setSpeed(PISerial::S115200);
	if (!ser.open(serial)) return false;
	return true;
}


void Techlaser::close() {
	req_thread.stopAndWait();
	ser.stopAndWait();
	ser.close();
	state.set({});
}


void Techlaser::start(float rotate_speed_deg_s) {
	if (isClosed()) return;
	req_thread.lock();
	ser.write("$i,%1#"_a.arg(rotate_speed_deg_s).toAscii());
	req_thread.unlock();
	req_thread.start(100_Hz); /// TODO: set desired rate
}


void Techlaser::stop() {
	if (isClosed()) return;
	req_thread.lock();
	ser.write("$g#"_a.toAscii());
	req_thread.unlock();
}


void Techlaser::packetReceived(PIByteArray msg) {
	if (msg.size() < 3) return;
	char cmd = msg[1];
	msg.remove(0, 2).take_back(); // drop first 2 and last 1 bytes
	PIStringList args;
	if (msg.isNotEmpty()) {
		if (msg.front() == ',') msg.take_front(); // drop first ','
		args = PIStringAscii(msg).split(',');
	}
	switch (cmd) {
	case 'a':
		if (args.size() < 1) return;
		state.getRef()->device_status = (DeviceStatus)args[0].toInt();
		break;
	case 'b':
		if (args.size() < 1) return;
		state.getRef()->motor_error = args[0].toUInt(16) > 0;
		break;
	case 'e':
		if (args.size() < 1) return;
		state.getRef()->motor_status = (MotorStatus)args[0].toInt();
		break;
	case 'c':
		if (args.size() < 1) return;
		auto ref                     = state.getRef();
		ref->current_angle           = args[0].toFloat();
		ref->angle_receive_timestamp = PISystemTime::current();
		break;
	case 'd':
		if (args.size() < 1) return;
		state.getRef()->current_speed = args[0].toFloat();
		break;
	default: break;
	}
}


Techlaser::State Techlaser::getState() const {
	return *state.getRef();
}


double Techlaser::getAngleAgo(PISystemTime ago) const {
	auto s              = getState();
	PISystemTime now    = PISystemTime::current();
	PISystemTime target = now - ago;
	PISystemTime dt     = target - s.angle_receive_timestamp;
	double angle        = s.current_angle + s.current_speed * dt.toSeconds();
	angle               = std::fmod(angle, 360.0);
	if (angle < 0.0) angle += 360.0;
	return angle;
}
