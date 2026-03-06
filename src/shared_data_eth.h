#ifndef SHARED_DATA_ETH_H
#define SHARED_DATA_ETH_H

#include "eth_storage.h"
#include "protocol_rls_mini.h"

#include <piethernet.h>


class GlobalData;


class GlobalDataEth: public EthStorage<GlobalDataEth> {
public:
	GlobalDataEth(GlobalData * g);
	~GlobalDataEth();

	void initEth();
	void stopEth();

	void receivedRLS(PIByteArray data, int port);

	void changeEndian(PIByteArray & data) {
		uchar * ptr = data.data();
		for (uint i = 0; i < data.size_s(); i += 2) {
			piSwap(ptr[i], ptr[i + 1]);
		}
	}

	template<typename T>
	void sendMessage(const T & msg) {
		if (!eth_rlso_send) return;
		PIByteArray data = piSerialize(msg);

		/// remove if wrong endian
		changeEndian(data);

		auto addr = eth_rlso_send->sendAddress();
		addr.setPort(T::Port);
		piCout << "send msg" << data;
		eth_rlso_send->send(addr, data);
	}

	// clang-format off
	/// Удалить ненужные
	void received_RR_Zapros       (const Protocol_RLS_Mini::RR_Zapros      & msg) {piCout << "rec msg" << "received_RR_Zapros    ";}
	void received_RR_Vr           (const Protocol_RLS_Mini::RR_Vr          & msg) {piCout << "rec msg" << "received_RR_Vr        ";}
	void received_RR_Izl          (const Protocol_RLS_Mini::RR_Izl         & msg) {piCout << "rec msg" << "received_RR_Izl       ";}
	void received_RR_Ant          (const Protocol_RLS_Mini::RR_Ant         & msg) {piCout << "rec msg" << "received_RR_Ant       ";}
	void received_RR_TTek         (const Protocol_RLS_Mini::RR_TTek        & msg) {piCout << "rec msg" << "received_RR_TTek      ";}
	void received_RR_AzPopr       (const Protocol_RLS_Mini::RR_AzPopr      & msg) {piCout << "rec msg" << "received_RR_AzPopr    ";}
	void received_RR_DPopr_POI    (const Protocol_RLS_Mini::RR_DPopr_POI   & msg) {piCout << "rec msg" << "received_RR_DPopr_POI ";}
	void received_RR_AzPopr_POI   (const Protocol_RLS_Mini::RR_AzPopr_POI  & msg) {piCout << "rec msg" << "received_RR_AzPopr_POI";}
	void received_RR_Kvit         (const Protocol_RLS_Mini::RR_Kvit        & msg) {piCout << "rec msg" << "received_RR_Kvit      ";}
	void received_POI_Zapros      (const Protocol_RLS_Mini::POI_Zapros     & msg) {piCout << "rec msg" << "received_POI_Zapros   ";}
	void received_POI_Shtat       (const Protocol_RLS_Mini::POI_Shtat      & msg) {piCout << "rec msg" << "received_POI_Shtat    ";}
	void received_POI_SDC         (const Protocol_RLS_Mini::POI_SDC        & msg) {piCout << "rec msg" << "received_POI_SDC      ";}
	void received_POI_DSA         (const Protocol_RLS_Mini::POI_DSA        & msg) {piCout << "rec msg" << "received_POI_DSA      ";}
	void received_POI_APU         (const Protocol_RLS_Mini::POI_APU        & msg) {piCout << "rec msg" << "received_POI_APU      ";}
	void received_POI_Zona        (const Protocol_RLS_Mini::POI_Zona       & msg) {piCout << "rec msg" << "received_POI_Zona     ";}
	void received_POI_Kan         (const Protocol_RLS_Mini::POI_Kan        & msg) {piCout << "rec msg" << "received_POI_Kan      ";}
	void received_POI_Kvit        (const Protocol_RLS_Mini::POI_Kvit       & msg) {piCout << "rec msg" << "received_POI_Kvit     ";}
	void received_POI_TK_Zapros   (const Protocol_RLS_Mini::POI_TK_Zapros  & msg) {piCout << "rec msg" << "received_POI_TK_Zapros";}
	void received_POI_TK_Kvit     (const Protocol_RLS_Mini::POI_TK_Kvit    & msg) {piCout << "rec msg" << "received_POI_TK_Kvit  ";}
	void received_CIT_Zapros      (const Protocol_RLS_Mini::CIT_Zapros     & msg) {piCout << "rec msg" << "received_CIT_Zapros   ";}
	void received_CIT_KU          (const Protocol_RLS_Mini::CIT_KU         & msg) {piCout << "rec msg" << "received_CIT_KU       ";}
	void received_CIT_Kvit        (const Protocol_RLS_Mini::CIT_Kvit       & msg) {piCout << "rec msg" << "received_CIT_Kvit     ";}
	void received_PI              (const Protocol_RLS_Mini::PI             & msg) {piCout << "rec msg" << "received_PI           ";}
	void received_KTA_VO          (const Protocol_RLS_Mini::KTA_VO         & msg) {piCout << "rec msg" << "received_KTA_VO       ";}
	void received_TRVO            (const Protocol_RLS_Mini::TRVO           & msg) {piCout << "rec msg" << "received_TRVO         ";}
	void received_TRETA           (const Protocol_RLS_Mini::TRETA          & msg) {piCout << "rec msg" << "received_TRETA        ";}
	void received_STRSOPR         (const Protocol_RLS_Mini::STRSOPR        & msg) {piCout << "rec msg" << "received_STRSOPR      ";}
	void received_KORTR           (const Protocol_RLS_Mini::KORTR          & msg) {piCout << "rec msg" << "received_KORTR        ";}
	void received_KV_KORTR        (const Protocol_RLS_Mini::KV_KORTR       & msg) {piCout << "rec msg" << "received_KV_KORTR     ";}
	void received_CMD_ZZT         (const Protocol_RLS_Mini::CMD_ZZT        & msg) {piCout << "rec msg" << "received_CMD_ZZT      ";}
	void received_ZZT             (const Protocol_RLS_Mini::ZZT            & msg) {piCout << "rec msg" << "received_ZZT          ";}
	void received_CMD_ZBL         (const Protocol_RLS_Mini::CMD_ZBL        & msg) {piCout << "rec msg" << "received_CMD_ZBL      ";}
	void received_ZBL             (const Protocol_RLS_Mini::ZBL            & msg) {piCout << "rec msg" << "received_ZBL          ";}
	void received_AZIMUTH         (const Protocol_RLS_Mini::AZIMUTH        & msg) {piCout << "rec msg" << "received_AZIMUTH      ";}
	void received_VKL_REG         (const Protocol_RLS_Mini::VKL_REG        & msg) {piCout << "rec msg" << "received_VKL_REG      ";}
	void received_OTKL_REG        (const Protocol_RLS_Mini::OTKL_REG       & msg) {piCout << "rec msg" << "received_OTKL_REG     ";}
	void received_ZPR_SOST_REG    (const Protocol_RLS_Mini::ZPR_SOST_REG   & msg) {piCout << "rec msg" << "received_ZPR_SOST_REG ";}
	void received_SOST_REG        (const Protocol_RLS_Mini::SOST_REG       & msg) {piCout << "rec msg" << "received_SOST_REG     ";}
	// clang-format on

protected:
	GlobalData * global        = nullptr;
	PIEthernet * eth_rlso_send = nullptr;

private:
};

#endif
