#pragma once

#include <cstdint>
#include <memory>
#include <uhd/usrp/multi_usrp.hpp>

class UserU220 {
public:
	explicit UserU220(uhd::usrp::multi_usrp::sptr usrp);

	void set_play(uint32_t enable, uint32_t trigger_src, uint32_t gain_on_enable, uint32_t rx_enable);

	void set_tx0_delay(uint32_t dac_delay, uint32_t gainon_delay);
	void set_tx1_delay(uint32_t dac_delay, uint32_t gainon_delay);
	void set_rx0_delay(uint32_t val);
	void set_rx1_delay(uint32_t val);
	void set_rx0_en_delay(uint32_t val);
	void set_rx1_en_delay(uint32_t val);

	void set_play_enable(uint32_t enable);
	void set_play_trigger_src(uint32_t trigger_src);
	void set_play_gain_on_enable(uint32_t gain_on_enable);
	void set_play_rx_enable(uint32_t rx_enable);

	void set_play_gpio_tx_enable(uint32_t gpio_enable);
	void set_play_gpio_rx_enable(uint32_t gpio_enable);
	void set_play_start_pulse_enable(uint32_t start_pulse_enable);
	void set_play_start_pulse_40m(uint32_t start_pulse_40m);
	void set_play_rx_insert_count(uint32_t rx_insert_count);
	void reset_time(uint32_t pps_time_reset);

	void set_play_start_tx();
	void set_play_stop_tx();

	void sync_ad9361_stage1();
	void sync_ad9361_stage2();
	void sync_ad9361_master();
	void sync_ad9361_finish();

	void set_tx_dac_sg(size_t channel, const std::array<std::complex<short>, 232 * 4> & samples);
	void set_tx_gainon_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples);
	void set_rx_en_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples);


private:
	enum class WrReg : uint32_t {
		PLAY_CTRL         = 0,
		MEM_PTR           = 1,
		TX0_DAC_MEM       = 2,
		TX0_GAINON_MEM    = 3,
		TX0_DELAY         = 4,
		RX0_DELAY         = 5,
		RX0_EN_DELAY      = 6,
		RX0_EN_MEM        = 7,
		RESET_TIME        = 8,
		CAT_SYNC_WIDTH    = 9,
		TX0_GAIN          = 10,
		TX1_GAIN          = 11,
		TX1_DAC_MEM       = 12,
		TX1_DELAY         = 13,
		RX1_DELAY         = 14,
		RX_PLAYBACK_START = 15,
		TX1_GAINON_MEM    = 16,
		HHHHHHHHHH        = 17,
		RX1_EN_MEM        = 18,
		RX1_EN_DELAY      = 19,
	};

	enum class RdReg : uint32_t {
		PLAY_CTRL             = 0,
		PLAY_CTRL_EFFECTIVE   = 1,
		TX0_DELAY             = 2,
		TX0_DELAY_EFFECTIVE   = 3,
		RX0_DELAY             = 4,
		RX0_DELAY_EFFECTIVE   = 5,
		RX_EN_DELAY           = 6,
		RX_EN_DELAY_EFFECTIVE = 7,
		RESET_TIME            = 8,
		RESET_TIME_EFFECTIVE  = 9,
		PPS_STATUS            = 10,
		CAT_SYNC_WIDTH        = 11,
		CAT_SYNC_STATUS       = 12,
		TX0_GAIN              = 13,
		TX0_GAIN_EFFECTIVE    = 14,
		TX1_GAIN              = 15,
		TX1_GAIN_EFFECTIVE    = 16,
		TX1_DELAY             = 17,
		TX1_DELAY_EFFECTIVE   = 18,
		RX1_DELAY             = 19,
		RX1_DELAY_EFFECTIVE   = 20
	};

	static constexpr uint32_t reg_addr(WrReg reg) { return static_cast<uint32_t>(reg) << 2; }
	static constexpr uint32_t reg_addr(RdReg reg) { return static_cast<uint32_t>(reg) << 3; }

	struct PlayCtrl {
		uint32_t enable            : 1;
		uint32_t trigger_src       : 1;
		uint32_t gain_on_enable    : 1;
		uint32_t rx_enable         : 1;
		uint32_t GPIO_TX_ENABLE    : 2;
		uint32_t GPIO_RX_ENABLE    : 2;
		uint32_t start_pulse_enable: 1;
		uint32_t cat_sync_enable   : 1;
		uint32_t start_pulse_40m   : 1;
		uint32_t rx_insert_count   : 1;
		uint32_t                   : 20;
	};

	struct TxDelay {
		uint32_t del_dac: 16;
		uint32_t del_pps: 16;
	};

	uhd::usrp::multi_usrp::sptr usrp_;

	PlayCtrl play_ctrl_{};
	TxDelay tx_delay_{};

	void write_reg(WrReg reg, uint32_t value);
	uint64_t read_reg(RdReg reg);

	void write_play_ctrl();
	void write_tx_delay();

	static uint32_t to_raw(const PlayCtrl & ctrl);
	static uint32_t to_raw(const TxDelay & delay);
};
