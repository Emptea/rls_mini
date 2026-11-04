#include "rx_cycle_trace.hpp"

#include <fstream>
#include <string>
#include <vector>

bool write_rx_cycle_csv(const std::vector<RxCycleTraceEntry> & entries, const std::string & filename) {
	std::ofstream file(filename);

	if (!file) {
		return false;
	}

	file << "iteration;start_ns;cycle_us;cycle_period_us;"
			"cpu_us;off_cpu_us;"
			"recv_total_us;overhead_us;"
			"receive0_us;receive1_us;receive2_us;receive3_us;"
			"error0;error1;error2;error3\n";

	for (const auto & e: entries) {
		file << e.iteration << ';' << e.start_ns << ';' << e.cycle_us << ';' << e.cycle_period_us << ';' << e.cpu_us << ';' << e.off_cpu_us
			 << ';' << e.recv_total_us << ';' << e.overhead_us;

		for (const auto value: e.board_receive_us) {
			file << ';' << value;
		}

		for (const auto value: e.board_error) {
			file << ';' << value;
		}

		file << '\n';
	}

	file.flush();
	return file.good();
}