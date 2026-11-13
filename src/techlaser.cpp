#include "techlaser.h"

#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <linux/serial.h>
#include <piliterals_string.h>
#include <piliterals_time.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool initializeRs485(const PIString & device) {
	const int fd = ::open(device.dataAscii(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		std::perror("open RS485");
		return false;
	}

	serial_rs485 disabled{};
	int rts = TIOCM_RTS;
	bool ok = false;

	if (::ioctl(fd, TIOCSRS485, &disabled) < 0) {
		std::perror("TIOCSRS485 disable");
	} else if (::ioctl(fd, TIOCMBIC, &rts) < 0) {
		std::perror("TIOCMBIC RTS");
	} else {
		ok = true; // RTS сброшен: на твоей плате DE = 0
	}

	::close(fd);
	return ok;
}


Techlaser::Techlaser() {
	CONNECTL(&ser, threadedReadEvent, ([this](const uchar * data, ssize_t size) {
		if (size > 0) processAngleBytes(data, size);
	}));
}

Techlaser::~Techlaser() {
	close();
}

static FILE * uart_dump;

bool Techlaser::open(const PIString & serial) {
	close();

	ser.setSpeed(PISerial::S115200);
	ser.setDataBitsCount(8);
	ser.setParameter(PISerial::ParityControl, false);
	ser.setParameter(PISerial::TwoStopBits, false);
	if (!ser.open(serial)) {
		return false;
	}
	if (!initializeRs485(serial)) {
		ser.close();
		return false;
	}
	angle_receiver.reset();
	uart_dump = std::fopen("techlaser_uart.csv", "w");
	if (!ser.isThreadedRead()) {
		ser.startThreadedRead();
	}
	piCout << "Techlaser: open port";
	return true;
}

void Techlaser::close() {
	ser.stopAndWait();
	ser.close();
	if (uart_dump) {
		std::fclose(uart_dump);
		uart_dump = nullptr;
	}
	angle_receiver.reset();
	have_speed_ref = false;
	state.set({});
}
void Techlaser::processAngleBytes(const uchar * data, ssize_t size) {
	float angle    = 0.0f;
	bool updated   = false;

	const auto now = std::chrono::steady_clock::now();
	const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

	if (uart_dump) {
		std::fprintf(uart_dump, "%lld;%zd", static_cast<long long>(ns), size);
		for (ssize_t i = 0; i < size; ++i)
			std::fprintf(uart_dump, ";%02X", static_cast<unsigned>(data[i]));
		std::fprintf(uart_dump, "\n");
	}

	for (ssize_t i = 0; i < size; ++i) {
		if (angle_receiver.push(data[i], angle)) updated = true;
	}

	if (!updated) return;

	auto ref = state.getRef();

	if (!have_speed_ref) {
		speed_ref_angle = angle;
		speed_ref_time  = now;
		have_speed_ref  = true;
	} else {
		const double dt = std::chrono::duration<double>(now - speed_ref_time).count();

		if (dt >= 0.1) {
			double delta = angle - speed_ref_angle;

			if (delta > 180.0) delta -= 360.0;
			if (delta < -180.0) delta += 360.0;

			const float speed  = static_cast<float>(delta / dt);
			// ref->current_speed = speed;
			ref->current_speed = 0.7f * ref->current_speed + 0.3f * speed;

			speed_ref_angle    = angle;
			speed_ref_time     = now;
			// piCout << "Tech: angle " << angle;
			// piCout << "Tech: velocity " << speed;
			// piCout << "Tech: raw" << PICoutManipulators::Hex << static_cast<unsigned>(data[size - 2]) << static_cast<unsigned>(data[size - 1]);
		}
	}

	ref->current_angle           = angle;
	ref->angle_receive_timestamp = now;
}

Techlaser::State Techlaser::getState() const {
	return *state.getRef();
}

double Techlaser::getAngleAgo(std::chrono::steady_clock::time_point target_time) const {
	const auto s    = getState();
	const double dt = std::chrono::duration<double>(target_time - s.angle_receive_timestamp).count();
	double angle    = s.current_angle + s.current_speed * dt;
	angle           = std::fmod(angle, 360.0);
	if (angle < 0.0) {
		angle += 360.0;
	}

	return angle;
}
