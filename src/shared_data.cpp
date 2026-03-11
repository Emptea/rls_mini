#include "shared_data.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pistring_std.h>
#include <pivaluetree_conversions.h>

GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(PIString2StdString(u220_args)) {
	main_config = PIValueTreeConversions::fromTextFile("rls_mini.conf");
}


GlobalData::~GlobalData() {}


GlobalData * GlobalData::instance() {
	static GlobalData ret;
	return &ret;
}


void GlobalData::init() {
	initEth();
	PIVector<PIString> serial_list = uhd_utils.get_serials_list();
	device_addrs_filtered_t devices = uhd_utils.uhd_get_devices();
	auto dit                        = devices.begin();
	for (size_t i = 0; i < serial_list.size(); i++){
		u220_ptrs << new U220(serial_list[i], u220_args, 0, u220_config);
		if (StdString2PIString(dit->first) == u220_ptrs[i]->get_serial() & dit != devices.end()){
			u220_ptrs[i]->init();
			dit++;
		}
	}
}


void GlobalData::start() {
	startEth();
}


void GlobalData::stop() {
	stopEth();
	piDeleteAllAndClear(u220_ptrs);
}
