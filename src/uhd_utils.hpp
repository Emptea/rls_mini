#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <iostream>
#include <uhd/device.hpp>
#include <uhd/utils/safe_main.hpp>

typedef std::map<std::string, std::set<std::string>> device_multi_addrs_t;
typedef std::map<std::string, device_multi_addrs_t> device_addrs_filtered_t;

class UHD_UTILS {
private:
	std::string device_args;
	device_addrs_filtered_t found_devices;

	void find_devices();

public:
	UHD_UTILS(const std::string & args = "");
	~UHD_UTILS();
	void uhd_print_devices();
	device_addrs_filtered_t uhd_get_devices();
};
