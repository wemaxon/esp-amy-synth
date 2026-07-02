# ES8390 Bring-Up Notes

Date: 2026-07-02

Board under test: custom `wt0132p4-a1` ESP32-P4 board with ES8390 codec.

## Wiring

Codec control I2C uses a separate bus:

- `CDATA`: IO10
- `CCLK`: IO9
- ES8390 7-bit address: `0x10`
- `AD0`: unconnected
- `ASDOUT/AD1`: IO2, weak-pulled low for address select

I2S:

- `MCLK`: IO6
- `SCLK/BCLK`: IO5
- `LRCK`: IO3
- `DSDIN`: IO4
- `ASDOUT`: IO2

Current line-out path:

- `LOUTP` and `ROUTP` are used as outputs.
- Each output path has `0R`, then `1uF` AC coupling, then `4.7R` series to the line-out connector.
- `LOUTN` and `ROUTN` are disconnected in the normal hardware path.
- MAX98306 speaker amp is not used during the current noise debug.
- Amp feature exists in firmware, `AMP_SHDN` is IO54, default off.

## Firmware State

WT0132P4 is configured for ES8390:

- sample rate: `44100 Hz`
- I2S word length: `16-bit`
- auto sine diagnostic: enabled
- sine frequency: `440 Hz`
- sine amplitude: `100%`
- digital silence: disabled
- DAC mute: disabled
- strict DAC-only mode: enabled
- control peripherals disabled for codec testing

Expected clocks:

- `MCLK`: about `11.2896 MHz` (`256 * 44.1 kHz`)
- `BCLK`: about `1.4112 MHz` (`44.1 kHz * 16 * 2`)
- `LRCK`: `44.1 kHz`

DAC-only firmware disables/mutes ADC-side paths:

- ADC serial output muted
- ADC dem output muted
- ADC digital path reset
- ADC analog modulators held in reset/powerdown
- ADC-to-DAC mixer disabled
- PGA gains set to no input

## Register Tests

Observed register dump after DAC VREF test:

- `0x40 = 0x60`: DAC serial port is 16-bit I2S.
- `0x44 = 0x00`: DAC mixer off, no ADC-to-DAC mix.
- `0x46 = 0xbf`, `0x47 = 0xbf`: DAC volumes at 0 dB.
- `0x49 = 0x0f`: DAC volume ramp disabled.
- `0x61 = 0xf9`: analog, bias, DAC VREF, reserved bit 4, bias level, and VMID level enabled.
- `0x64 = 0x30`: ADC modulators reset/powered down.
- `0xf1 = 0x0d`: CSM reports normal state.
- chip ID: `0x83 0x89`.

Tests already tried:

- Added `EN_DACVREFGEN` in `0x61` (`0xd9 -> 0xf9`): noise unchanged.
- Changed `0x69` line-out from `0xa0` to `0x20` to clear `SPD_DOWN1/2`: noise unchanged.
- Changed `0x69` to `0x23`, HP drivers enabled with `SPD_DOWN1/2` cleared: noise unchanged.
- Changed I2S/DAC word length from 16-bit to 32-bit at 44.1 kHz: noise unchanged.
- Changed sample rate from 44.1 kHz to 48 kHz while still in 32-bit mode: noise disappeared.
- Changed back from 32-bit to 16-bit while staying at 48 kHz: noise stayed gone.
- Changed `0x69` back to `0x20` line-out mode while staying at 48 kHz: noise stayed gone.

Current important discriminator:

- `44.1 kHz`: noisy.
- `48 kHz`: clean.
- `16-bit` vs `32-bit`: not the deciding factor.
- HP driver on/off: not the deciding factor.

## Measurements

Audio/noise behavior:

- With normal sine output, strong unwanted noise is audible on top of the sine.
- Noise frequency seen on scope is around `1.5-2 kHz`.
- Reducing sine amplitude reduces the noise level; at `10%` sine the noise is much quieter but still present.
- Digital silence over I2S leaves noise present, but much quieter.
- DAC mute produces about the same result as digital silence.
- Fully disabling I2S removes the noise completely.
- Noise is still present when measuring the ES8390 output differentially.
- At `48 kHz`, the noise disappears in both 32-bit and 16-bit I2S modes.
- Returning to `16-bit` at `48 kHz` still works, so 32-bit framing was not required.

Scope observations:

- Direct `LOUTP` and `ROUTP` relative to analog ground show sine plus random/oscillating noise.
- Output DC common-mode sits around `AVDD / 2`.
- AC-coupled probing shows the sine plus the unwanted noise.
- Short probe ground spring was used.
- Comparing ESP GND to audio-out GND did not reveal the same noise.

Power/reference observations:

- `AVDD`: initially 3.0 V LDO; later tested from ESP32-P4 dev board 3.3 V through ferrite.
- Noise was unchanged between those AVDD supply tests.
- `AVDD`: no visible noise on scope.
- `VMID`: about 1.34 V, no visible noise.
- `DACVREF`: about 2.57 V, no visible noise.
- `ADCVREF`: 0 V.
- `PCDD/DVDD`: 3.3 V, no visible noise.

I2C and wiring observations:

- External I2C pullups were added; codec I2C works at `0x10`.
- Occasional register read failure was seen during dumps.
- Removing I2C lines did not change the audio noise.
- Extra ground wires around I2S cables did not reduce the noise.
- `47R` series resistors on I2S lines did not reduce the noise.

## Current Interpretation

Likely ruled down:

- Pure analog output-stage self-noise, because I2S fully off gives zero noise.
- I2C coupling, because removing I2C lines does not change the noise.
- Simple flying-wire I2S return-path problem, because extra grounds and `47R` series resistors did not reduce it.
- Simple common-mode output residue, because the noise is also visible differentially.
- AVDD source as the only cause, because 3.0 V LDO and 3.3 V through ferrite behave the same.

Still plausible:

- ES8390 DAC clock/filter/register setup issue.
- I2S clock generation issue specific to the 44.1 kHz clock family.
- MCU clock divider jitter/modulation or fractional-clock artifacts at `11.2896 MHz` MCLK.
- Internal DAC/reference/control-loop oscillation when clocks are active.
- Board-level coupling that depends on active I2S clocks but is not fixed by temporary wiring changes.

Less likely after later tests:

- I2S bit-depth/framing width, because `48 kHz / 16-bit` is clean.
- Codec output driver mode, because HP driver mode and line-out mode behave the same.

## Next Useful Tests

1. Reproduce the failure after switching back to `44.1 kHz / 16-bit`.
2. Measure actual `MCLK`, `BCLK`, and `LRCK` at `44.1 kHz` and `48 kHz`; compare frequency error, duty cycle, and jitter/noise on MCLK.
3. Try forcing the I2S clock source to APLL for `44.1 kHz` if the ESP32-P4 clock tree supports a cleaner 44.1 kHz-family clock.
4. Try 44.1 kHz with a different MCLK multiple, for example `384 Fs` or `512 Fs`, and update the ES8390 clock registers if needed.
5. Scope output while triggering from LRCK/BCLK/MCLK to see if the 1-2 kHz component is periodic with clock divider or audio-buffer patterns.
6. Recheck local capacitor placement/values on `VMID`, `DACVREF`, `AVDD`, `PVDD`, and `DVDD` close to the ES8390.
7. If clock-source changes do not help, review/remove remaining vendor-style reserved-register values (`0x63`, `0x4d`, clock mux/divider values) one at a time.
