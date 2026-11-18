Ready-made **Davidian Schallmauer** plugins for Windows, macOS and Linux, built and tested from the same source by GitHub Actions. The first release: a circuit model of a classic two-channel 120 W high-gain head with four EL34s, with its clean and high-gain channels, valve effects loop, phase inverter, push-pull power stage, output transformer, global feedback and a 16 Ω 4x12 speaker load, plus an optional cabinet curve.

## Downloads

| File | For | Contains |
|---|---|---|
| `...-windows-x64.zip` | 64-bit Windows 10 and 11 | VST3 and LV2 |
| `...-macos-universal.zip` | macOS 10.13 and later, Apple silicon and Intel | Audio Unit and LV2 |
| `...-linux-x64.zip` | 64-bit Linux on x86 (glibc 2.29 or later, such as Ubuntu 20.04 or Debian 11) | LV2 |
| `...-linux-arm64.zip` | 64-bit Linux on ARM, such as a Raspberry Pi 4 or 5 | LV2 |

## Install

Unzip, close your host, and copy the format it uses:

- **VST3 (Windows):** copy the `Davidian Schallmauer.vst3` folder into `C:\Program Files\Common Files\VST3`.
- **Audio Unit (macOS):** copy `Davidian Schallmauer.component` into `~/Library/Audio/Plug-Ins/Components`. It isn't notarised, so clear the download quarantine once in Terminal: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Davidian Schallmauer.component"`.
- **LV2:** copy the `schallmauer.lv2` folder into `%APPDATA%\LV2` (Windows), `~/Library/Audio/Plug-Ins/LV2` (macOS, then clear the quarantine the same way) or `~/.lv2` (Linux).

Restart the host (or rescan its plugins) and look for **Schallmauer** by **Davidian**.

## What's in it

- Clean and High Gain channels, each with Gain, Bass, Middle, Treble, Presence and Volume, a channel footswitch, and a Master. Defaults are the owner's manual's starting settings.
- Every valve is solved with its grid current and its network each sample, and the phase inverter, output valves and global feedback are solved together, with the supply sagging under load.
- The circuit runs 4× oversampled at 44.1 and 48 kHz (2× at 88.2 and 96 kHz). No latency.
- An optional 4x12 cabinet curve, an input level trim (volts at 0 dBFS) and an output trim.
- The schematic it follows has a few errors and missing values. The corrections and estimates are listed in the README.
- It uses about half of one CPU core on a current x64 desktop.

## Testing status

- Every build passes the DSP tests: the network solver against exact responses, the valve tables, every valve's operating point, every tone control, output power (114 W at the onset of clipping against the 120 W rating), stability with every control up, and channel switching while playing.
- **VST3:** passes Steinberg's validator. A test host drives its editor with simulated mouse input, and its output matches the LV2 build sample for sample.
- **Audio Unit:** passes Apple's `auval` in strict mode, for both Apple silicon and Intel. A test host renders it, and its output matches the LV2 build sample for sample. It also opens the Cocoa view.
- The Linux bundles are also checked with the lilv LV2 parser.
- Not yet tried in a DAW by a person, and not yet compared with a real amplifier.

## Licence and AI usage

- **Licence:** GPL-3.0-or-later. The licence, `NOTICE.md` and `AI_USAGE.md` are inside each bundle. The VST3 includes the Steinberg VST3 SDK (MIT) and the Audio Unit includes Apple's AudioUnitSDK (Apache 2.0). Their licences are in the bundles too.
- **AI usage:** this plugin was made with generative AI (Claude Code). See `AI_USAGE.md`.
