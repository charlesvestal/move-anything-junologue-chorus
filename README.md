# Junologue Chorus for Move Anything

Juno-60 chorus emulation module based on [junologue-chorus](https://github.com/peterall/junologue-chorus) by Peter Allwin.

Faithful reproduction of the Roland Juno-60 BBD chorus with three classic modes.

## Features

- Three chorus modes: I, I+II, and II
- Two triangle LFOs at fixed rates matching Juno-60 hardware (0.513 Hz and 0.863 Hz)
- Delay times from hardware measurements by Andy Harman
- Adjustable dry/wet mix with equal-power crossfade
- Brightness control for pre/post filtering
- Soft limiter on input
- Works as an audio FX in Signal Chain patches

## Prerequisites

- [Move Anything](https://github.com/charlesvestal/move-anything) installed on your Ableton Move
- SSH access enabled: http://move.local/development/ssh

## Install

### Via Module Store (Recommended)

1. Launch Move Anything on your Move
2. Select **Module Store** from the main menu
3. Navigate to **Audio FX** > **Junologue Chorus**
4. Select **Install**

### Build from Source

Requires Docker (recommended) or ARM64 cross-compiler.

```bash
git clone https://github.com/charlesvestal/move-anything-junologue-chorus
cd move-anything-junologue-chorus
./scripts/build.sh
./scripts/install.sh
```

## Controls

| Control | Function |
|---------|----------|
| Knobs 1-3 | Mode, Mix, Brightness |

## Parameters

| Key | Type | Range | Default | Description |
|-----|------|-------|---------|-------------|
| mode | enum | I, I+II, II | I+II | Chorus mode (which LFOs are active) |
| mix | float | 0-1 | 0.5 | Dry/wet balance |
| brightness | float | 0-1 | 1.0 | Pre/post filter cutoff |

### Chorus Modes

- **I**: LFO1 only (0.513 Hz) - subtle chorus
- **I+II**: Both LFOs mixed at equal gain - rich ensemble
- **II**: LFO2 only (0.863 Hz) - faster, more vibrato-like

## License

MIT License - See [LICENSE](LICENSE)

Based on junologue-chorus by Peter Allwin.
