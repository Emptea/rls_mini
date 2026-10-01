#include <cstdint>
#include <uhd/usrp/multi_usrp.hpp>

#define SR_CORE_WR_PLAY_CTRL_ADDR 0x00
#define SR_CORE_RD_PLAY_CTRL_ADDR 0x00
#define SR_CORE_PLAY_CTRL_WIDTH   2

typedef struct {
	uint32_t enable        : 1;
	uint32_t trigger_src   : 1;
	uint32_t gain_on_enable: 1;
	uint32_t rx_enable     : 1;
	uint32_t GPIO_TX_ENABLE: 2;
	uint32_t GPIO_RX_ENABLE: 2;
	uint32_t start_pulse_enable: 1;
	uint32_t cat_sync_enable   : 1;
	uint32_t start_pulse_40m   : 1;
	uint32_t                   : 21; // reserved
} sr_core_play_ctrl;

#define SR_CORE_WR_TX_DELAY_ADDR 0x10
#define SR_CORE_RD_TX_DELAY_ADDR 0x10
#define SR_CORE_TX_DELAY_WIDTH   32

typedef struct {
	uint32_t del_dac: 16;
	uint32_t del_pps: 16; // reserved
} sr_core_tx_delay;

#define SR_CORE_WR_RX_DELAY_ADDR 0x14
#define SR_CORE_RD_RX_DELAY_ADDR 0x18
#define SR_CORE_RX_DELAY_WIDTH   16

typedef struct {
	uint32_t del_adc: 16;
	uint32_t        : 16; // reserved
} sr_core_rx_delay;

#define SR_CORE_WR_RX_GPIO_DELAY_ADDR 0x18
#define SR_CORE_RD_RX_GPIO_DELAY_ADDR 0x30
#define SR_CORE_RX__GPIO_DELAY_WIDTH  16

typedef struct {
	uint32_t del_rx_gpio: 16;
	uint32_t            : 16; // reserved
} sr_core_rx_gpio_delay;

void set_sr_core_play(uhd::usrp::multi_usrp::sptr usrp, uint32_t enable, uint32_t trigger_src, uint32_t gain_on_enable, uint32_t rx_enable);
void set_sr_core_tx_delay(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_dac, uint32_t del_pps);
void set_sr_core_rx_delay(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_adc);

void set_sr_core_play_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t enable);
void set_sr_core_play_trigger_src(uhd::usrp::multi_usrp::sptr usrp, uint32_t trigger_src);
void set_sr_core_play_gain_on_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gain_on_enable);
void set_sr_core_play_rx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t rx_enable);
void set_sr_core_tx_delay_del_dac(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_dac);
void set_sr_core_tx_delay_del_pps(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_pps);
void set_sr_core_rx_delay(uhd::usrp::multi_usrp::sptr usrp, uint32_t del_adc);
void set_sr_core_play_gpio_tx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gpio_enable);
void set_sr_core_play_gpio_rx_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t gpio_enable);
void set_sr_core_play_start_pulse_enable(uhd::usrp::multi_usrp::sptr usrp, uint32_t start_pulse_enable);
void set_sr_core_play_start_pulse_40m(uhd::usrp::multi_usrp::sptr usrp, uint32_t start_pulse_40m);

void set_sr_core_play_start_tx(uhd::usrp::multi_usrp::sptr usrp);
void set_sr_core_play_stop_tx(uhd::usrp::multi_usrp::sptr usrp);