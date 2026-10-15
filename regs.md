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
`regs->poke32(0, 3)` followed by `regs->peek64(8)` (slot 1, register 0 as
written) returning 3 confirms which convention is in use. `regs->peek64(0)`
returns the build version.

---

## User registers (radio 0) - memory playback

Open the device with the `enable_user_regs` device argument.

### Writes

| Local | UHD `poke32` offset | Bits | Name | Reset |
|---|---|---|---|---|
| 0 | 0 | [0] | `enable_playback` - runs the pulse counter, arms playback, both TX channels owned by the player (0 between bursts) | 0 |
| | | [1] | `playback_source` - trigger: 0 = `PPS_IN_EXT`, 1 = internal start pulse | 0 |
| | | [2] | `amp_enable_on_playback` - hold `tx_amp_en1/2` on while playback is enabled | 0 |
| | | [3] | `rx_start_on_trigger` - each radio's RX samples held until its first ADC capture start (register 5 [15:0] / [31:16]) after the stream opens | 0 |
| | | [4] | `PLAY_EXTERNAL_TX_ENABLE_P` enable - AND mask on the pin; its memory keeps playing either way | 1 |
| | | [5] | `PLAY_EXTERNAL_TX_ENABLE_N` enable | 1 |
| | | [6] | `PLAY_EXTERNAL_RX_ENABLE_P` enable | 1 |
| | | [7] | `PLAY_EXTERNAL_RX_ENABLE_N` enable | 1 |
| | | [8] | `start_pulse_enable` - drive the start pulse on `START_PULSE_P/N`; when 0 the pins stay low but the counter and the internal trigger keep running | 1 |
| | | [9] | `cat_sync_enable` - one `CAT_SYNC` (AD9361 SYNC_IN) pulse, register 9 cycles long, on the first rising edge of a `PPS_IN_EXT` sequence (armed when this bit goes on, and after a silence longer than register 8's dead zone), copied to `SYNC_OUT_P/N` | 0 |
| | | [10] | `start_pulse_40m` - master clocking: the start pulse leaves on the falling edge of the 40 MHz clock instead of from `radio_clk`, so every board's 40 MHz sampling of it lands on the same rising edge. Change it with the start pulse off | 0 |
| | | [11] | `rx_insert_count` - RX chunk counter: the RX stream (both radios) is cut into chunks of 232 samples, each led by the delimiter 0x01234567 and a 32-bit chunk counter; packets hold whole chunks; see below | 0 |
| | | [12] | `rclk_lock_off` - stop the `radio_clk` phase lock: with 0 (the default) `radio_clk` follows the AD9361's RX_FRAME in 2-channel mode, so it lines up across boards after a multi-chip sync; see below | 0 |
| | | [14:13] | `cat_en_ctrl` [1:0] - AD9361 ENABLE (`CAT_EN`) on every `PPS_IN_EXT` rising edge: `10` drives it low, `11` high, `00` / `01` leave it as it is (high from power-up); see below | 0 |
| | | [15] | `cat_en_ctrl` [2] - the 40 MHz clock edge `CAT_EN` and `ENABLE_OUT` change on: 0 rising, 1 falling (half a cycle, 12.5 ns, later); see below | 0 |
| 1 | 4 | [9:0] | memory write pointer, shared by all six memories (width = clog2(`MEM_LEN`)) | 0 |
| 2 | 8 | [31:0] | radio 0 (`tx0`) DAC memory[pointer] = `{I[15:0], Q[15:0]}`, then pointer + 1 | init file |
| 3 | 12 | [0] | `PLAY_EXTERNAL_TX_ENABLE_P` memory[pointer] = bit, then pointer + 1 | init file |
| 4 | 16 | [15:0] | radio 0 (`tx0`) DAC playback delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| | | [31:16] | radio 1 (`tx1`) DAC playback delay after the trigger, 0 = none | 0 |
| 5 | 20 | [15:0] | radio 0 ADC capture start delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| | | [31:16] | radio 1 ADC capture start delay after the trigger, 0 = none | 0 |
| 6 | 24 | [15:0] | `PLAY_EXTERNAL_RX_ENABLE_P` playback delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| | | [31:16] | `PLAY_EXTERNAL_RX_ENABLE_N` playback delay after the trigger, 0 = none | 0 |
| 7 | 28 | [0] | `PLAY_EXTERNAL_RX_ENABLE_P` memory[pointer] = bit, then pointer + 1 | init file |
| 8 | 32 | [0] | `pps_time_reset` - zero the command timer (`vita_time`) on the first rising edge of a `PPS_IN_EXT` sequence; see below | 0 |
| | | [31:16] | dead zone, ms: a silence on `PPS_IN_EXT` longer than this re-arms "first edge", for the timer reset and for `CAT_SYNC` | 1500 |
| 9 | 36 | [15:0] | `CAT_SYNC` pulse length, `CLK_40MHz_FPGA` cycles, 0 = no pulse | 10 |
| 10 | 40 | [15:0] | radio 0 (`tx0`) playback gain, real part: two's complement, `0x2000` = 1.0; see below | `0x2000` |
| | | [31:16] | radio 0 playback gain, imaginary part | 0 |
| 11 | 44 | [15:0] | radio 1 (`tx1`) playback gain, real part | `0x2000` |
| | | [31:16] | radio 1 playback gain, imaginary part | 0 |
| 12 | 48 | [31:0] | radio 1 (`tx1`) DAC memory[pointer] = `{I[15:0], Q[15:0]}`, then pointer + 1 | init file |
| 13 | 52 | [15:0] | `PLAY_EXTERNAL_TX_ENABLE_P` playback delay after the trigger, `radio_clk` cycles, 0 = none | 0 |
| | | [31:16] | `PLAY_EXTERNAL_TX_ENABLE_N` playback delay after the trigger, 0 = none | 0 |
| 14 | 56 | [11:0] | RX playback amplitude2 counter start, all eight playbacks (both radios): a write loads every counter with it; see [RX playback](#rx-playback-registers-32-63-and-64-95) | 0 |
| 15 | 60 | [0] | `PLAY_EXTERNAL_TX_ENABLE_N` memory[pointer] = bit, then pointer + 1 | init file |
| 16 | 64 | [0] | `PLAY_EXTERNAL_RX_ENABLE_N` memory[pointer] = bit, then pointer + 1 | init file |
| 17 | 68 | [15:0] | RX_FRAME / TX_FRAME lock-in: `PPS_IN_EXT` edges to let pass after the first edge of a sequence; the lock-in is on the next one (0 = on the first edge); see [RX_FRAME / TX_FRAME lock-in](#rx_frame--tx_frame-lock-in-on-pps-register-17-readback-slots-23--24) | 0 |
| | | [16] | lock-in enable; going 1 also arms a new sequence | 1 |
| 32-63 | 128-252 | | RX playback, radio 0 (see [RX playback](#rx-playback-registers-32-63-and-64-95)) | |
| 64-95 | 256-380 | | RX playback, radio 1 | |

Each TX radio has its own DAC memory and DAC delay, and each RX radio its own
ADC capture start: radio 0 uses registers 2, 4 [15:0] and 5 [15:0], radio 1
uses 12, 4 [31:16] and 5 [31:16]. All of them sit in radio 0's user register
space for now (`get_user_settings_iface(0)`). One trigger starts everything;
the delays then place each radio's playback and capture independently.

The delays are paired by type, two per register in the same order, [15:0]
radio 0 / `_P` pin and [31:16] radio 1 / `_N` pin:

| Register | Delays |
|---|---|
| 4 | DAC playback |
| 5 | ADC capture start |
| 6 | `PLAY_EXTERNAL_RX_ENABLE_P` / `_N` |
| 13 | `PLAY_EXTERNAL_TX_ENABLE_P` / `_N` |

A write sets both halves, so write the two delays together. They read back
in slots 3 / 4, 5 / 6, 7 / 8 and 18 / 19 (as written / in effect), with the
same layout.

Each of the four external enable pins has its own 1-bit memory and its own
delay:

| Pin | Memory write | Delay |
|---|---|---|
| `PLAY_EXTERNAL_TX_ENABLE_P` | 3 | 13 [15:0] |
| `PLAY_EXTERNAL_TX_ENABLE_N` | 15 | 13 [31:16] |
| `PLAY_EXTERNAL_RX_ENABLE_P` | 7 | 6 [15:0] |
| `PLAY_EXTERNAL_RX_ENABLE_N` | 16 | 6 [31:16] |

The P-pin memories and the RX enable P delay keep the old `PLAY_GPIO` /
`RX_ENABLE` registers (3, 7 and 6 [15:0]). The N pins need their own writes:
their memories start from the same init file as their P pin, but their
delays start at 0. Both DAC memories start with the same init file, so with
equal delays and gains both channels play the same samples, as before.

Host code written for the previous layout (commit `ddedaa7`) must be
updated; these registers changed:

| Register | Previous | Now |
|---|---|---|
| 4 [31:16] | `PLAY_EXTERNAL_TX_ENABLE_P` delay | radio 1 DAC delay |
| 5 [31:16] | - | radio 1 ADC capture start delay |
| 6 [31:16] | - | `PLAY_EXTERNAL_RX_ENABLE_N` delay |
| 13 [15:0] | radio 1 DAC delay | `PLAY_EXTERNAL_TX_ENABLE_P` delay |
| 13 [31:16] | - | `PLAY_EXTERNAL_TX_ENABLE_N` delay |
| 14 | radio 1 ADC capture start delay | RX playback amplitude2 counter start |
| 15 | RX playback amplitude2 counter start | `PLAY_EXTERNAL_TX_ENABLE_N` memory |
| 16 | `PLAY_EXTERNAL_TX_ENABLE_N` memory | `PLAY_EXTERNAL_RX_ENABLE_N` memory |
| 17 | `PLAY_EXTERNAL_TX_ENABLE_N` delay | RX_FRAME / TX_FRAME lock-in on PPS |
| 18 | `PLAY_EXTERNAL_RX_ENABLE_N` memory | not used |
| 19 | `PLAY_EXTERNAL_RX_ENABLE_N` delay | not used |

The readback follows. In today's numbering (every slot below 32 has since
moved up by one, see [Readback](#readback)), slots 3 / 4, 5 / 6, 7 / 8 and
18 / 19 show registers 4, 5, 6 and 13 as above, with the in-effect override
flag at [32] in all four (at `ddedaa7` it was at [16] in slots 5, 7 and 18).
The counter start reads back in slot 22 (21 at `ddedaa7`). What `ddedaa7`
showed in slots 19 / 20 (register 14) and 22 to 25 (registers 17 and 19) is
now in the paired slots above.

Every write to register 0 sets all sixteen bits. Keep [8:4] set (`0x1F0`)
unless you mean to switch pins off; the old `0xD` would now silence the start
pulse and all four 1-bit memory pins. [12] is active high off, [14:13] = `00`
changes nothing and [15] = 0 is the rising edge, as before, so values written
for the older bits keep the `radio_clk` phase lock on and leave ENABLE alone.
A later write with [14:13] = `00` only stops the ENABLE updates; ENABLE keeps
its level.

`start_pulse_enable = 0` is the slave setting: the board still triggers from
`PPS_IN_EXT` (or itself, with `playback_source = 1`) without driving the shared
start line.

`CAT_SYNC` runs on the 40 MHz clock, not `radio_clk`, and gives **one pulse on
the first rising edge of a `PPS_IN_EXT` sequence**, decided like the command
timer reset (`top/pps_first_edge.v`): it arms when `cat_sync_enable` goes on,
and again after a silence longer than register 8's dead zone (shared, VIO
override included); the later edges of a running sequence give no pulse. The
pulse is register 9 cycles of `CLK_40MHz_FPGA` long (default 10, 0 = no pulse)
and starts 4-5 cycles after the edge (`PPS_IN_EXT` is asynchronous to the
clock). It is timed to `CLK_40MHz_FPGA` with 3 ns setup / 3 ns hold (slack in
`antsdr_u220/sync.rpt` after a build). Readback slot 13 counts the pulses
fired and shows `armed`. Bits [8] to [11] have no VIO override.

Multi-chip sync procedure. AD9361 registers are written on every board; the
steps are ADI's (`ad9361_mcs()` in the ADI Linux driver, wrapped in ALERT as
in libad9361's `ad9361_multichip_sync()`):

1. Set `start_pulse_40m` on the master, so the pulse leaves it half a 40 MHz
   cycle away from the sampling edges (checked in `sync.rpt`). Cable + input
   delay to the farthest board must stay under the rest of that half cycle
   (about 12 ns minus the FPGA path) for all boards, the master included via
   its loopback, to sample the same edge.
2. **ALERT:** write AD9361 0x014 = `0x01` (UHD's own way there, through the
   FDD flush state) and read 0x017 until [3:0] = `0x5`. UHD leaves the chip
   in FDD. A sync run in FDD can shift RX_FRAME against the RX data
   (EngineerZone threads 78775 / 105186, read from search excerpts only).
   The `radio_clk` lock follows RX_FRAME, so `RADIO_CLK_OUT` then comes out
   one `DCLK_OUT` period apart between boards from sync to sync, and RX
   channels 1 and 2 can swap.
3. **The steps:**

   | ADI step | AD9361 |
   |---|---|
   | 1 | 0x015 [6:5] = 0 (synthesizers powered); 0x001 = `0x0D` (BB, BBPLL, RF); 0x047 [7] = 1 (MCS refclk scale) |
   | 2 | one `CAT_SYNC` pulse: BBPLL sync |
   | 3 | 0x001 = `0x0B` (BB, digital clocks, RF) |
   | 4 | one `CAT_SYNC` pulse: digital clock sync |
   | 5 | 0x001 = `0x08` (MCS RF Enable only: the LO dividers stay enabled in ALERT) |

   For each pulse: set `cat_sync_enable` on every board (this arms them),
   start the master's start pulse, and check readback slot 13 on every board:
   the count went up by one. Re-arm for the second pulse by clearing and
   setting `cat_sync_enable` again (or by stopping the start pulse for longer
   than the dead zone), then start the pulse again. ADI's code waits 1 ms
   after every step.
4. **Back to FDD:** write 0x014 = `0x21` (UHD's value) and check 0x017 [3:0] =
   `0xA`.
5. Wait until readback slot 20 [0] is 1 on every board (the `radio_clk` lock
   has realigned to RX_FRAME), then reset the timers on PPS (register 8).

Extra pulses of the same sequence do not re-run the sync step any more, so how
long the start pulse keeps running does not matter. Sync after setting the
master clock rate: a rate change reprograms the BBPLL and undoes the sync.

### RX chunk counter (register 0 [11])

With `rx_insert_count` set, the RX stream is cut into **chunks** of
`CHUNK_SIZE` = 232 samples, each led by the delimiter `0x01234567`
(`CHUNK_DELIM`) and a 32-bit chunk counter (localparams in
`lib/vita_200/new_rx_framer.v`). A packet (a **frame**) holds whole chunks
only. Full layout in `frame.md`.

```
frame = [0x01234567][counter n][232 samples] ... [0x01234567][counter n+k-1][232 samples]
```

- **Count:** starts at 0 with the first chunk of a stream and goes up by one
  per chunk, across frames. It resets while the stream is not running, so
  with `rx_start_on_trigger` the first chunk after the radio's ADC capture
  start is 0. Each radio starts on its own capture start (register 5 [15:0]
  and [31:16]), so the two counters match only when the two ADC delays are
  equal; otherwise radio 1's chunk 0 starts (register 5 [31:16] - [15:0])
  cycles after radio 0's.
- **Chunks per frame:** as many as fit in the host's samples-per-packet
  (`spp`, counted in 32-bit words), at least one: k = max(1, spp / 234). Set
  `spp` to a multiple of 234 (234, 468, 702, ...) so frames fill it exactly.
  Do not go below 234: a frame is still one whole chunk, bigger than `spp`,
  and UHD's receive buffers can overflow.
- **Stream end:** the frame carrying `eob` ends wherever the stream ends, so
  its last chunk can be short.
- **On the host** the delimiter and the counter each arrive as one "sample"
  (`sc16`: I = high 16 bits, Q = low 16 bits), every 234 elements, the first at
  element 0 of the stream. Use `sc16` only: `sc8` and `fc32` conversion in the
  radio would mangle them.
- Change the bit only while no RX stream is running.

### Command timer reset on PPS (register 8)

The command timer is each radio's `vita_time`: timed stream commands, timed TX
bursts and `set_command_time` wait on it, and RX packets are stamped with it.
With `pps_time_reset` set, both radios' timers are zeroed on the same
`radio_clk` cycle, a fixed 3-4 cycles after the **first rising edge of a
`PPS_IN_EXT` sequence** (`top/pps_first_edge.v`). Boards sharing the line all
start counting from that edge, so timed commands for the same time line up.

- **First edge:** the scheme arms when it is enabled, and again after a silence
  on `PPS_IN_EXT` longer than the dead zone. Any rising edge disarms it; the
  edge that does so while armed zeroes the timers. Edges of a running sequence
  (closer together than the dead zone) leave the timers alone.
- **Dead zone:** milliseconds, timed from fixed clocks, so it does not depend
  on the master clock rate; `CAT_SYNC` uses the same value. A 1 PPS has gaps
  of exactly 1000 ms, so keep it clearly longer (default 1500); at 1000 it
  would re-arm on random seconds. `0xFFFF` never re-arms on silence, only on
  enabling.
- **Which PPS:** `PPS_IN_EXT` straight from the pin, the same line the
  playback trigger uses.
- **Internal PPS re-phased too:** the same event moves the rising edge of the
  internal 1 Hz PPS, the ppsloop's `lpps` (the timekeepers' PPS, so UHD's
  `get_time_last_pps()`, and `PPS_LED`), to the first edge, and restarts the
  bus-clock PPS generator (`pps_fpga_int`, the ppsloop's reference when
  `pps_select` = 10 and `ref_sel` = 1). With a shared reference clock the
  boards' internal PPS then stay aligned, so `set_time_next_pps()` lines up
  across boards as well.
  `lpps` rises a few tens of ns after the timers are zeroed and the
  timekeepers see it 2-3 `radio_clk` cycles later, so `get_time_last_pps()`
  reads a few ticks past whole seconds. Exception: while the ppsloop is
  locked to an external reference (fine-adjust state) `lpps` is left alone,
  since the reference already sets its phase. If that reference is
  `pps_fpga_int`, the loop drops lock at the jump and re-locks to the
  re-phased signal within a few seconds, re-centering the oscillator DAC on
  the way as on any reference loss.
- **Enabling mid-sequence** arms too: the next edge zeroes the timers even if
  pulses were already running. For several boards, enable on all of them and
  then start the sequence.
- **Host side:** a reset moves the time base under any timed command already
  queued, so schedule commands after it. Readback slot 11 shows `armed`, the
  number of resets done and the milliseconds since the last edge; poll it to
  know the reset happened. A host `set_time_*` still works, and the next first
  edge overwrites it.

A cleared pin enable only forces that pin to 0 at the output register (no
added latency, no glitch). The memory behind it keeps playing, so setting the
bit again mid-burst resumes at the entry being played.

### Playback gains (registers 10 and 11)

Each TX channel plays its own DAC memory's sample times its own complex gain
(`top/tx_gain.v`, built on `complex_multiplier` from `top/muls.v`): register
10 for radio 0 (`tx0`), register 11 for radio 1 (`tx1`). Both reset to 1.0,
which passes the samples through unchanged. Samples streamed by UHD are not
scaled.

- **Format:** `[15:0]` real part, `[31:16]` imaginary part, each 16-bit two's
  complement with 13 fraction bits: `0x2000` = 1.0, range -4.0 to just under
  4.0, step 2^-13. For amplitude g and rotation phi:
  `re = round(8192 * g * cos(phi))`, `im = round(8192 * g * sin(phi))`.

  | Gain | Register |
  |---|---|
  | 1.0 (reset) | `0x00002000` |
  | -1.0 (180°) | `0x0000E000` |
  | j (+90°) | `0x20000000` |
  | -j (-90°) | `0xE0000000` |
  | 0.5 | `0x00001000` |
  | 0 (channel silent, but see SISO below) | `0x00000000` |

- **Math:** for the sample `{I, Q}` and the gain `re + j*im`,
  `I' = (I*re - Q*im) / 2^13` and `Q' = (I*im + Q*re) / 2^13`, rounded half
  up and saturated per part at `0x7FFF` / `0x8000`. A positive phi turns the
  sample counterclockwise in the I/Q plane (I towards Q). Zero samples stay
  zero at any gain. Rotation or a gain above 1 can push a full-scale sample
  into saturation, so keep `|I + jQ| * g` at or below 32767. As before, only
  the top 12 bits of each part reach the AD9361.
- **Latency:** 8 `radio_clk` cycles (`LATENCY` in `tx_gain.v`, set by its
  pipeline parameters), on the DAC samples only. Compared with no gain stage,
  they leave 8 cycles later relative to the trigger, the four 1-bit memory
  pins and the ADC capture starts. The switch back to UHD's samples and the
  PA hold are delayed by the same 8 cycles, so a burst cut short by disabling
  playback still ends cleanly. To restore the old timing, lower each DAC
  delay (register 4, both halves) by 8. If a DAC delay is below 8,
  raise the four pin memory delays and the ADC delays by 8 instead. That keeps the old
  relative timing, but everything moves 8 cycles later after the trigger.
- **Changing a gain** works at any time, including mid-burst, and reaches the
  output with the same 8-cycle delay as the samples. Registers 10 and 11 are
  separate writes, so briefly one channel has its new gain and the other its
  old one. The VIO override (`probe_out16`) changes both on the same cycle.
- **SISO mode:** with one TX channel the AD9361 receives a single stream:
  channel 0's sample, or channel 1's wherever channel 0's I and Q are both
  zero in their top 12 bits (`lib/cat_io/antsdr_u205_io.v`). So radio 0's
  memory, delay and gain apply to whichever port UHD enabled. Where the two
  channels differ (memory contents, DAC delays or gains), channel 1's samples
  show up wherever channel 0's are near zero, which is every sample if
  register 10 is 0. Use per-channel differences only with both channels
  running (MIMO). In SISO mode, keep both DAC memories, DAC delays and gains
  equal.
- **TX1/TX2 phase flip** (`tx-phase-flip.md`): suppose the loopback shows
  channel 1 phi ahead of channel 0, with the RX offset removed. Then 1.0 at
  -phi in register 11 lines the channels up. For the 180° flip that is -1.0,
  `0x0000E000`.

Readback is in slots 14 to 17 (as written, and in effect after the VIO).

### `radio_clk` phase lock (register 0 [12], readback slot 20)

In 2-channel (MIMO) mode `radio_clk` is the AD9361's DATA_CLK divided by 2 in
a BUFR (`lib/cat_io/antsdr_u205_io.v`). Which DATA_CLK edge it rises on used
to be set by whenever that divider last started, so after a multi-chip sync,
which aligns DATA_CLK and RX_FRAME on every AD9361, two boards' `radio_clk`
could still be one DATA_CLK cycle (half a sample) apart. The lock keeps
`radio_clk` rising on the DATA_CLK edge where the FPGA holds a channel 1 word
(RX_FRAME high), so boards whose AD9361s are synchronized also share
`radio_clk`.

- **How:** once RX_FRAME has alternated against the other phase for 64
  DATA_CLK cycles in a row, the divider is cleared for a few cycles right
  after a falling `radio_clk` edge and released so that it restarts on the
  other edge. One `radio_clk` cycle stretches to 6-7 DATA_CLK cycles (no short
  pulse), then `radio_clk` runs one DATA_CLK cycle later than before. The
  edge the divider restarts on is learned: if a correction does not take, the
  next one releases a word later; after two in a row that did not take, the
  lock tries only every 4096 DATA_CLK cycles (67 µs at 61.44 MHz).
- **When:** after the divider starts (power-up, switching to 2-channel mode),
  after an AD9361 restart, and after a sync that moved RX_FRAME. Never in
  steady state. Nothing happens in 1-channel mode (`radio_clk` is DATA_CLK
  itself), while RX_FRAME does not alternate (RX data port idle), or with
  register 0 [12] set.
- **Cost:** the `radio_clk` domain misses 2 to 3 samples, so the command
  timer (`vita_time`) of a board that corrected ends 2-3 ticks behind one that
  did not. Do the timer reset on PPS (register 8) after the sync, once slot 20
  [0] is 1 on every board. For those cycles TX_FRAME stays low and the AD9361
  gets the last channel 2 sample again.
- **TX framing** follows `radio_clk`: channel 1 is taken from the `radio_clk`
  domain at each `radio_clk` edge. It used to toggle freely from reset, so it
  sat on either DATA_CLK cycle depending on when 2-channel mode came on.
- **RX pairing:** in the locked phase the RX logic takes both channels from
  one frame. With [12] = 1 the phase is arbitrary again, and half the starts
  pair RX2 one sample late (fixed differently on branch `2-fix-rx-interleave`).
- **Assumption, check on hardware:** the multi-chip sync aligns RX_FRAME as
  well as DATA_CLK. After a sync, `RADIO_CLK_OUT` (D17 / C17) of the boards
  should line up on a scope; with [12] = 1 they can also be one `DCLK_OUT`
  period apart. `RX_FRAME_OUT` (E16 / D16) shows RX_FRAME itself: high in the
  same `DCLK_OUT` cycle on every board if the sync aligned it, and, on each
  board, high while `RADIO_CLK_OUT` and `TX_FRAME_OUT` (C14 / C15) are low
  when the lock holds. First hardware run (2026-10-12): `DCLK_OUT` lined up but
  `RADIO_CLK_OUT` could flip from sync to sync. That sync ran in ALERT,
  apparently with a single SYNC pulse. ADI's sequence above uses two: the
  first syncs the BBPLLs, the second the digital clock dividers. Check again
  with both.
- **Status (slot 20):** [0] locked (2-channel mode, RX_FRAME alternating,
  `radio_clk` on the channel 1 word), [1] RX_FRAME alternating, [2] lock in
  charge, [15:8] corrections made (wraps).

### AD9361 ENABLE on PPS (register 0 [15:13])

`CAT_EN`, the AD9361's ENABLE pin, used to be tied high. It now comes from an
ODDR on the 40 MHz clock (`top/cat_en_pps.v`) and changes only on
`PPS_IN_EXT` rising edges, as register 0 [14:13] (or its VIO override) says;
[15] picks the 40 MHz edge it changes on. A copy goes out on `ENABLE_OUT`
(JP1 pin 8) for the scope:

| [14:13] | On every `PPS_IN_EXT` rising edge |
|---|---|
| `00`, `01` | nothing: ENABLE keeps its level |
| `10` | ENABLE low |
| `11` | ENABLE high |

- **Every edge**, not only the first of a sequence like `CAT_SYNC`: with `10`
  ENABLE goes low on the next edge and stays low through the later ones.
  Write the code on every board between two edges; the next edge switches
  them all.
- **Timing:** the same edge detect and 40 MHz clock as `CAT_SYNC`. With
  [15] = 0, ENABLE changes on the rising edge 2-3 cycles (50-75 ns) after the
  PPS edge, as before. With [15] = 1 it changes on the falling edge after
  that, half a cycle (12.5 ns) later (62.5-87.5 ns). Either way it is the
  same 40 MHz edge on every board that samples the PPS on the same edge (see
  `start_pulse_40m`).
- **Pins:** `CAT_EN` and `ENABLE_OUT` each come from their own ODDR, in their
  I/O block, fed the same data on the same clock. They differ only by their
  output buffers (`CAT_EN` is LVCMOS18, `ENABLE_OUT` LVCMOS33 in bank 16).
  `sync.rpt` lists the clock-to-pin delay of both after a build. Each level
  change is one transition on the pins. [15] can change at any time: while
  the level holds, the pins do not move, and an edge switch at the same
  moment as a level change still gives one transition, on one edge or the
  other.
- **New values** are in effect well under a microsecond after the write; a
  PPS edge that close to the write may still act on the old ones.
- **Reset:** ENABLE is high from power-up and after a clock reset (FPGA reset
  or MMCM unlock), as before. The register resets to `00`.
- **AD9361 side:** ENABLE only moves the ENSM while AD9361 register 0x014 [4]
  (ENSM pin control) is set, with [3] (level mode) for a level; in FDD,
  ENABLE high is FDD and low is ALERT (check UG-570). UHD never sets [4]: its
  0x014 writes (`0x21` FDD, `0x01` ALERT) clear it, and UHD repeats them on
  every stream setup, tune and rate change. Set ENABLE to the state you want
  first, then [4]: in level mode the chip follows the pin at once.
- **Readback:** slot 1 [15:13] as written; slot 2 [13:11] in effect ([13] the
  edge), [14] the VIO override, [15] ENABLE as driven (the register), [16]
  ENABLE read back from the `CAT_EN` pad (both a few `radio_clk` cycles late).
- **VIO:** `probe_out25` override, `probe_out26` the code and edge (like
  [15:13]); `probe_in41` / `probe_in42` the same as written / in effect,
  `probe_in43` ENABLE as driven,
  `probe_in44` ENABLE read back from the pad (both straight from their own
  domains, so they show even while `radio_clk` is stopped).
- **ILA** (`ila_radio_clk`, appended at probes 124-127): `reg_cat_en_ctrl`,
  `cat_en_ctrl` (in effect, 3 bits each), `cat_en_r` (ENABLE as driven) and `cat_en_pad_r`
  (read back from the pad), the last two 2-3 `radio_clk` cycles late. Trigger
  on `pps_time_rise` (probe 54) to see the PPS edge and ENABLE change a few
  cycles later.
- **Is the pin itself switching?** The readback, VIO and ILA values marked
  "as driven" are the register. Three checks reach further:
  1. **Pad readback** (slot 2 [16], `probe_in44`, ILA probe 127): `CAT_EN` is
     an IOBUF that always drives, and its input buffer reads the voltage on
     the pad. If it does not follow [15] within a few cycles, something on
     the board holds the pin (short, another driver). It cannot see an open
     trace between the pad and the AD9361.
  2. **`ENABLE_OUT` on a scope:** the same level from an ODDR of its own,
     with the same data and clock; shows the logic and timing (also the edge
     [15] picks), not the `CAT_EN` pin.
  3. **The AD9361 itself:** with 0x014 [4] (and [3]) set, read 0x017 [3:0]
     after each edge: `0x5` ALERT with ENABLE low, `0xA` FDD with it high.
     This is the only check that covers the trace and the chip's input.

```cpp
// On every board, between two PPS edges; the next edge switches them all.
// ctrl0 = whatever register 0 holds otherwise (e.g. PINS_ALL, see Examples).
regs->poke32(0, ctrl0 | (2u << 13));                  // 10: ENABLE low from the next edge on
// ... after that edge:
uint64_t st = regs->peek64(16);                       // slot 2
bool enable_reg = (st >> 15) & 1;                     // [15]: ENABLE as driven (0 now)
bool enable_pad = (st >> 16) & 1;                     // [16]: the pad; should match [15]
regs->poke32(0, ctrl0 | (3u << 13));                  // 11: ENABLE high from the next edge on
// ... after that edge, if you like:
regs->poke32(0, ctrl0);                               // 00: no more changes, ENABLE stays high
// The same on the 40 MHz falling edge: add (1u << 15) to each write, e.g.
regs->poke32(0, ctrl0 | (1u << 15) | (2u << 13));     // falling edge, 10
```

### RX_FRAME / TX_FRAME lock-in on PPS (register 17, readback slots 23 / 24)

On a chosen `PPS_IN_EXT` rising edge the FPGA keeps the state of RX_FRAME and
TX_FRAME and counts the lock-in (`top/pps_frame_lock.v`). Compare boards
that share the PPS: the same pairs on every board mean their frames have the
same phase at that edge.

- **Which edge:** counted from the first edge of a sequence, by the same
  rule as the command timer reset (`top/pps_first_edge.v`, register 8's dead
  zone): armed when [16] goes 1 (and at power-up) and again after a silence
  longer than the dead zone. Register 17 [15:0] = N lets N edges pass and
  locks in on the next one, edge N of the sequence (0 = the first edge). One
  lock-in per sequence. For another one without stopping the PPS, write [16]
  = 0 and then 1: the next edge starts a new sequence. A count lowered while
  waiting locks in on the next edge.
- **What is kept:** the two pairs `RX_FRAME_OUT` and `TX_FRAME_OUT` send for
  one DATA_CLK cycle, [1] the first half of the cycle and [0] the second:
  RX_FRAME as the IDDR caught it, TX_FRAME as the FPGA sends it. In
  2-channel mode `11` is a channel 1 cycle and `00` a channel 2 cycle.
- **Timing:** sampled on DATA_CLK (the BUFG the frame outputs leave on).
  The pairs are taken 2-3 DATA_CLK cycles after the PPS edge (2 synchronizer
  stages), the same number on every board that samples the PPS on the same
  DATA_CLK edge. A PPS edge that lands within setup/hold of a DATA_CLK edge
  can be seen a cycle apart on two boards, which flips the pairs in
  2-channel mode; repeat the lock-in to tell that from a real frame offset.
  Nothing happens while DATA_CLK is stopped (AD9361 not running).
- **Readback:** slot 23 register 17 as written. Slot 24: [15:0] lock-ins
  (wraps), [17:16] RX_FRAME pair and [19:18] TX_FRAME pair at the last
  lock-in, [20] armed (the next edge starts a sequence), [21] waiting (a
  sequence is running, its lock-in still to come), [47:32] edge index: the
  number of the latest edge of the sequence (first = 0), held at the
  lock-in edge. Slot 24 comes from the DATA_CLK domain a few cycles late.
- **Reset:** the count and the pairs clear with the FPGA clocks (FPGA reset,
  MMCM unlock), not with UHD. Register 17 resets to 0 edges, enabled, so by
  default the first edge of every sequence is locked in.

```cpp
// Lock in on the 3rd edge after the PPS comes back (or after re-arming).
regs->poke32(68, (1u << 16) | 2u);        // register 17: enable, 2 edges to pass
regs->poke32(68, 2u);                     // re-arm without stopping the PPS:
regs->poke32(68, (1u << 16) | 2u);        //   enable 0, then 1
// ... after the edges:
uint64_t fl = regs->peek64(192);          // slot 24
uint16_t lockins  = fl & 0xFFFF;
unsigned rx_frame = (fl >> 16) & 3;       // [1] first half, [0] second half
unsigned tx_frame = (fl >> 18) & 3;
bool     waiting  = (fl >> 21) & 1;       // still counting edges
```

### RX playback (registers 32-63 and 64-95)

Four synthetic signals per radio, added to that radio's RX samples ahead of
its DDC (`top/rx_playback.v`), for example simulated echoes. Playback k
(0..3) of a radio starts on every **ADC capture start** of that radio (the
trigger plus register 5 [15:0] for radio 0, [31:16] for radio 1), waits its
own delay, then plays its own sample memory, one entry per `radio_clk` cycle:

```
rx += sample[n] * correction * phase[k] * amplitude[k]
      * amplitude2[k][(counter[k] + offset[k]) mod 4096]
```

One block per radio, both in radio 0's user space for now: `BASE` = 32 for
radio 0, 64 for radio 1. UHD `poke32` offset = 4 x register.

| Register | `poke32` offset, radio 0 / 1 | Bits | Name | Reset |
|---|---|---|---|---|
| BASE+0 | 128 / 256 | [3:0] | enable, bit k = playback k. A playback also needs `enable_playback` (register 0 [0]) | 0 |
| BASE+1 | 132 / 260 | [11:0] | memory write pointer, shared by the radio's 8 memories | 0 |
| BASE+2 | 136 / 264 | [23:0] | correction, real part | `0x004000` |
| BASE+3 | 140 / 268 | [23:0] | correction, imaginary part | 0 |
| BASE+8+k | 160 + 4k / 288 + 4k | [15:0] | playback k delay after the capture start, `radio_clk` cycles, 0 = none | 0 |
| BASE+12+k | 176 + 4k / 304 + 4k | [31:0] | playback k phase difference, `2^32 = 2*pi` | `0x00004000` |
| BASE+16+k | 192 + 4k / 320 + 4k | [31:0] | playback k amplitude, `{im[15:0], re[15:0]}` | `0x00004000` |
| BASE+20+k | 208 + 4k / 336 + 4k | [31:0] | playback k sample memory[pointer] = `{I[15:0], Q[15:0]}`, then pointer + 1 | init file |
| BASE+24+k | 224 + 4k / 352 + 4k | [15:0] | playback k amplitude2 memory[pointer], then pointer + 1 | `0x4000` |
| BASE+28+k | 240 + 4k / 368 + 4k | [11:0] | playback k amplitude2 offset: added to the counter for the memory read address | 0 |
| 14 | 56 | [11:0] | amplitude2 counter start, shared by all eight playbacks (see below) | 0 |

Readback slot = register number (`peek64` offset 8 x slot) for BASE+0 to
BASE+19 and BASE+28 to BASE+31, as written; register 14 reads back in slot 22.
Slot BASE+20+k is playback k's status: `[13:2]` its amplitude2 counter (the
next capture start uses entry counter + offset), `[1]` playing, `[0]`
waiting out the delay.

- **Formats:** two's complement, 2^14 = 1.0 everywhere. Correction parts are
  24 bits (range -512 to just under 512), phase, amplitude and amplitude2
  16 bits (-2.0 to just under 2.0). 1.0 is `0x004000` / `0x4000`; a phase
  difference phi is `re = round(16384 * cos(phi))`, `im = round(16384 * sin(phi))`.
- **Math:** each of the four products is rounded half up (+0.5, then the 14
  fraction bits dropped) and saturated to 24 bits. A stage can take a sample
  past 16 bits (by up to 2^8) without clipping, as long as a later stage
  brings it back. The four playbacks are summed and added to I and Q of the
  RX sample, saturating at `0x7FFF` / `0x8000`. The ADC's 12 bits sit in
  [15:4], so a full-scale memory sample (32767) is as large as a full-scale
  ADC sample. Real RX samples are not delayed by any of this.
- **Memories:** per playback, a 928-entry sample memory (starts with
  `tx_dac_mem.mem`, the DAC memories' chirp) and a 4096-entry amplitude2
  memory (starts all 1.0). Write them like the DAC memories: pointer, then
  data. Writes past a memory's end are dropped, but the pointer still moves.
  Rewrite a memory with its playback off, or a burst can mix old and new
  entries.
- **amplitude2:** every playback counts its capture starts. A capture
  start plays its whole burst with entry (counter + offset[k]) mod 4096
  (pulse-to-pulse amplitude), then the counter steps by one (mod 4096). The
  offset is the one in effect when the burst starts (after the delay).
  Writing register 14 loads all eight counters (enabled or not) with the
  value written; a capture start in the same cycle as the load still uses
  the old counter. A disabled playback (its bit or `enable_playback` off)
  holds its counter at register 14's value, so after switching it on the
  first burst uses entry register 14 + offset. The status slot shows the
  counter.
- **Timing:** a capture start in cycle c starts the burst in cycle c + delay,
  and entry n is added to the ADC sample that reaches the radio in cycle
  c + delay + 33 + n (`LATENCY` = 33). Compared with a real signal, the burst
  sits delay + 33 samples (full rate, before decimation) after the ADC sample
  of the capture start. Set delay = N - 33 to place it N samples later.
- **Restarts** follow the DAC memories' rules: a capture start while the
  delay runs restarts the delay; one mid-burst restarts the burst at entry
  0. A burst cut short keeps its own amplitude2 entry. With capture starts
  exactly 928 cycles apart (the internal start pulse's period) and a fixed
  delay, consecutive bursts follow each other without a gap or overlap.
- **Changing coefficients** takes effect for the samples entering that
  multiplier from then on, so a burst can briefly see old and new values
  mixed between stages. Change them between bursts if that matters.

### Build version (readback slot 0)

Slot 0 says which bitstream a board runs: `[23:16]` MAJOR, `[15:0]` MINOR,
higher bits 0. Version 1.5 reads `0x010005`.

- **Source:** `antsdr_u220/version.txt` holds the version the next bitstream
  gets, as `MAJOR.MINOR` in decimal (MAJOR 0..255, MINOR 0..65535), e.g.
  `1.0`.
- **Counting:** a bitstream takes the version in the file, and once it is
  written the file goes up by one MINOR, so every bitstream has its own
  version. A failed build leaves the file alone and the retry gets the same
  version. For a new MAJOR, edit the file (e.g. `2.0`): the next bitstream
  gets exactly that. So after a build the file is one MINOR ahead of the
  bitstream just built; keep that in mind when committing it.
- **How:** two `write_bitstream` hooks of `impl_1`, set by
  `antsdr_u220/version.tcl` from both build scripts: `version_pre.tcl` writes
  the version into the bitstream's USR_ACCESS register
  (`BITSTREAM.CONFIG.USR_ACCESS`), which the FPGA reads through
  `USR_ACCESSE2` (`top/antsdr_u220.v`); `version_post.tcl` raises the file.
  So `make`, `make impl` and Generate Bitstream in the Vivado GUI all count,
  and a new version needs no new synthesis. A bitstream written outside
  `impl_1` (a bare `write_bitstream` in the Tcl console) carries no version.
- **Build log:** `VERSION  building 1.0` at the start (a malformed
  `version.txt` stops the build there, before any run) and `VERSION
  bitstream 1.0, version.txt now 1.1` at the end. If the file did not move,
  the build ends with an error after the reports: the hooks did not run (see
  `impl_1`'s `runme.log`), and the bitstream has no version.
- **Static:** fixed from configuration on. Like every user register, it is
  only reachable while `radio_clk` runs.

### Readback

| Slot | UHD `peek64` offset | Contents |
|---|---|---|
| 0 | 0 | build version: `[23:16] MAJOR, [15:0] MINOR` (`antsdr_u220/version.txt` when the bitstream was built), see [Build version](#build-version-readback-slot-0) |
| 1 | 8 | register 0 as written: `[15:13] cat_en_ctrl, [12] rclk_lock_off, [11] rx_insert_count, [10] start_pulse_40m, [9] cat_sync_enable, [8] start_pulse_enable, [7] PLAY_EXTERNAL_RX_ENABLE_N en, [6] PLAY_EXTERNAL_RX_ENABLE_P en, [5] PLAY_EXTERNAL_TX_ENABLE_N en, [4] PLAY_EXTERNAL_TX_ENABLE_P en, [3] rx_start_on_trigger, [2] amp, [1] source, [0] enable` |
| 2 | 16 | in effect (after VIO): `[16] CAT_EN read back from its pad, [15] CAT_EN (AD9361 ENABLE) as driven, [14] ENABLE override, [13:11] cat_en_ctrl, [10:7] pin enables (same order as register 0 [7:4]), [6] pin enable override, [5] rx capture override, [4] rx_start_on_trigger, [3] amp, [2] playback override, [1] source, [0] enable` |
| 3 | 24 | register 4 as written: `[31:16] radio 1 dac_delay, [15:0] radio 0 dac_delay` |
| 4 | 32 | in effect: `[32] delay override, [31:16] radio 1 dac_delay, [15:0] radio 0 dac_delay` |
| 5 | 40 | register 5 as written: `[31:16] radio 1 adc_delay, [15:0] radio 0 adc_delay` |
| 6 | 48 | in effect: `[32] delay override, [31:16] radio 1 adc_delay, [15:0] radio 0 adc_delay` |
| 7 | 56 | register 6 as written: `[31:16] PLAY_EXTERNAL_RX_ENABLE_N delay, [15:0] PLAY_EXTERNAL_RX_ENABLE_P delay` |
| 8 | 64 | in effect: `[32] delay override, [31:16] PLAY_EXTERNAL_RX_ENABLE_N delay, [15:0] PLAY_EXTERNAL_RX_ENABLE_P delay` |
| 9 | 72 | register 8 as written: `[31:16] dead zone, [0] pps_time_reset` |
| 10 | 80 | in effect (after VIO): `[32] override, [31:16] dead zone, [0] pps_time_reset` |
| 11 | 88 | status: `[47:32] timer resets done (wraps), [31:16] ms since the last PPS_IN_EXT rising edge (saturates at 0xFFFF), [0] armed` |
| 12 | 96 | register 9 as written: `[15:0] CAT_SYNC length` |
| 13 | 104 | `CAT_SYNC` status: `[47:32] pulses fired (wraps), [0] armed` |
| 14 | 112 | register 10 as written: `[31:16] im, [15:0] re` (radio 0 gain) |
| 15 | 120 | radio 0 gain in effect (after VIO): `[32] gain override, [31:16] im, [15:0] re` |
| 16 | 128 | register 11 as written: `[31:16] im, [15:0] re` (radio 1 gain) |
| 17 | 136 | radio 1 gain in effect (after VIO): `[32] gain override, [31:16] im, [15:0] re` |
| 18 | 144 | register 13 as written: `[31:16] PLAY_EXTERNAL_TX_ENABLE_N delay, [15:0] PLAY_EXTERNAL_TX_ENABLE_P delay` |
| 19 | 152 | in effect: `[32] delay override, [31:16] PLAY_EXTERNAL_TX_ENABLE_N delay, [15:0] PLAY_EXTERNAL_TX_ENABLE_P delay` |
| 20 | 160 | `radio_clk` phase lock status: `[15:8] corrections made (wraps), [2] lock in charge (2-channel mode and register 0 [12] = 0), [1] RX_FRAME alternating, [0] radio_clk rising on the channel 1 word`; see below |
| 21 | 168 | not used, reads 0 |
| 22 | 176 | register 14 as written: `[11:0]` RX playback amplitude2 counter start |
| 23 | 184 | register 17 as written: `[16] frame lock-in enable, [15:0] edges to pass` |
| 24 | 192 | RX_FRAME / TX_FRAME lock-in: `[47:32] edge index (first edge = 0, held at the lock-in), [21] waiting, [20] armed, [19:18] TX_FRAME pair, [17:16] RX_FRAME pair (both at the last lock-in), [15:0] lock-ins (wraps)`; see above |
| 25-31 | 200-248 | not used, read 0 |
| 32-55, 60-63 | 256-440, 480-504 | RX playback, radio 0: slot = register number, see RX playback |
| 64-87, 92-95 | 512-696, 736-760 | RX playback, radio 1 |

Slot 0 was added after commit `ddedeb7`, and every other slot below 32 moved
up by one with it: host code written before that reads slot n (n < 32) must
now read slot n + 1, `peek64` offset + 8. The RX playback slots did not move.

### Output pins

All LVCMOS33, bank 16. `START_PULSE`, `DCLK_OUT`, `RADIO_CLK_OUT`,
`RX_FRAME_OUT`, `TX_FRAME_OUT` and `SYNC_OUT` carry the same single-ended
signal on both pins of the pair. `ENABLE_OUT` is a single pin, `B16_L15_N`;
`B16_L15_P` is free. Each `PLAY_EXTERNAL_*` pin plays its own 1-bit memory
with its own delay.

| Pins | Package pins | Signal | Enable bits (register 0) |
|---|---|---|---|
| `START_PULSE_P` / `_N` | B20 / A20 (B16_L16) | start pulse, looped back to `PPS_IN_EXT` | [8] (both pins) |
| `PLAY_EXTERNAL_TX_ENABLE_P` / `_N` | A18 / A19 (B16_L17) | own memory each (registers 3 / 15) | [4] / [5] |
| `PLAY_EXTERNAL_RX_ENABLE_P` / `_N` | F16 / E17 (B16_L2) | own memory each (registers 7 / 16) | [6] / [7] |
| `DCLK_OUT_P` / `_N` | B17 / B18 (B16_L11) | AD9361 DATA_CLK, for measurement (own BUFG) | - |
| `RADIO_CLK_OUT_P` / `_N` | D17 / C17 (B16_L12) | `radio_clk` (DATA_CLK/2 in 2-channel mode, on RX_FRAME, see the phase lock), for measurement | - |
| `RX_FRAME_OUT_P` / `_N` | E16 / D16 (B16_L5) | AD9361 RX_FRAME as the FPGA caught it, launched like `DCLK_OUT` (same BUFG and ODDR), for measurement; see `pin-connector.md` | - |
| `TX_FRAME_OUT_P` / `_N` | C14 / C15 (B16_L3) | TX_FRAME the FPGA sends the AD9361, one DATA_CLK cycle late, launched like `DCLK_OUT`, for measurement; see `pin-connector.md` | - |
| `SYNC_OUT_P` / `_N` | B15 / B16 (B16_L7) | copy of `CAT_SYNC` | [9] |
| `CAT_SYNC` | K13 (bank 15, LVCMOS18) | AD9361 SYNC_IN | [9] |
| `ENABLE_OUT` | E18 (B16_L15_N, JP1-8) | copy of `CAT_EN`: own ODDR, same data and 40 MHz clock | [15:13] |
| `CAT_EN` | G15 (LVCMOS18) | AD9361 ENABLE, set on `PPS_IN_EXT` edges; an ODDR into an IOBUF, its pad read back in slot 2 [16] | [15:13] |

The two clock pairs and the two frame pairs only run while the AD9361 does.
The FPGA's own delay from the `CAT_DCLK_P` pin to each clock-out pin, slow
and fast corner, is in `antsdr_u220/clk_out.rpt` after a build; the frame
outputs have `DCLK_OUT`'s.

### Build-time constants (`antsdr_u220/top/tx_mem_player.v`)

| Localparam | Value | Meaning |
|---|---|---|
| `PERIOD` | 928 | start pulse period, `radio_clk` cycles |
| `PULSE_LEN` | 8 | start pulse width, `radio_clk` cycles |
| `MEM_LEN` | 928 | entries per burst, depth of all six memories, lines per init file |
| `DAC0_INIT_FILE` / `DAC1_INIT_FILE` (parameters) | `tx_dac_mem.mem` (both) | `$readmemh` contents at configuration (below) |
| `EXT_TX_EN_P_INIT_FILE` / `EXT_TX_EN_N_INIT_FILE` (parameters) | `play_external_tx_enable_mem.mem` (both) | as above |
| `EXT_RX_EN_P_INIT_FILE` / `EXT_RX_EN_N_INIT_FILE` (parameters) | `play_external_rx_enable_mem.mem` (both) | as above |

### Init file contents (`antsdr_u220/top/`)

What the memories hold after configuration, until the host rewrites them.
Entry n plays n `radio_clk` cycles after that memory starts (trigger + its own
delay), so with equal delays the four pin memories match. DAC
entries then take 8 more cycles through the gain stage (see Playback gains).

| Memory | File | Contents |
|---|---|---|
| DAC, radio 0 and radio 1 | `tx_dac_mem.mem` (both) | two LFM chirps: entries 0..159 (160) and 564..587 (24), zero elsewhere |
| `PLAY_EXTERNAL_TX_ENABLE_P` and `_N` | `play_external_tx_enable_mem.mem` (both) | 1 at entries 0..167 and 564..595: each chirp plus 8 entries, 0 elsewhere |
| `PLAY_EXTERNAL_RX_ENABLE_P` and `_N` | `play_external_rx_enable_mem.mem` (both) | identical to the TX enable file |

Memory writes and all user registers are on `radio_clk`: they only work while
the AD9361 is running (i.e. after UHD has opened the device).

### Examples

```cpp
#include <uhd/usrp/multi_usrp.hpp>
#include <algorithm>
#include <cmath>

auto usrp = uhd::usrp::multi_usrp::make("enable_user_regs");
auto regs = usrp->get_user_settings_iface(0);        // radio 0

const uint32_t PINS_ALL = 0x1F0;                       // [8:4]: start pulse and all four 1-bit memory pins on

// Load all six memories (playback disabled first, pins left enabled)
regs->poke32(0, PINS_ALL);
regs->poke32(4, 0);                                   // pointer = 0
for (uint32_t w : dac0_words) regs->poke32(8, w);     // radio 0: {I, Q}, top 12 bits of each used
regs->poke32(4, 0);
for (uint32_t w : dac1_words) regs->poke32(48, w);    // radio 1
regs->poke32(4, 0);
// One memory per external enable pin (register numbers; poke32 offset = 4 * register)
const uint32_t EXT_TX_P_MEM = 3, EXT_TX_N_MEM = 15, EXT_RX_P_MEM = 7, EXT_RX_N_MEM = 16;
for (uint32_t b : tx_enable_p_bits) regs->poke32(4 * EXT_TX_P_MEM, b & 1);
regs->poke32(4, 0);
for (uint32_t b : tx_enable_n_bits) regs->poke32(4 * EXT_TX_N_MEM, b & 1);
regs->poke32(4, 0);
for (uint32_t b : rx_enable_p_bits) regs->poke32(4 * EXT_RX_P_MEM, b & 1);
regs->poke32(4, 0);
for (uint32_t b : rx_enable_n_bits) regs->poke32(4 * EXT_RX_N_MEM, b & 1);

// Delays, two per register, [31:16] radio 1 / N pin, [15:0] radio 0 / P pin:
// DAC 5 / 12 cycles (radio 0 / 1), ADC capture 9 / 9, TX enable P 17 / N 19, RX enable P 23 / N 25
regs->poke32(16, (12u << 16) | 5u);                   // register 4:  DAC
regs->poke32(20, (9u << 16) | 9u);                    // register 5:  ADC, equal so the chunk counters match
regs->poke32(24, (25u << 16) | 23u);                  // register 6:  RX enable
regs->poke32(52, (19u << 16) | 17u);                  // register 13: TX enable

// Playback gains (registers 10, 11): {im, re}, 0x2000 = 1.0; g = amplitude, phi = rotation
auto gain_word = [](double g, double phi_deg) -> uint32_t {
    const double phi = phi_deg * M_PI / 180.0;
    auto part = [](double v) {
        return uint32_t(uint16_t(int16_t(std::clamp(std::lround(v * 8192.0), -32768L, 32767L))));
    };
    return (part(g * std::sin(phi)) << 16) | part(g * std::cos(phi));
};
regs->poke32(40, gain_word(1.0, 0.0));                // radio 0: unchanged, 0x00002000
regs->poke32(44, gain_word(1.0, 180.0));              // radio 1: inverted, 0x0000E000

// Enable: trigger from PPS_IN_EXT, PAs held on, RX gated to the trigger
regs->poke32(0, PINS_ALL | (1u << 0) | (0u << 1) | (1u << 2) | (1u << 3));   // 0x1FD

// Switch PLAY_EXTERNAL_RX_ENABLE_N off; its memory keeps playing, the other pins are unaffected
regs->poke32(0, 0x1FD & ~(1u << 7));                  // 0x17D

// Read back
uint64_t ver   = regs->peek64(0);    // slot 0: build version
uint8_t  ver_major = (ver >> 16) & 0xFF;
uint16_t ver_minor = ver & 0xFFFF;                    // 0x010005 = 1.5
uint64_t ctrl  = regs->peek64(8);    // slot 1: register 0 as written, incl. pin enables [7:4]
uint64_t state = regs->peek64(16);   // slot 2: in effect
bool playing_enabled = state & 1;
bool vio_overrides   = (state >> 2) & 1;
uint8_t pins_in_effect = (state >> 7) & 0xF;          // after the pin enable VIO override
uint64_t dac_delays = regs->peek64(32);               // in effect, slot 4 (register 4)
uint16_t dac0_delay = dac_delays & 0xFFFF;
uint16_t dac1_delay = (dac_delays >> 16) & 0xFFFF;
uint64_t tx_en_delays = regs->peek64(152);            // in effect, slot 19 (register 13)
uint16_t tx_enable_p_delay = tx_en_delays & 0xFFFF;
uint16_t tx_enable_n_delay = (tx_en_delays >> 16) & 0xFFFF;
uint64_t adc_delays = regs->peek64(48);               // in effect, slot 6 (register 5)
uint16_t adc1_delay = (adc_delays >> 16) & 0xFFFF;
uint64_t rx_en_delays = regs->peek64(64);             // in effect, slot 8 (register 6)
uint16_t rx_enable_p_delay = rx_en_delays & 0xFFFF;
uint16_t rx_enable_n_delay = (rx_en_delays >> 16) & 0xFFFF;
uint64_t gain1 = regs->peek64(136);                   // slot 17: radio 1 gain in effect
int16_t gain1_re = int16_t(gain1 & 0xFFFF);           // 8192 = 1.0
int16_t gain1_im = int16_t((gain1 >> 16) & 0xFFFF);
bool gain_vio_override = (gain1 >> 32) & 1;

// RX playback, radio 0 (block at register 32; radio 1: 64)
const uint32_t RXP0 = 32;
auto rxp = [&](uint32_t reg, uint32_t v) { regs->poke32(4 * reg, v); };
auto c14 = [](double re, double im) -> uint32_t {      // {im, re}, 0x4000 = 1.0
    auto part = [](double v) {
        return uint32_t(uint16_t(int16_t(std::clamp(std::lround(v * 16384.0), -32768L, 32767L))));
    };
    return (part(im) << 16) | part(re);
};
rxp(RXP0 + 0, 0);                                     // all four off while loading
for (int k = 0; k < 4; k++) {
    rxp(RXP0 + 1, 0);
    for (uint32_t w : echo_words[k]) rxp(RXP0 + 20 + k, w);   // {I, Q}, 928 entries
    rxp(RXP0 + 1, 0);
    for (uint16_t a : amp2_table[k]) rxp(RXP0 + 24 + k, a);   // 4096 entries, 0x4000 = 1.0
}
rxp(RXP0 + 2, 0x004000);                              // correction 1.0 (24-bit parts)
rxp(RXP0 + 3, 0);
rxp(RXP0 + 8, 200 - 33);                              // playback 0: 200 samples after the capture start
rxp(RXP0 + 12, c14(std::cos(0.5), std::sin(0.5)));    // phase difference 0.5 rad
rxp(RXP0 + 16, c14(0.1, 0.0));                        // amplitude 0.1
rxp(RXP0 + 28, 100);                                  // playback 0 reads amplitude2 from entry counter + 100
rxp(14, 0);                                           // all eight amplitude2 counters to 0
rxp(RXP0 + 0, 0x1);                                   // playback 0 on (with enable_playback)
uint64_t st = regs->peek64(8 * (RXP0 + 20));          // playback 0 status
uint16_t amp2_counter = (st >> 2) & 0xFFF;           // next burst: entry amp2_counter + 100

// RX starting on the trigger: plain "start now" command, generous timeout
uhd::stream_args_t sa("sc16");
auto rx = usrp->get_rx_stream(sa);
uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
cmd.stream_now = true;
rx->issue_stream_cmd(cmd);
// rx->recv(..., timeout > pulse period) - nothing arrives until each radio's capture start
```

Order: write register 0 only after UHD has finished opening the device;
otherwise its codec loopback self-test sees playback data and fails.

---

## VIO `u_vio_play` (b200_core, `bus_clk`)

Seven independent overrides. While an override is set, its VIO values replace
the register values (host writes still land, they have no effect).

| Probe | Width | Meaning |
|---|---|---|
| `probe_out0` | 1 | override playback bits (`probe_out1/2/6`) |
| `probe_out1` | 1 | `enable_playback` |
| `probe_out2` | 1 | `playback_source` |
| `probe_out6` | 1 | `amp_enable_on_playback` |
| `probe_out3` | 1 | override delays (`probe_out4/5/8/10/21..24`) |
| `probe_out4` | 16 | radio 0 DAC delay |
| `probe_out21` | 16 | radio 1 DAC delay |
| `probe_out5` | 16 | `PLAY_EXTERNAL_TX_ENABLE_P` delay |
| `probe_out23` | 16 | `PLAY_EXTERNAL_TX_ENABLE_N` delay |
| `probe_out8` | 16 | radio 0 ADC delay |
| `probe_out22` | 16 | radio 1 ADC delay |
| `probe_out10` | 16 | `PLAY_EXTERNAL_RX_ENABLE_P` delay |
| `probe_out24` | 16 | `PLAY_EXTERNAL_RX_ENABLE_N` delay |
| `probe_out9` | 1 | override ADC capture (`probe_out7`) |
| `probe_out7` | 1 | `rx_start_on_trigger` |
| `probe_out11` | 1 | override pin enables (`probe_out12`) |
| `probe_out12` | 4 | pin enables, `[0] PLAY_EXTERNAL_TX_ENABLE_P, [1] _TX_ENABLE_N, [2] _RX_ENABLE_P, [3] _RX_ENABLE_N`; starts at `0xF` |
| `probe_out13` | 1 | override command timer reset on PPS (`probe_out14/15`) |
| `probe_out14` | 1 | `pps_time_reset` |
| `probe_out15` | 16 | dead zone, ms; starts at 1500 |
| `probe_out16` | 1 | override playback gains (`probe_out17..20`) |
| `probe_out17` / `probe_out18` | 16 | radio 0 gain re / im; start at `0x2000` (1.0) / 0 |
| `probe_out19` / `probe_out20` | 16 | radio 1 gain re / im; start at `0x2000` (1.0) / 0 |
| `probe_out25` | 1 | override AD9361 ENABLE on PPS (`probe_out26`) |
| `probe_out26` | 3 | ENABLE on PPS, like register 0 [15:13]: [1:0] `10` low, `11` high, `00` / `01` no change; [2] falling edge; starts at `000` |
| `probe_in0` / `probe_in2` | 1 | `enable_playback`: register / in effect |
| `probe_in1` / `probe_in3` | 1 | `playback_source`: register / in effect |
| `probe_in8` / `probe_in9` | 1 | `amp_enable_on_playback`: register / in effect |
| `probe_in10` / `probe_in11` | 1 | `rx_start_on_trigger`: register / in effect |
| `probe_in4` / `probe_in6` | 16 | radio 0 DAC delay: register 4 [15:0] / in effect |
| `probe_in33` / `probe_in34` | 16 | radio 1 DAC delay: register 4 [31:16] / in effect |
| `probe_in5` / `probe_in7` | 16 | `PLAY_EXTERNAL_TX_ENABLE_P` delay: register 13 [15:0] / in effect |
| `probe_in37` / `probe_in38` | 16 | `PLAY_EXTERNAL_TX_ENABLE_N` delay: register 13 [31:16] / in effect |
| `probe_in12` / `probe_in13` | 16 | radio 0 ADC delay: register 5 [15:0] / in effect |
| `probe_in35` / `probe_in36` | 16 | radio 1 ADC delay: register 5 [31:16] / in effect |
| `probe_in14` / `probe_in15` | 16 | `PLAY_EXTERNAL_RX_ENABLE_P` delay: register 6 [15:0] / in effect |
| `probe_in39` / `probe_in40` | 16 | `PLAY_EXTERNAL_RX_ENABLE_N` delay: register 6 [31:16] / in effect |
| `probe_in16` / `probe_in17` | 4 | pin enables: register 0 [7:4] / in effect |
| `probe_in18` / `probe_in19` | 1 | `pps_time_reset`: register 8 [0] / in effect |
| `probe_in20` / `probe_in21` | 16 | dead zone, ms: register 8 [31:16] / in effect |
| `probe_in22` | 1 | armed: the next `PPS_IN_EXT` rising edge zeroes the timer |
| `probe_in23` | 16 | ms since the last `PPS_IN_EXT` rising edge |
| `probe_in24` | 16 | timer resets done |
| `probe_in25` / `probe_in27` | 16 | radio 0 gain re: register 10 [15:0] / in effect |
| `probe_in26` / `probe_in28` | 16 | radio 0 gain im: register 10 [31:16] / in effect |
| `probe_in29` / `probe_in31` | 16 | radio 1 gain re: register 11 [15:0] / in effect |
| `probe_in30` / `probe_in32` | 16 | radio 1 gain im: register 11 [31:16] / in effect |
| `probe_in41` / `probe_in42` | 3 | ENABLE on PPS and its edge: register 0 [15:13] / in effect |
| `probe_in43` | 1 | `CAT_EN` (AD9361 ENABLE) as driven |
| `probe_in44` | 1 | `CAT_EN` read back from its pad |

Set the values first, then flip `probe_out0` (the playback bits are not
filtered against partial arrival; the delays, the pin enables, the timer
reset settings, the gains and the ENABLE code are, so values changed together
take effect on the same cycle).

The gain probes (`probe_out17..20`, `probe_in25..32`) read best in signed
decimal (set the radix in the VIO dashboard): 8192 = 1.0.

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
