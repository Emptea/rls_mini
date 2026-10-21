#include "techlaser.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/serial.h>
#include <piliterals_string.h>
#include <piliterals_time.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool parseNumber(const PIString & text, double & value) {
	const char * begin = text.dataAscii();
	char * end         = nullptr;

	errno              = 0;
	value              = std::strtod(begin, &end);

	return end != begin && *end == '\0' && errno != ERANGE && std::isfinite(value);
}

bool parseFaults(const PIString & text, unsigned long & value) {
	const char * begin = text.dataAscii();

	// По протоколу — 32-битное шестнадцатеричное число.
	if (*begin == '\0' || std::strlen(begin) > 8) return false;

	for (const char * p = begin; *p; ++p) {
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
	}

	char * end = nullptr;
	errno      = 0;
	value      = std::strtoul(begin, &end, 16);

	return *end == '\0' && errno != ERANGE && value <= 0xffffffffUL;
}

} // namespace

static bool initializeRs485(const PIString & device) {
	const int fd = ::open(device.dataAscii(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		std::perror("open RS485");
		return false;
	}

	serial_rs485 config{};
	config.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;

	bool ok      = false;

	if (::ioctl(fd, TIOCSRS485, &config) < 0) {
		std::perror("TIOCSRS485");
	} else {
		serial_rs485 actual{};

		if (::ioctl(fd, TIOCGRS485, &actual) < 0) {
			std::perror("TIOCGRS485");
		} else {
			const unsigned mask = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND;

			ok                  = (actual.flags & mask) == config.flags;

			if (!ok) std::fprintf(stderr, "Unexpected RS485 flags: 0x%x\n", actual.flags);
		}
	}

	::close(fd);
	return ok;
}


Techlaser::Techlaser() {
	pext.setSplitMode(PIPacketExtractor::HeaderAndFooter);
	pext.setHeader({'$'});
	pext.setFooter({'#'});

	CONNECTL(&ser, threadedReadEvent, ([this](const uchar * data, ssize_t size) { pext.appendData(data, size); }));
	CONNECTL(&pext, packetReceived, ([this](const uchar * data, ssize_t size) { packetReceived(PIByteArray(data, size)); }));
	req_thread.setSlot([this] { processExchange(); });

	req_thread.needLockRun(true);
}


Techlaser::~Techlaser() {
	close();
}


bool Techlaser::open(const PIString & serial) {
	close();

	if (!initializeRs485(serial)) return false;

	ser.setSpeed(PISerial::S115200);
	ser.setDataBitsCount(8);
	ser.setParameter(PISerial::ParityControl, false);
	ser.setParameter(PISerial::TwoStopBits, false);

	if (!ser.open(serial)) return false;

	{
		std::lock_guard<std::mutex> lock(exchange_mutex);
		control_queue.clear();
		waiting_reply   = false;
		expected_reply  = '\0';
		polling         = true;
		poll_index      = 0;
		next_poll_cycle = Clock::now();
	}

	if (!ser.isThreadedRead()) ser.startThreadedRead();

	req_thread.start(20_Hz);
	return true;
}

void Techlaser::close() {
	// Не удерживаем exchange_mutex при ожидании завершения потоков.
	req_thread.stopAndWait();
	ser.stopAndWait();
	ser.close();

	{
		std::lock_guard<std::mutex> lock(exchange_mutex);
		control_queue.clear();
		waiting_reply  = false;
		expected_reply = '\0';
		polling        = false;
		poll_index     = 0;
	}

	state.set({});
}

void Techlaser::start(float rotate_speed_deg_s) {
	if (isClosed()) return;

	if (!std::isfinite(rotate_speed_deg_s)) {
		std::fprintf(stderr, "Techlaser: invalid rotation speed\n");
		return;
	}

	const PIByteArray command = "$i,%1#"_a.arg(rotate_speed_deg_s).toAscii();

	std::lock_guard<std::mutex> lock(exchange_mutex);

	// Новое задание скорости заменяет ещё не отправленное старое.
	// Уже ожидающая отправки остановка сохраняется.
	for (auto it = control_queue.begin(); it != control_queue.end();) {
		if (it->command == 'i')
			it = control_queue.erase(it);
		else
			++it;
	}

	control_queue.push_back(Request{command, 'i'});

	// polling         = true;
	poll_index      = 0;
	next_poll_cycle = Clock::now();
}

void Techlaser::stop() {
	if (isClosed()) return;

	std::lock_guard<std::mutex> lock(exchange_mutex);

	// Отменяем все ещё не отправленные команды управления.
	control_queue.clear();
	control_queue.push_front(Request{"$g#"_a.toAscii(), 'g'});

	// Продолжаем опрос, чтобы наблюдать снижение скорости.
	polling         = true;
	poll_index      = 0;
	next_poll_cycle = Clock::now();
}

void Techlaser::processExchange() {
	if (isClosed()) return;

	static const PIByteArray queries[] = {
		"$a#"_a.toAscii(),
		"$b#"_a.toAscii(),
		"$c#"_a.toAscii(),
		"$d#"_a.toAscii(),
		"$e#"_a.toAscii(),
	};

	static const char commands[] = {'a', 'b', 'c', 'd', 'e'};

	Request request{};

	{
		std::lock_guard<std::mutex> lock(exchange_mutex);
		const auto now = Clock::now();

		if (waiting_reply) {
			if (now < reply_deadline) return;

			std::fprintf(stderr, "Techlaser: timeout waiting for '%c'; continuing\n", expected_reply);

			// Пропускаем запрос без ответа.
			// Опрос и очередь управления сохраняются.
			waiting_reply  = false;
			expected_reply = '\0';
		}

		// Команды управления имеют приоритет над опросом.
		if (!control_queue.empty()) {
			request = control_queue.front();
			control_queue.pop_front();
		} else {
			if (!polling) return;

			if (poll_index == 0) {
				if (now < next_poll_cycle) return;

				next_poll_cycle = now + poll_period;
			}

			request.bytes   = queries[poll_index];
			request.command = commands[poll_index];
			poll_index      = (poll_index + 1) % 5;
		}

		// Устанавливаем ожидание до отправки,
		// чтобы не пропустить быстрый ответ.
		waiting_reply  = true;
		expected_reply = request.command;
		reply_deadline = now + reply_timeout;
	}

	// Отправляем без удержания mutex, чтобы приём мог работать.
	if (!ser.send(request.bytes)) {
		std::lock_guard<std::mutex> lock(exchange_mutex);

		waiting_reply  = false;
		expected_reply = '\0';

		// polling и control_queue не меняем.
		std::fprintf(stderr, "Techlaser: failed to send '%c'; continuing\n", request.command);
	}

	std::fprintf(stderr, "Techlaser: failed to send '%c'; exchange suspended\n", request.command);
}


void Techlaser::packetReceived(PIByteArray msg) {
	if (msg.size() < 3 || msg[0] != '$' || msg[msg.size() - 1] != '#') return;

	const char cmd = msg[1];

	// Удаляем '$', букву команды и '#'.
	msg.remove(0, 2).take_back();
	const bool has_payload = msg.isNotEmpty();
	PIStringList args;
	if (has_payload) {
		if (msg.front() != ',') return;

		msg.take_front();
		args = PIStringAscii(msg).split(',');
	}

	double value         = 0.0;
	unsigned long faults = 0;

	switch (cmd) {
	case 'a':
	case 'c':
	case 'd':
	case 'e':
	case 'i':
		if (args.size() != 1 || !parseNumber(args[0], value)) return;
		break;

	case 'b':
		if (args.size() != 1 || !parseFaults(args[0], faults)) return;
		break;

	case 'g':
	case 'X':
		if (has_payload) return;
		break;

	default: return;
	}

	// Проверяем значения перечислений и положения.
	if (cmd == 'a' && (value < 0 || value > 3 || std::floor(value) != value)) return;

	if (cmd == 'e' && (value < 0 || value > 5 || std::floor(value) != value)) return;

	if (cmd == 'c' && (value < 0 || value >= 360)) return;

	std::lock_guard<std::mutex> lock(exchange_mutex);

	// Чужой или запоздавший ответ не завершает текущую транзакцию.
	if (!waiting_reply) return;

	if (cmd == 'X') {
		std::fprintf(stderr, "Techlaser: device rejected command '%c'; continuing\n", expected_reply);

		waiting_reply  = false;
		expected_reply = '\0';
		return;
	}

	if (cmd != expected_reply) return;

	{
		auto ref = state.getRef();

		switch (cmd) {
		case 'a': ref->device_status = static_cast<DeviceStatus>(static_cast<int>(value)); break;
		case 'b': ref->motor_error = faults != 0; break;
		case 'c':
			ref->current_angle           = static_cast<float>(value);
			ref->angle_receive_timestamp = PISystemTime::current();
			break;
		case 'd': ref->current_speed = static_cast<float>(value); break;
		case 'e': ref->motor_status = static_cast<MotorStatus>(static_cast<int>(value)); break;
		case 'i':
			// Ответ содержит целевую, а не текущую скорость.
			break;

		case 'g':
			// Подтверждение команды, не завершения остановки.
			break;
		}
	}

	waiting_reply  = false;
	expected_reply = '\0';
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
