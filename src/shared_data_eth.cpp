#include "shared_data_eth.h"

#include "ccm_protocol.h"
#include "shared_data.h"

#include <piliterals_bytes.h>
#include <piliterals_time.h>
#include <pivaluetree_conversions.h>


GlobalDataEth::GlobalDataEth(GlobalData * g) {
	global = g;
}


GlobalDataEth::~GlobalDataEth() {}


void GlobalDataEth::initEth() {
	const auto & net_conf = global->mainConfig().child("net");

	/// RLSO start
	PIStringList my_dest  = {"poi", "upr", "it", "soi", "all"};

	PIEthernet * _eth     = PIIODevice::createFromFullPath(net_conf.childValue("rls").toString())->cast<PIEthernet>();
	if (_eth) {
		eth_rlso_send = new PIEthernet(PIEthernet::UDP);
		eth_rlso_send->setSendAddress(_eth->sendAddress());
		if (_eth) {
			/*PIString d_s_ip = "255.255.255.255";
			if (_eth) {
			    PIEthernet::InterfaceList ifaces   = PIEthernet::interfaces();
			    const PIEthernet::Interface * cint = 0;
			    cint                               = ifaces.getByAddress(_eth->readIP());
			    if (cint) {
			        d_s_ip = PIEthernet::getBroadcast(_eth->readIP(), cint->netmask);
			        piCout << _eth->readIP() << cint->netmask << d_s_ip;
			    }
			}*/
			PICodeInfo::ClassInfo * ci = PICODEINFO::classes().value("Protocol_RLS_Mini");
			PISet<int> used_ports;
			if (ci) {
				for (auto * i: ci->children_info) {
					// piCout << i->name << i->meta;
					if (!my_dest.contains(i->meta.value("dest"))) continue;
					int port = i->meta.value("port").toInt();
					// piCout << "rec" << PIString(i->name).mid(19) << "on port" << port;
					if (used_ports[port]) continue;
					used_ports << port;
					PIEthernet * e = new PIEthernet(PIEthernet::UDP);
					CONNECTL(e, threadedReadEvent, ([this, port](const uchar * readed, ssize_t size) {
								 receivedRLS(PIByteArray(readed, size), port);
							 }));
					e->setProperty("_port_", port);
					e->setParameter(PIEthernet::Broadcast);
					e->setReadIP(_eth->readIP());
					e->setReadPort(port);
					_addEth(e);
					piCout << "add eth on" << e->readAddress();
				}
			}
			delete _eth;
		}
	}

	/// RLSO end
}


void GlobalDataEth::stopEth() {
	piCout << "stop eth";
	EthStorage<GlobalDataEth>::stopEth();
	piDeleteSafety(eth_rlso_send);
}

template<typename T>
bool RLS_CheckType(const Protocol_RLS_Mini::Header & header, int port) {
	if (T::Port != port) return false;
	if (T::Type >= 0 && T::Type != header.msg_type) return false;
	if (T::Code >= 0 && T::Code != header.msg_code) return false;
	return true;
}

// piCout << "parse msg" #T;
#define RLS_TRY_PARSE(T)                                                      \
	if (RLS_CheckType<Protocol_RLS_Mini::T>(header, port)) {                  \
		Protocol_RLS_Mini::T msg = piDeserialize<Protocol_RLS_Mini::T>(data); \
		global->received_##T(msg);                                            \
		return;                                                               \
	}

void GlobalDataEth::receivedRLS(PIByteArray data, int port) {
	/// remove if wrong endian
	changeEndian(data);

	Protocol_RLS_Mini::Header header;
	if (data.size() < sizeof(header)) return;
	memcpy(&header, data.data(), sizeof(header));

	// clang-format off
	/// Удалить ненужные
	RLS_TRY_PARSE(RR_Zapros     );
	RLS_TRY_PARSE(RR_Vr         );
	RLS_TRY_PARSE(RR_Izl        );
	RLS_TRY_PARSE(RR_Ant        );
	RLS_TRY_PARSE(RR_TTek       );
	RLS_TRY_PARSE(RR_AzPopr     );
	RLS_TRY_PARSE(RR_DPopr_POI  );
	RLS_TRY_PARSE(RR_AzPopr_POI );
	RLS_TRY_PARSE(POI_Zapros    );
	RLS_TRY_PARSE(POI_Shtat     );
	RLS_TRY_PARSE(POI_SDC       );
	RLS_TRY_PARSE(POI_DSA       );
	RLS_TRY_PARSE(POI_APU       );
	RLS_TRY_PARSE(POI_Zona      );
	RLS_TRY_PARSE(POI_Kan       );
	RLS_TRY_PARSE(POI_TK_Zapros );
	RLS_TRY_PARSE(CIT_Zapros    );
	RLS_TRY_PARSE(CIT_KU        );
	RLS_TRY_PARSE(TRVO          );
	RLS_TRY_PARSE(TRETA         );
	RLS_TRY_PARSE(AZIMUTH       );
	// clang-format on
}

#undef RLS_TRY_PARSE
