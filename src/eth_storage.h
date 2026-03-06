#ifndef eth_storage_H
#define eth_storage_H

#include <piethernet.h>


template<typename P>
class EthStorage {
public:
	~EthStorage() { piDeleteAllAndClear(all_eth); }

	void startEth() {
		for (auto * e: all_eth) {
			e->setDebug(false);
			e->startThreadedRead();
		}
	}

	void stopEth() {
		for (auto * e: all_eth)
			e->stopAndWait();
	}

	void _addEth(PIEthernet * e) { all_eth << e; }

	bool eth_enabled = true;

protected:
	PIEthernet * createEth(const PIString & fullpath, void (P::*rec_func)(PIByteArray) = nullptr) {
		auto eth = PIIODevice::createFromFullPath(fullpath)->cast<PIEthernet>();
		if (!eth) return nullptr;
		if (rec_func) {
			eth->setParameter(PIEthernet::SeparateSockets);
			P * parent = static_cast<P *>(this);
			CONNECTL(eth, threadedReadEvent, ([this, parent, rec_func](const uchar * readed, ssize_t size) {
						 if (!eth_enabled) return;
						 (parent->*rec_func)(PIByteArray(readed, size));
					 }));
		} else
			eth->setReadAddress("");
		all_eth << eth;
		return eth;
	};

private:
	PIVector<PIEthernet *> all_eth;
};

#endif
