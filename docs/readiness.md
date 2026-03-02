# Plugin Readiness (75–100%)

This checklist tracks practical readiness for each plugin.
**50% readiness** = Core DSP working with real-world parameters, metering/state wired, parameter smoothing, multi-mode behaviour.
**75% readiness** = Full signal chain, lookahead/oversampling, per-band processing, modulation, preset-ready parameters.

## UI Specification
- [x] Big-screen visual layout defined in `docs/ui_spec.md`.
- [x] EQ: draggable band handles on live spectrum + magnitude curve (uses `computeMagnitudeDb`).
- [x] Compressor: transfer-curve pane + scrolling gain-reduction history strip.
- [x] Tape Delay: input/wet meter bars show ducking in action; Duck controls row clearly labelled.
- [x] Shared glassmorphic shell: header with Track Role menu, Smart Setup button, Settings popover.
- [x] Window size presets (Small 800×500 / Medium 1200×700 / Large 1600×900).
- [ ] JUCE Component implementation of EQ spectrum canvas.
- [ ] JUCE Component implementation of compressor GR arc + history strip.
- [ ] JUCE Component implementation of delay duck meter bars.

## Relay
- [x] Create JUCE processor/editor scaffolding with all Relay parameters.
- [x] Implement the core signal chain stub (gain → filters → pan → output).
- [x] Wire basic metering state (RMS/peak/LUFS placeholders).
- [x] Parameter smoothing on gain and pan to prevent zipper noise.
- [x] Mid/side width processing.
- [x] LUFS integrated metering (ITU-R BS.1770 K-weighted, via `LufsMeter`).
- [ ] Relay metadata broadcast via IPC (JUCE InterprocessConnection layer).

## Compressor
- [x] Define parameter layout (threshold/ratio/attack/release/knee/mix/mode).
- [x] Stub compressor modes (VCA/Opto/Vari-Mu) with shared gain reduction path.
- [x] Add gain-reduction meter placeholder in state/UI.
- [x] Soft knee compression curve.
- [x] Makeup gain parameter.
- [x] Auto-makeup gain calculation (`computeAutoMakeupDb`).
- [x] Sidechain input routing (`processWithSidechain`).
- [x] Smart setup from track role (`applySmartSetup(TrackRole)`).

## EQ
- [x] Define 6-band parametric parameters and per-band enable state.
- [x] Stub filter chain with bypassed biquads.
- [x] Add analyzer/curve UI placeholder container.
- [x] Band type selection: Peak, Low Shelf, High Shelf, Low Cut, High Cut.
- [x] Correct biquad coefficient calculation for all filter types.
- [x] Role-based preset loader (`loadRolePreset(TrackRole)`).
- [x] Frequency-response query (`computeMagnitudeDb(float)`) and `Biquad::magnitudeResponseDb` for drawing the EQ curve in the UI.
- [ ] Spectrum analyzer overlay (UI layer).
- [ ] Masking detection helper (requires external FFT spectrum data).

## Limiter
- [x] Define limiter parameters (ceiling, threshold, lookahead, release, true-peak).
- [x] Stub lookahead buffer and gain computation path.
- [x] Add overs indicator/state placeholder.
- [x] Lookahead buffer with configurable delay.
- [x] True-peak inter-sample detection (2× estimation).
- [x] Gain reduction metering.
- [x] Multi-stage release envelope (two-stage fast/slow ballistics).
- [x] Streaming-safe mode presets (`makeStreamingPreset`: Spotify, YouTube, Apple Music).

## Saturate3
- [x] Define per-band drive/shape/mix parameters for three bands.
- [x] Stub crossover and per-band saturation pipeline.
- [x] Add band meter placeholders.
- [x] Linkwitz-Riley crossover band splitting (low/mid/high).
- [x] Per-band waveshaping character: Tape (tanh), Tube (asymmetric exp), Transformer (cubic), Clean.
- [x] Configurable crossover frequencies.
- [x] 2× oversampling for aliasing reduction (linear-interpolation + averaging decimation).
- [x] Per-band solo/mute (`Band::soloed`, `Band::muted`).

## Tape Delay
- [x] Define delay time, feedback, wow/flutter, filters, mix.
- [x] Stub delay line with feedback loop.
- [x] Add tempo sync and ping-pong toggles (placeholder only).
- [x] Wow/flutter via LFO-modulated delay time.
- [x] Feedback-path LP/HP filtering (tone shaping).
- [x] Linear interpolation for smooth fractional-sample delay.
- [x] Tempo sync with DAW BPM (`Params::bpm` + `Params::beatDivision`).
- [x] Ducking: sidechain-driven wet-signal attenuation with envelope follower (`duckAmount`, `duckThresholdDb`, `duckAttackMs`, `duckReleaseMs`; `processWithSidechain`).
- [ ] Multi-tap delay mode.

## Convolution IR Reverb
- [x] Define IR selection, pre-delay, decay, mix, damping controls.
- [x] Stub convolution engine with placeholder impulse handling.
- [x] Add IR browser UI placeholder panel.
- [x] Decay envelope applied to impulse response.
- [x] Damping low-pass filter on wet signal.
- [x] Pre-delay with configurable time.
- [ ] FFT-based partitioned convolution for performance.
- [ ] IR file loader (WAV/AIFF, OS/JUCE layer).
- [ ] IR browser UI panel (UI layer).
