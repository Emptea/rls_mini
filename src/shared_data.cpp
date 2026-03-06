#include "shared_data.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pivaluetree_conversions.h>


GlobalData::GlobalData(): GlobalDataEth(this) {
	main_config = PIValueTreeConversions::fromTextFile("rls_mini.conf");
}


GlobalData::~GlobalData() {}


GlobalData * GlobalData::instance() {
	static GlobalData ret;
	return &ret;
}


void GlobalData::init() {
	initEth();
}


void GlobalData::start() {
	startEth();
}


void GlobalData::stop() {
	stopEth();
}
