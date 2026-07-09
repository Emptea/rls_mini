#pragma once

#include "rlso_const.hpp"

#include <piprotectedvariable.h>
#include <pisemaphore.h>
#include <pithread.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>


#define N_SAMPS_IN_TX_BUF SAMPLES_PER_CYCLE
#define N_PACKS_IN_TX_BUF 1

#define NUM_CHANNELS_RX   1
#define NUM_CHANNELS_TX   8

#define BUFFER_SIZE       (sizeof(unsigned int) * U220_SPB) /* must match driver exactly */

#define TX_BUFFER_COUNT   2 /* app only, must be <= to the number in the driver */
#define RX_BUFFER_COUNT   4 /* app only, must be <= to the number in the driver */

static const PIString rx_devnode                   = "/dev/dma_proxy_rx";

static const PIString tx_devnodes[NUM_CHANNELS_TX] = {"/dev/dma_proxy_tx_ch0",
                                                      "/dev/dma_proxy_tx_ch1",
                                                      "/dev/dma_proxy_tx_ch2",
                                                      "/dev/dma_proxy_tx_ch3",
                                                      "/dev/dma_proxy_tx_ch4",
                                                      "/dev/dma_proxy_tx_ch5",
                                                      "/dev/dma_proxy_tx_ch6",
                                                      "/dev/dma_proxy_tx_ch7"};

class dma_channel: public PIThread {
	PIOBJECT_SUBCLASS(dma_channel, PIThread)

public:
	struct channel_buffer {
		unsigned int buffer[BUFFER_SIZE / sizeof(unsigned int)];
		enum proxy_status {
			PROXY_NO_ERROR = 0,
			PROXY_BUSY     = 1,
			PROXY_TIMEOUT  = 2,
			PROXY_ERROR    = 3
		} status;
		unsigned int length;
	} __attribute__((aligned(64))); /* 64 byte alignment required for DMA, but 1024 handy for viewing memory */

	struct ch_config {
		std::string devnode;
		int buffer_size;
		int buffer_count;
	} config;

private:
	struct channel {
		channel_buffer * buf_ptr = nullptr; // proxy‑driver ring
		int fd                   = -1;
		int buffer_size          = 0;
		int counter              = 0;
		int buffer_id            = 0;
		int in_progress_count    = 0;
		int buffer_count         = 1;
	} ch;

	int num_transfers  = 0;
	bool flag_save_buf = false;
	FILE * dump_file;
	int n_samps_per_buf = 232;

	void save_buf_to_file(void * buffer, int N);

public:
	int init(ch_config cfg);
	void cleanup();
	void start_transfer();
	void start_transfer_for_buf(int buffer_id);
	int wait_for_transfer();

	void begin() override {
		printf("Start DMA devnode %s\n", config.devnode.c_str());

		for (ch.buffer_id = 0; ch.buffer_id < ch.buffer_count; ++ch.buffer_id) {
			ch.buf_ptr[ch.buffer_id].length = ch.buffer_size;
			start_transfer_for_buf(ch.buffer_id);
			if (num_transfers && (ch.in_progress_count >= num_transfers)) break;
		}

		ch.buffer_id = 0;
	}

	void run() override {
		if (wait_for_transfer() || (num_transfers && (ch.counter >= num_transfers))) {
			stop();
			return;
		}

		if (num_transfers && ((ch.counter + ch.in_progress_count) < num_transfers)) {
			start_transfer_for_buf(ch.buffer_id);
			piCout << "Started transfer for buffer " << ch.buffer_id << "global cnt is " << ch.buffer_count;
		}
		ch.buffer_id = (ch.buffer_id + 1) % ch.buffer_count;
	}

	void end() override { cleanup(); }


	void * get_buffer(size_t num) const { return ch.buf_ptr[num].buffer; }

	void get_all_buffers(void ** buffer_array) const {
		for (size_t i = 0; i < ch.buffer_count; ++i) {
			buffer_array[i] = static_cast<void *>(&ch.buf_ptr[i].buffer);
		}
	}
	int get_counter() { return ch.counter; }
	int get_buffer_id() { return ch.buffer_id; }

	void set_num_transfers(int n_trans) { num_transfers = n_trans; }

	void set_save_to_file(PIString f_name, int n_samps) {
		flag_save_buf   = true;
		n_samps_per_buf = n_samps;
		dump_file       = fopen(f_name.data(), "w");
	}
};