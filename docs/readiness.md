# Plugin Readiness (10–20%)

This checklist marks the first practical milestone for each plugin:
**10% readiness** = JUCE plugin shell compiles, parameters are defined, pass-through audio works.
**20% readiness** = minimal DSP chain stubbed, meters/state wired, basic UI layout placeholders.

## Relay
- [ ] Create JUCE processor/editor scaffolding with all Relay parameters.
- [ ] Implement the core signal chain stub (gain → filters → pan → output).
- [ ] Wire basic metering state (RMS/peak/LUFS placeholders).

## Compressor
- [ ] Define parameter layout (threshold/ratio/attack/release/knee/mix/mode).
- [ ] Stub compressor modes (VCA/Opto/Vari-Mu) with shared gain reduction path.
- [ ] Add gain-reduction meter placeholder in state/UI.

## EQ
- [ ] Define 6-band parametric parameters and per-band enable state.
- [ ] Stub filter chain with bypassed biquads.
- [ ] Add analyzer/curve UI placeholder container.

## Limiter
- [ ] Define limiter parameters (ceiling, threshold, lookahead, release, true-peak).
- [ ] Stub lookahead buffer and gain computation path.
- [ ] Add overs indicator/state placeholder.

## Saturate3
- [ ] Define per-band drive/shape/mix parameters for three bands.
- [ ] Stub crossover and per-band saturation pipeline.
- [ ] Add band meter placeholders.

## Tape Delay
- [ ] Define delay time, feedback, wow/flutter, filters, mix.
- [ ] Stub delay line with feedback loop.
- [ ] Add tempo sync and ping-pong toggles (placeholder only).

## Convolution IR Reverb
- [ ] Define IR selection, pre-delay, decay, mix, damping controls.
- [ ] Stub convolution engine with placeholder impulse handling.
- [ ] Add IR browser UI placeholder panel.
