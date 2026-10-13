#pragma once

#include <cstdint>
#include <memory>
#include <uhd/usrp/multi_usrp.hpp>

class UserU220 {
public:
	explicit UserU220(uhd::usrp::multi_usrp::sptr usrp);

	void set_play(uint32_t enable, uint32_t trigger_src, uint32_t gain_on_enable, uint32_t rx_enable);

	void set_tx_dac_delay(uint32_t delay_ch0, uint32_t delay_ch1);
	void set_tx_gainon_delay(uint32_t delay_ch0, uint32_t delay_ch1);
	void set_rx_adc_delay(uint32_t delay_ch0, uint32_t delay_ch1);
	void set_rx_en_delay(uint32_t delay_ch0, uint32_t delay_ch1);

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

	uint32_t get_fpga_version();


private:
	enum class WrReg : uint32_t {
		PLAY_CTRL         = 0,
		MEM_PTR           = 1,
		TX0_DAC_MEM       = 2,
		TX0_GAINON_MEM    = 3,
		TX_DAC_DELAY      = 4,
		RX_ADC_DELAY      = 5,
		RX_EN_DELAY       = 6,
		RX0_EN_MEM        = 7,
		RESET_TIME        = 8,
		CAT_SYNC_WIDTH    = 9,
		TX0_GAIN          = 10,
		TX1_GAIN          = 11,
		TX1_DAC_MEM       = 12,
		TX_GAINON_DELAY   = 13,
		RX_PLAYBACK_START = 14,
		TX1_GAINON_MEM    = 15,
		RX1_EN_MEM        = 16,
	};

	enum class RdReg : uint32_t {
		FPGA_VERSION                 = 0,
		PLAY_CTRL                 = 1,
		PLAY_CTRL_EFFECTIVE       = 2,
		TX_DAC_DELAY              = 3,
		TX_DAC_DELAY_EFFECTIVE    = 4,
		RX_ADC_DELAY              = 5,
		RX_ADC_EFFECTIVE          = 6,
		RX_EN_DELAY               = 7,
		RX_EN_DELAY_EFFECTIVE     = 8,
		RESET_TIME                = 9,
		RESET_TIME_EFFECTIVE      = 10,
		PPS_STATUS                = 11,
		CAT_SYNC_WIDTH            = 12,
		CAT_SYNC_STATUS           = 13,
		TX0_GAIN                  = 14,
		TX0_GAIN_EFFECTIVE        = 15,
		TX1_GAIN                  = 16,
		TX1_GAIN_EFFECTIVE        = 17,
		TX_GAINON_DELAY           = 18,
		TX_GAINON_DELAY_EFFECTIVE = 19,
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

	uhd::usrp::multi_usrp::sptr usrp_;

	PlayCtrl play_ctrl_{};

	void write_reg(WrReg reg, uint32_t value);
	uint64_t read_reg(RdReg reg);

	void write_play_ctrl();
	void write_tx_delay();

	static uint32_t to_raw(const PlayCtrl & ctrl);
};
