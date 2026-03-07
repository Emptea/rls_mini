#include "shared_data.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pivaluetree_conversions.h>


GlobalData::GlobalData(): GlobalDataEth(this), uhd_utils(args) {
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
	dit = devices.begin();
	for (size_t i = 0; i < serial_list.size(); i++){
		u220_ptrs << new U220(serial_list(i), args, 0, config);
		if (dit->first == u220_ptrs[i]->get_serial() & dit != devices.end()){
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
