#include "axu_header_logger.hpp"

#include "rls.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

AxuHeaderLogger::~AxuHeaderLogger() {
	stop();
}

bool AxuHeaderLogger::start(const std::string & directory) {
	stop();

	namespace fs = std::filesystem;

	std::error_code ec;
	fs::create_directories(directory, ec);

	if (ec) {
		fprintf(stderr, "RX logger: cannot create directory: %s\n", ec.message().c_str());
		return false;
	}

	const auto timestamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	// const auto filename = directory + "/rx_headers_" + std::to_string(timestamp) + ".hex";
	const auto filename = directory + "/rx_headers.log";

	file.open(filename, std::ios::out | std::ios::trunc);

	if (!file) {
		fprintf(stderr, "RX logger: cannot open %s\n", filename.c_str());
		return false;
	}

	dropped.store(0);

	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
		stopping = false;
		running  = true;
	}

	worker = std::thread(&AxuHeaderLogger::worker_loop, this);

	fprintf(stderr, "RX header logger started: %s\n", filename.c_str());

	return true;
}

void AxuHeaderLogger::write(const rls::header & hdr) {
	static_assert(sizeof(rls::header) == 24, "Unexpected RX header size");

	Record record;
	std::memcpy(record.data(), &hdr, sizeof(hdr));

	{
		std::lock_guard<std::mutex> lock(mutex);

		if (!running || stopping) return;

		if (queue.size() >= MAX_QUEUE_SIZE) {
			dropped.fetch_add(1);
			return;
		}

		queue.push_back(record);
	}

	cv.notify_one();
}

void AxuHeaderLogger::stop() {
	{
		std::lock_guard<std::mutex> lock(mutex);

		if (!running) return;

		stopping = true;
	}

	cv.notify_one();

	if (worker.joinable()) {
		worker.join();
	}

	file.close();

	{
		std::lock_guard<std::mutex> lock(mutex);
		running  = false;
		stopping = false;
	}

	fprintf(stderr, "RX header logger stopped, dropped=%llu\n", static_cast<unsigned long long>(dropped.load()));
}

void AxuHeaderLogger::worker_loop() {
	while (true) {
		std::deque<Record> batch;

		{
			std::unique_lock<std::mutex> lock(mutex);

			cv.wait(lock, [this] { return stopping || !queue.empty(); });

			if (queue.empty() && stopping) break;

			batch.swap(queue);
		}

		for (const auto & record: batch) {
			write_record(record);
		}
	}

	file.flush();
}

void AxuHeaderLogger::write_record(const Record & record) {
	static constexpr char HEX[] = "0123456789ABCDEF";

	char line[66];
	size_t pos = 0;

	for (const uint32_t word: record) {
		line[pos++] = '0';
		line[pos++] = 'x';

		for (int shift = 28; shift >= 0; shift -= 4) {
			line[pos++] = HEX[(word >> shift) & 0xF];
		}

		line[pos++] = '\n';
	}

	file.write(line, sizeof(line));
}