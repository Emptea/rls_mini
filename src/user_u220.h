#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <uhd/usrp/multi_usrp.hpp>
class UserRegLogger;
class UserU220 {
public:
	explicit UserU220(uhd::usrp::multi_usrp::sptr usrp);

	void set_reg_logger(UserRegLogger * logger, size_t board_index) {
		reg_logger_  = logger;
		board_index_ = board_index;
	}

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


	void apply_rx_playback();
	void pulse_rx_playback_apply();
	void set_rx_playback_enable(size_t radio, uint8_t mask);
	void set_rx_playback_correction(size_t radio, std::complex<int32_t> correction);
	void set_rx_playback_delay(size_t radio, const std::array<uint32_t, 4> & values);
	void set_rx_playback_phase(size_t radio, const std::array<uint32_t, 4> & values);
	void set_rx_playback_amplitude(size_t radio, const std::array<uint32_t, 4> & values);
	void set_rx_playback_samples(size_t radio, size_t playback, const std::array<std::complex<int16_t>, 928> & samples);
	void set_rx_playback_dn(size_t radio, size_t playback, const std::array<int16_t, 4096> & samples);
	void set_rx_playback_dn_offset(size_t radio, const std::array<uint32_t, 4> & values);
	void set_rx_playback_dn_counter(uint16_t start, int32_t step);

	// Чтение состояния
	uint16_t get_rx_playback_dn_counter_start();

	struct RxPlaybackStatus {
		int16_t sin         = 0;     // [63:48] Синус поворота
		int16_t cos         = 0;     // [47:32] Косинус поворота
		uint32_t dn_counter = 0;     // [21:2] Amplitude2 counter, ЦМР 1/256 записи
		bool playing        = false; // [1] Воспроизведение
		bool waiting        = false; // [0] Ожидание задержки

		double getDnDeg() const { return dn_counter * 360.0 / (1 << 20); }
		double getSin() const { return sin / 32768.0; }
		double getCos() const { return cos / 32768.0; }
	};

	RxPlaybackStatus get_rx_playback_status(size_t radio, size_t playback);

	uint32_t get_fpga_version();


private:
	enum class WrReg : uint32_t {
		PLAY_CTRL               = 0,
		MEM_PTR                 = 1,
		TX0_DAC_MEM             = 2,
		TX0_GAINON_MEM          = 3,
		TX_DAC_DELAY            = 4,
		RX_ADC_DELAY            = 5,
		RX_EN_DELAY             = 6,
		RX0_EN_MEM              = 7,
		RESET_TIME              = 8,
		CAT_SYNC_WIDTH          = 9,
		TX0_GAIN                = 10,
		TX1_GAIN                = 11,
		TX1_DAC_MEM             = 12,
		TX_GAINON_DELAY         = 13,
		TX1_GAINON_MEM          = 15,
		RX1_EN_MEM              = 16,

		RX_PLAYBACK_START       = 14,
		RX_PLAYBACK_APPLY       = 18,
		RX_PLAYBACK_APPLY_PULSE = 19,

		RX0_PLAYBACK_BASE       = 32,
		RX1_PLAYBACK_BASE       = 64,
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

		RX_PLAYBACK_START         = 22,
		RX0_PLAYBACK_BASE         = 32,
		RX1_PLAYBACK_BASE         = 64,
	};

	enum class RxPlaybackReg : uint32_t {
		ENABLE            = 0,
		MEM_PTR           = 1,
		CORRECTION_RE     = 2,
		CORRECTION_IM     = 3,
		DELAY             = 8,
		PHASE             = 12,
		AMPLITUDE         = 16,
		SAMPLE_MEM        = 20,
		AMPLITUDE2_MEM    = 24,
		AMPLITUDE2_OFFSET = 28,
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
	UserRegLogger * reg_logger_ = nullptr;
	size_t board_index_         = 0;
	PlayCtrl play_ctrl_{};

	void write_reg(WrReg reg, uint32_t value, bool log = true);
	void write_rx_playback_reg(size_t channel, RxPlaybackReg reg, uint32_t value, bool log = true);
	uint64_t read_reg(RdReg reg, bool log = true);
	uint64_t read_rx_playback_reg(size_t channel, RxPlaybackReg reg, bool log = true);


	void write_play_ctrl();
	void write_tx_delay();


	static uint32_t to_raw(const PlayCtrl & ctrl);
};
