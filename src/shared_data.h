#ifndef shared_data_H
#define shared_data_H

#include "shared_data_eth.h"

#include <pivaluetree.h>


#define GLOBAL (GlobalData::instance())

class GlobalData
	: public PIObject
	, public GlobalDataEth {
	PIOBJECT(GlobalData)

public:
	static GlobalData * instance();

	void init();
	void start();
	void stop();

	const PIValueTree & mainConfig() const { return main_config; }

protected:

private:
	GlobalData();
	~GlobalData();

	PIValueTree main_config;
};

#endif
