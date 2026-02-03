# Junologue Chorus - Move Anything Audio FX Module

Port of [peterall/junologue-chorus](https://github.com/peterall/junologue-chorus) for Ableton Move.

## Architecture

Single C file (`src/dsp/junologue_chorus.c`) implementing the Audio FX API v2.

### DSP Components

- **Delay line**: 512-sample circular buffer with linear-interpolated fractional read
- **Triangle LFOs**: Two fixed-rate oscillators (0.513 Hz, 0.863 Hz) matching Juno-60 hardware
- **First-order lowpass filters**: Pre-filter (before delay) and post-filter (after delay, per channel)
- **Soft limiter**: stmlib-style saturation on mono input

### Signal Flow

```
Stereo In → Mono Sum → Soft Limit → Pre-LPF → Delay Write
                                                    ↓
                              LFO1 → Tap L/R (delay_times[0]/[1])
                              LFO2 → Tap L/R (delay_times[0]/[1])
                                                    ↓
                              Mode Gain Mix → Post-LPF L/R
                                                    ↓
                              Dry/Wet Mix → Stereo Out
```

### Parameters

| Key | Type | Range | Default | Description |
|-----|------|-------|---------|-------------|
| mode | enum | I, I+II, II | I | Chorus mode (which LFOs are active) |
| mix | float | 0-1 | 0.5 | Dry/wet balance (equal-power crossfade) |
| brightness | float | 0-1 | 0.5 | Pre/post filter cutoff |

### Chorus Modes

- **I**: LFO1 only (0.513 Hz) - subtle chorus
- **I+II**: Both LFOs mixed at equal gain (-3dB each) - rich ensemble
- **II**: LFO2 only (0.863 Hz) - faster, more vibrato-like

### Delay Times

From Juno-60 hardware measurements (Andy Harman):
- Tap 0: 1.54ms - 5.15ms (used for left channel)
- Tap 1: 1.51ms - 5.40ms (used for right channel, inverted LFO)

## Build & Deploy

```bash
./scripts/build.sh          # Cross-compile via Docker
./scripts/install.sh        # Deploy to Move via SSH
```
