# Register map

Three register spaces are reachable over USB. Each is an 8-bit address on a
settings bus, written as a 32-bit value; readback is 64 bits, selected by index.

| Space | FPGA bus | Reached from the host through | Clock |
|---|---|---|---|
| Core | `b200_core`, control SID 0x40 | UHD driver internals only (`_local_ctrl`) | `bus_clk` |
| Radio 0 / 1 | `radio_legacy`, control SID 0x10 / 0x20 | UHD driver internals (`_radio_perifs[i].ctrl`) | `radio_clk` |
| User (radio 0) | `b200_core`, via radio 0's `user_settings` | **public API**: `multi_usrp::get_user_settings_iface(0)` | `radio_clk` |

Everything added for memory playback is in the **user** space, so it works with
an unmodified UHD. The core and radio tables are for reference and debugging.

## Address conventions

"Local" is the register number the FPGA decodes (`set_addr`). What you pass on
the host side depends on the interface:

| Interface | Write | Read |
|---|---|---|
| UHD internal (`poke32`/`peek64` on `_local_ctrl` or a radio ctrl) | `poke32(TOREG(local), v)`, `TOREG(x) = 4 * x` | `peek64(8 * slot)`: writes `slot` to readback register 32, returns the 64-bit readback |
| User settings iface | `regs->poke32(4 * local, v)` | `regs->peek64(8 * slot)` |

Under the hood a user `poke32` is two radio 0 writes (user address to 253, data
to 254). A user `peek64` writes the slot to radio register 255 and reads radio
readback slot 7.

The byte-offset conventions above are from stock UHD (`radio_ctrl_core_3000`,
`user_settings_core_3000`). If the MicroPhase fork behaves differently,
`regs->poke32(0, 3)` followed by `regs->peek64(0)` returning 3 confirms which
convention is in use.

---

## User registers (radio 0) - memory playback

Open the device with the `enable_user_regs` device argument.

### Writes

| Local | UHD `poke32` offset | Bits | Name | Reset |
|---|---|---|---|---|
| 0 | 0 | [0] | `enable_playback` - runs the pulse counter, arms playback, both TX channels owned by the player (0 between bursts) | 0 |
| | | [1] | `playback_source` - trigger: 0 = `PPS_IN_EXT`, 1 = internal start pulse | 0 |
| | | [2] | `amp_enable_on_playback` - hold `tx_amp_en1/2` on while playback is enabled | 0 |
| | | [3] | `rx_start_on_trigger` - RX samples held until the first `adc_start` after the stream opens | 0 |
| | | [4] | `PLAY_GPIO_P` enable - AND mask on the pin; the GPIO memory keeps playing either way | 1 |
| | | [5] | `PLAY_GPIO_N` enable | 1 |
| | | [6] | `RX_ENABLE_P` enable - AND mask on the pin; the rx_enable memory keeps playing either way | 1 |
| | | [7] | `RX_ENABLE_N` enable | 1 |
| 1 | 4 | [9:0] | memory write pointer, shared by all three memories (width = clog2(`MEM_LEN`)) | 0 |
| 2 | 8 | [31:0] | DAC memory[pointer] = `{I[15:0], Q[15:0]}`, then pointer + 1 | init file |
| 3 | 12 | [0] | GPIO memory[pointer] = bit, then pointer + 1 | init file |
| 4 | 16 | [15:0] | DAC playback delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| | | [31:16] | GPIO playback delay after the trigger, 0 = none | 0 |
| 5 | 20 | [15:0] | ADC capture start delay after the trigger, 0 = none | 0 |
| 6 | 24 | [15:0] | rx_enable playback delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| 7 | 28 | [0] | rx_enable memory[pointer] = bit, then pointer + 1 | init file |

Every write to register 0 sets all eight bits. Keep [7:4] set (`0xF0`) unless
you mean to switch pins off; the old `0xD` would now silence all four 1-bit
memory pins.

A cleared pin enable only forces that pin to 0 at the output register (no
added latency, no glitch). The memory behind it keeps playing, so setting the
bit again mid-burst resumes at the entry being played.

### Readback

| Slot | UHD `peek64` offset | Contents |
|---|---|---|
| 0 | 0 | register 0 as written: `[7] RX_ENABLE_N en, [6] RX_ENABLE_P en, [5] PLAY_GPIO_N en, [4] PLAY_GPIO_P en, [3] rx_start_on_trigger, [2] amp, [1] source, [0] enable` |
| 1 | 8 | in effect (after VIO): `[10:7] pin enables (same order as register 0 [7:4]), [6] pin enable override, [5] rx capture override, [4] rx_start_on_trigger, [3] amp, [2] playback override, [1] source, [0] enable` |
| 2 | 16 | register 4 as written: `[31:16] gpio_delay, [15:0] dac_delay` |
| 3 | 24 | in effect: `[32] delay override, [31:16] gpio_delay, [15:0] dac_delay` |
| 4 | 32 | register 5 as written: `[15:0] adc_delay` |
| 5 | 40 | in effect: `[16] delay override, [15:0] adc_delay` |
| 6 | 48 | register 6 as written: `[15:0] rx_enable_delay` |
| 7 | 56 | in effect: `[16] delay override, [15:0] rx_enable_delay` |

### Output pins

All LVCMOS33, bank 16. Each P/N pair carries the same single-ended signal on
both pins, unless a pin enable masks one of them.

| Pins | Package pins | Signal | Enable bits (register 0) |
|---|---|---|---|
| `START_PULSE_P` / `_N` | B20 / A20 (B16_L16) | start pulse, looped back to `PPS_IN_EXT` | - |
| `PLAY_GPIO_P` / `_N` | A18 / A19 (B16_L17) | GPIO memory | [4] / [5] |
| `RX_ENABLE_P` / `_N` | F19 / F20 (B16_L18) | rx_enable memory | [6] / [7] |

### Build-time constants (`antsdr_u220/top/tx_mem_player.v`)

| Localparam | Value | Meaning |
|---|---|---|
| `PERIOD` | 928 | start pulse period, `radio_clk` cycles |
| `PULSE_LEN` | 8 | start pulse width, `radio_clk` cycles |
| `MEM_LEN` | 928 | entries per burst, depth of all three memories, lines per init file |
| `DAC_INIT_FILE` / `GPIO_INIT_FILE` / `RX_ENABLE_INIT_FILE` (parameters) | `tx_dac_mem.mem` / `tx_gpio_mem.mem` / `tx_rx_enable_mem.mem` | `$readmemh` contents at configuration (below) |

### Init file contents (`antsdr_u220/top/`)

What the memories hold after configuration, until the host rewrites them.
Entry n plays n `radio_clk` cycles after that memory starts (trigger + its own
delay), so with equal GPIO and rx_enable delays the two pins match.

| Memory | File | Contents |
|---|---|---|
| DAC | `tx_dac_mem.mem` | two LFM chirps: entries 0..159 (160) and 564..587 (24), zero elsewhere |
| GPIO | `tx_gpio_mem.mem` | 1 at entries 0..167 and 564..595: each chirp plus 8 entries, 0 elsewhere |
| rx_enable | `tx_rx_enable_mem.mem` | identical to the GPIO file |

Memory writes and all user registers are on `radio_clk`: they only work while
the AD9361 is running (i.e. after UHD has opened the device).

### Examples

```cpp
#include <uhd/usrp/multi_usrp.hpp>

auto usrp = uhd::usrp::multi_usrp::make("enable_user_regs");
auto regs = usrp->get_user_settings_iface(0);        // radio 0

const uint32_t PINS_ALL = 0xF0;                        // [7:4]: all four 1-bit memory pins on

// Load all three memories (playback disabled first, pins left enabled)
regs->poke32(0, PINS_ALL);
regs->poke32(4, 0);                                   // pointer = 0
for (uint32_t w : dac_words)  regs->poke32(8, w);     // {I, Q}, top 12 bits of each used
regs->poke32(4, 0);
for (uint32_t b : gpio_bits)  regs->poke32(12, b & 1);
regs->poke32(4, 0);
for (uint32_t b : rx_enable_bits) regs->poke32(28, b & 1);

// Delays: DAC 5 cycles, GPIO 17 cycles, ADC capture 9 cycles, rx_enable 23 cycles
regs->poke32(16, (17u << 16) | 5u);
regs->poke32(20, 9u);
regs->poke32(24, 23u);

// Enable: trigger from PPS_IN_EXT, PAs held on, RX gated to the trigger
regs->poke32(0, PINS_ALL | (1u << 0) | (0u << 1) | (1u << 2) | (1u << 3));   // 0xFD

// Switch RX_ENABLE_N off; the rx_enable memory keeps playing on RX_ENABLE_P
regs->poke32(0, 0xFD & ~(1u << 7));                   // 0x7D

// Read back
uint64_t ctrl  = regs->peek64(0);    // as written, incl. pin enables [7:4]
uint64_t state = regs->peek64(8);    // in effect
bool playing_enabled = state & 1;
bool vio_overrides   = (state >> 2) & 1;
uint8_t pins_in_effect = (state >> 7) & 0xF;          // after the pin enable VIO override
uint64_t delays = regs->peek64(24);                   // in effect, slot 3
uint16_t dac_delay  = delays & 0xFFFF;
uint16_t gpio_delay = (delays >> 16) & 0xFFFF;
uint16_t rx_enable_delay = regs->peek64(56) & 0xFFFF; // in effect, slot 7

// RX starting on the trigger: plain "start now" command, generous timeout
uhd::stream_args_t sa("sc16");
auto rx = usrp->get_rx_stream(sa);
uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
cmd.stream_now = true;
rx->issue_stream_cmd(cmd);
// rx->recv(..., timeout > pulse period) - nothing arrives until adc_start
```

Order: write register 0 only after UHD has finished opening the device;
otherwise its codec loopback self-test sees playback data and fails.

---

## VIO `u_vio_play` (b200_core, `bus_clk`)

Four independent overrides. While an override is set, its VIO values replace
the register values (host writes still land, they have no effect).

| Probe | Width | Meaning |
|---|---|---|
| `probe_out0` | 1 | override playback bits (`probe_out1/2/6`) |
| `probe_out1` | 1 | `enable_playback` |
| `probe_out2` | 1 | `playback_source` |
| `probe_out6` | 1 | `amp_enable_on_playback` |
| `probe_out3` | 1 | override delays (`probe_out4/5/8/10`) |
| `probe_out4` | 16 | DAC delay |
| `probe_out5` | 16 | GPIO delay |
| `probe_out8` | 16 | ADC delay |
| `probe_out10` | 16 | rx_enable delay |
| `probe_out9` | 1 | override ADC capture (`probe_out7`) |
| `probe_out7` | 1 | `rx_start_on_trigger` |
| `probe_out11` | 1 | override pin enables (`probe_out12`) |
| `probe_out12` | 4 | pin enables, `[0] PLAY_GPIO_P, [1] PLAY_GPIO_N, [2] RX_ENABLE_P, [3] RX_ENABLE_N`; starts at `0xF` |
| `probe_in0` / `probe_in2` | 1 | `enable_playback`: register / in effect |
| `probe_in1` / `probe_in3` | 1 | `playback_source`: register / in effect |
| `probe_in8` / `probe_in9` | 1 | `amp_enable_on_playback`: register / in effect |
| `probe_in10` / `probe_in11` | 1 | `rx_start_on_trigger`: register / in effect |
| `probe_in4` / `probe_in6` | 16 | DAC delay: register / in effect |
| `probe_in5` / `probe_in7` | 16 | GPIO delay: register / in effect |
| `probe_in12` / `probe_in13` | 16 | ADC delay: register / in effect |
| `probe_in14` / `probe_in15` | 16 | rx_enable delay: register / in effect |
| `probe_in16` / `probe_in17` | 4 | pin enables: register 0 [7:4] / in effect |

Set the values first, then flip `probe_out0` (the playback bits are not
filtered against partial arrival; the delays and the pin enables are, so pins
changed together switch on the same cycle).

---

## Core registers (SID 0x40, `b200_core`)

Written by the UHD driver; not reachable from an application without patching
UHD (`_local_ctrl->poke32(TOREG(local), v)`).

| Local | UHD `poke32` offset | Name | Contents |
|---|---|---|---|
| 8, 9, 10 | 32, 36, 40 | `SR_CORE_SPI` | AD9361 SPI (`simple_spi_core`: divider, control, data) |
| 16 | 64 | `SR_CORE_MISC` | `[8] swap_atr_n, [7] tx_bandsel_a, [6] tx_bandsel_b, [5] rx_bandsel_a, [4] rx_bandsel_b, [3] rx_bandsel_c, [2] codec_arst, [1] mimo, [0] ref_sel` (UHD rewrites the whole word) |
| 24 | 96 | `SR_CORE_COMPAT` | not decoded |
| 32 | 128 | `SR_CORE_READBACK` | core readback slot select [1:0] |
| 40 | 160 | `SR_CORE_GPSDO_ST` | `[7:0]` GPSDO status |
| 48 | 192 | `SR_CORE_SYNC` | `[2] time_sync, [1:0] pps_select` (00 GPS, 01 external, 10 internal, 11 none) |

| Slot | UHD `peek64` offset | Contents |
|---|---|---|
| 0 | 0 | `{32'hACE0BA5E, COMPAT_MAJOR 0x0010, COMPAT_MINOR 0x0000}` |
| 1 | 8 | `[31:0]` SPI readback |
| 2 | 16 | `{16'b0, radio_st, gpsdo_st, rb_misc}`, `rb_misc[0] = ext_ref_locked` |
| 3 | 24 | `[1:0]` AD9361 lock signals |

---

## Radio registers (SID 0x10 radio 0, 0x20 radio 1, `radio_legacy`)

Written by the UHD driver per channel (`_radio_perifs[i].ctrl`). Listed so
ILA captures of `radio_0/set_addr` can be read.

| Local | UHD `poke32` offset | Name | Contents |
|---|---|---|---|
| 6 | 24 | `SR_LOOPBACK` | `[0]` digital TX->RX loopback |
| 12..17 | 48..68 | `SR_ATR` | front-end ATR: idle, RX, TX, full duplex, DDR, disable |
| 21 | 84 | `SR_TEST` | test register (readback slot 0) |
| 22 | 88 | `SR_CODEC_IDLE` | TX sample sent while not streaming (UHD loopback self-test) |
| 32 | 128 | `SR_READBACK` | radio readback slot select [2:0] |
| 64.. | 256.. | `SR_TX_CTRL` | TX control (`new_tx_control`), TX responder at 66.. |
| 96..99 | 384..396 | `SR_RX_CTRL` | RX stream command, time high, time low (queues the command), halt |
| 100.. | 400.. | `SR_RX_CTRL+4` | RX framer (101 = RX stream ID) |
| 128..130 | 512..520 | `SR_TIME` | time high, time low, time control |
| 136 / 138 | 544 / 552 | `SR_RX_FMT` / `SR_TX_FMT` | sample format conversion |
| 144.. | 576.. | `SR_RX_DSP` | DDC |
| 184.. | 736.. | `SR_TX_DSP` | DUC |
| 200..206 | 800..824 | `SR_FP_GPIO` | front-panel GPIO ATR (radio 0 only) |
| 253 / 254 | 1012 / 1016 | `SR_USER_SR_BASE` | user register address / data (radio 0) |
| 255 | 1020 | `SR_USER_RB_ADDR` | user readback slot select (radio 0) |

| Slot | UHD `peek64` offset | Contents |
|---|---|---|
| 0 | 0 | `[31:0]` `SR_TEST` value |
| 1 | 8 | `vita_time` |
| 2 | 16 | `vita_time` at last PPS |
| 3 | 24 | `{tx, rx}` current samples (before the playback mux) |
| 4 | 32 | `[9:0]` front-panel GPIO readback |
| 5 | 40 | `{rx_flow_ctrl_busy, ibs_state[3:0]}` |
| 6 | 48 | unused (0) |
| 7 | 56 | user readback (radio 0 only; see above) |
