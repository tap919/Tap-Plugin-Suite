# Plugin Readiness (50–75%)

This checklist tracks practical readiness for each plugin.
**50% readiness** = Core DSP working with real-world parameters, metering/state wired, parameter smoothing, multi-mode behaviour.
**75% readiness** = Full signal chain, lookahead/oversampling, per-band processing, modulation, preset-ready parameters.

## Relay
- [x] Create JUCE processor/editor scaffolding with all Relay parameters.
- [x] Implement the core signal chain stub (gain → filters → pan → output).
- [x] Wire basic metering state (RMS/peak/LUFS placeholders).
- [x] Parameter smoothing on gain and pan to prevent zipper noise.
- [x] Mid/side width processing.
- [ ] LUFS integrated metering.
- [ ] Relay metadata broadcast via IPC.

## Compressor
- [x] Define parameter layout (threshold/ratio/attack/release/knee/mix/mode).
- [x] Stub compressor modes (VCA/Opto/Vari-Mu) with shared gain reduction path.
- [x] Add gain-reduction meter placeholder in state/UI.
- [x] Soft knee compression curve.
- [x] Makeup gain parameter.
- [ ] Auto-makeup gain calculation.
- [ ] Sidechain input routing.
- [ ] Smart setup from track role.

## EQ
- [x] Define 6-band parametric parameters and per-band enable state.
- [x] Stub filter chain with bypassed biquads.
- [x] Add analyzer/curve UI placeholder container.
- [x] Band type selection: Peak, Low Shelf, High Shelf, Low Cut, High Cut.
- [x] Correct biquad coefficient calculation for all filter types.
- [ ] Spectrum analyzer overlay.
- [ ] Masking detection helper.
- [ ] Role-based preset loader.

## Limiter
- [x] Define limiter parameters (ceiling, threshold, lookahead, release, true-peak).
- [x] Stub lookahead buffer and gain computation path.
- [x] Add overs indicator/state placeholder.
- [x] Lookahead buffer with configurable delay.
- [x] True-peak inter-sample detection (2× estimation).
- [x] Gain reduction metering.
- [ ] Multi-stage release envelope.
- [ ] Streaming-safe mode presets.

## Saturate3
- [x] Define per-band drive/shape/mix parameters for three bands.
- [x] Stub crossover and per-band saturation pipeline.
- [x] Add band meter placeholders.
- [x] Linkwitz-Riley crossover band splitting (low/mid/high).
- [x] Per-band waveshaping character: Tape (tanh), Tube (asymmetric exp), Transformer (cubic), Clean.
- [x] Configurable crossover frequencies.
- [ ] Oversampling for aliasing reduction.
- [ ] Per-band solo/mute.

## Tape Delay
- [x] Define delay time, feedback, wow/flutter, filters, mix.
- [x] Stub delay line with feedback loop.
- [x] Add tempo sync and ping-pong toggles (placeholder only).
- [x] Wow/flutter via LFO-modulated delay time.
- [x] Feedback-path LP/HP filtering (tone shaping).
- [x] Linear interpolation for smooth fractional-sample delay.
- [ ] Tempo sync with DAW BPM.
- [ ] Multi-tap delay mode.

## Convolution IR Reverb
- [x] Define IR selection, pre-delay, decay, mix, damping controls.
- [x] Stub convolution engine with placeholder impulse handling.
- [x] Add IR browser UI placeholder panel.
- [x] Decay envelope applied to impulse response.
- [x] Damping low-pass filter on wet signal.
- [x] Pre-delay with configurable time.
- [ ] FFT-based partitioned convolution for performance.
- [ ] IR file loader (WAV/AIFF).
- [ ] IR browser UI panel.
