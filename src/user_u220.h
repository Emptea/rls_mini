#pragma once

#include <array>
#include <complex>
#include <cstddef>
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

	void enable_ad9361_stage1();
	void enable_ad9361_stage2();
	void enable_ad9361_master();
	void enable_ad9361_finish();

	void set_tx_dac_sg(size_t channel, const std::array<std::complex<short>, 232 * 4> & samples);
	void set_tx_gainon_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples);
	void set_rx_en_sg(size_t channel, const std::array<uint8_t, 232 * 4> & samples);
	void set_tx_complex_gain(size_t channel, const std::complex<short> gain);


	// radio: 0..1; playback: 0..3. All accesses use radio 0's user interface.
	enum class RxPlaybackReg : uint32_t {
		ENABLE = 0, MEM_PTR = 1, CORRECTION_RE = 2, CORRECTION_IM = 3,
		DELAY = 8, PHASE = 12, AMPLITUDE = 16, SAMPLE_MEM = 20,
		AMPLITUDE2_MEM = 24, AMPLITUDE2_OFFSET = 28,
	};
	struct RxPlaybackStatus {
		bool waiting;
		bool playing;
		uint16_t amplitude2_counter;
	};

	// For per-playback registers, add playback to the enum's offset.
	void write_rx_playback_reg(size_t radio, uint32_t offset, uint32_t value);
	// Memory contents cannot be read back; SAMPLE_MEM slots return status.
	uint64_t read_rx_playback_reg(size_t radio, uint32_t offset);
	void set_rx_playback_enable(size_t radio, uint8_t mask);
	void set_rx_playback_correction(size_t radio, std::complex<int32_t> correction);
	void set_rx_playback_delay(size_t radio, size_t playback, uint16_t distance_m);
	void set_rx_playback_phase(size_t radio, size_t playback, uint16_t distance_m,
	                           size_t channel, uint16_t angle, int16_t velocity_mps);
	// Positive attenuation reduces RCS: 0 dB = 1.0, 10 dB = 0.1.
	static double rx_playback_rcs_from_attenuation(double attenuation_db);
	// distance_m must be nonzero; channel is the n_chan in the gain formula.
	void set_rx_playback_amplitude(size_t radio, size_t playback, uint16_t distance_m,
	                               size_t channel, uint16_t angle, double attenuation_db = 0.0);
	void set_rx_playback_samples(size_t radio, size_t playback,
	                             const std::array<std::complex<int16_t>, 928> & samples);
	void set_rx_playback_amplitude2(size_t radio, size_t playback,
	                                const std::array<int16_t, 4096> & samples);
	// angle: 65536 units per revolution; counter start: degrees, wrapped at 360.
	void set_rx_playback_amplitude2_offset(size_t radio, size_t playback, uint16_t angle);
	void set_rx_playback_amplitude2_counter_start(double angle_degrees);
	uint16_t get_rx_playback_amplitude2_counter_start();
	RxPlaybackStatus get_rx_playback_status(size_t radio, size_t playback);

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
		FPGA_VERSION              = 0,
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
		uint32_t rclk_lock_off     : 1;
		uint32_t cat_enable_val    : 1;
		uint32_t cat_enable_we     : 1;
		uint32_t cat_edge          : 1;
		uint32_t                   : 16;
	};

	uhd::usrp::multi_usrp::sptr usrp_;

	PlayCtrl play_ctrl_{};

	void write_reg(WrReg reg, uint32_t value);
	uint64_t read_reg(RdReg reg);

	void write_play_ctrl();
	void write_tx_delay();


	static uint32_t rx_playback_offset(RxPlaybackReg reg, size_t playback);
	// Phase: 2^32 units/revolution; currently uses velocity only.
	// Amplitude: signed complex Q14; angle: 65536 units/revolution.
	static uint32_t convert_rx_playback_phase(uint16_t distance_m, size_t channel,
	                                          uint16_t angle, int16_t velocity_mps);
	static std::complex<int16_t> convert_rx_playback_amplitude(uint16_t distance_m, size_t channel,
	                                                               uint16_t angle, double attenuation_db);

	static uint32_t to_raw(const PlayCtrl & ctrl);
};
