# Tap Plugin Suite — UI Specification

## Overview

All seven plugins share a **glassmorphic dark** visual language: frosted-glass
panels, subtle glow accents, and a high-contrast dark background so every meter
and spectrum is immediately legible on a large monitor.  
The guiding UX principle is *heavy lifting under the hood*: role-based smart
presets and one-click auto-setup mean a producer can open any plugin, hit
**Smart Setup**, and have a production-ready starting point in seconds.  
Fine-tuning is exposed through a compact set of knobs, sliders, and menus — not
an ocean of parameters.

---

## 1. Shared Shell

```
┌─────────────────────────────────────────────────────────────┐
│  [PLUGIN NAME]          [Track Role ▾]   [Smart Setup]  [⚙] │  ← Header bar
├─────────────────────────────────────────────────────────────┤
│                                                             │
│               [PRIMARY VISUALISATION]                       │  ← Big-screen area
│                                                             │
├─────────────────────────────────────────────────────────────┤
│         [CONTROLS ROW — knobs & sliders]                    │  ← Controls bar
└─────────────────────────────────────────────────────────────┘
```

### Header bar (all plugins)
| Element | Type | Purpose |
|---|---|---|
| Plugin name | Label | Identity |
| Track Role | Drop-down menu (10 options) | Drives Smart Setup presets |
| Smart Setup | Push-button | One-click role-aware starting point |
| Settings (⚙) | Icon button → popover | Theme, resize, bypass, undo history |

### Settings popover (shared)
- **Window size** — Small (800×500), Medium (1200×700), Large (1600×900) toggle
- **Bypass** toggle
- **Reset to defaults** button
- **Copy / Paste state** buttons (for parameter snapshots)

---

## 2. EQ — High-Detail Spectrum Visualisation

### Big-screen area (≥60% of total height)

```
 dB
+18 ┤                              ·····
 +9 ┤           ···                      ·····
  0 ┼───────────────────────────────────────────
 -9 ┤   ···
-18 ┤
    └────┬────┬────┬────┬────┬────┬────┬────┬─→ Hz
        20  100  200  500  1k  2k  5k  10k 20k
```

- **Real-time spectrum analyzer** overlay (20 Hz–20 kHz, log scale).
  - Filled with a translucent gradient (blue-teal palette).
  - 30 ms peak-hold with slow fall-off so transients are visible without
    obscuring the curve.
- **EQ magnitude curve** drawn on top using `EqProcessor::computeMagnitudeDb`
  evaluated at ~512 log-spaced points; rendered as a bright white line with a
  soft glow.
- **Band handles** — one draggable circle per enabled band placed at its centre
  frequency on the curve.  
  - Drag left/right → changes frequency.  
  - Drag up/down → changes gain (±18 dB).  
  - Scroll wheel / pinch → changes Q.
- **Band highlight** — clicking a handle activates that band's parameter row
  below and fills its frequency region with a translucent colour.

### Controls row

Six band strips (one per biquad), laid out horizontally:

```
 [Band 1]  [Band 2]  [Band 3]  [Band 4]  [Band 5]  [Band 6]
  [HPF ▾]  [Peak▾]   [Peak▾]   [Peak▾]   [Peak▾]   [LPF ▾]
  Freq knob  Freq     Freq      Freq      Freq      Freq
  ——        Gain      Gain      Gain      Gain      ——
  ——        Q         Q         Q         Q         ——
  [ON/OFF]  [ON/OFF]  [ON/OFF]  [ON/OFF]  [ON/OFF]  [ON/OFF]
```

- **Filter type** menu per band: HPF, LPF, Low Shelf, High Shelf, Peak.
- **Freq knob** — 20 Hz–20 kHz, logarithmic.
- **Gain knob** — −18 to +18 dB (hidden for HPF/LPF).
- **Q knob** — 0.1–10 (hidden for shelves).
- **On/Off toggle** — lights up when band is active.

### Menu options (Settings popover, EQ-specific)
- **Role Preset** drop-down — LeadVocal Tight, LeadVocal Bright, 808 Scoop,
  DrumBus Glue, Master Balanced, FX Open, Manual.
- **Masking Overlay** — select a reference track (Kick, Snare, Piano, Synth,
  None) to ghost its spectrum behind the active spectrum.

---

## 3. Compressor — Full Gain-Reduction Visibility

### Big-screen area

Two panes side-by-side:

#### Left pane — Transfer curve + operating point
```
 Out
  │            ╱ ← slope above threshold (ratio)
  │           ╱
  │   ──────╱  ← soft knee transition
  │  ╱
  └────────────── In (dB)
        ↑
    threshold
```
- Drawn in real time; a bright dot tracks the instantaneous gain-reduction
  operating point on the curve, making ratio and knee immediately visual.

#### Right pane — Gain-reduction history strip
```
 0 dB ─────────────────────────────────────────
 -3 dB     ▓▓▓▓░░░░░░░░░░░░░░░░░░░░
 -6 dB   ▓▓▓▓▓▓▓▓
-12 dB
        ← 2 s of history →
```
- Scrolling waveform-style meter showing `gainReductionDb()` over the last 2 s.
- Peak-hold needle in a contrasting accent colour.
- Numeric readout of current GR in the top-right corner.

### Controls row

```
[VCA | Opto | VariMu]   Threshold  Ratio   Attack   Release   Knee   Makeup   Mix
       3-way toggle       knob      knob    knob      knob     knob    knob   slider
```

- **Mode** — 3-way illuminated toggle (VCA / Opto / Vari-Mu); changes the
  mode-scale of the time constants visually.
- **Threshold** knob — −60 to 0 dB.
- **Ratio** knob — 1:1 to 20:1, displayed as X:1.
- **Attack** knob — 0.01–100 ms.
- **Release** knob — 10–1000 ms.
- **Knee** knob — 0 (hard) to 12 dB (soft).
- **Makeup** knob — −24 to +24 dB; shows "A" badge when auto-makeup is active.
- **Mix** slider — 0–100% (parallel compression).

### Menu options (Settings popover, Compressor-specific)
- **Sidechain Source** — Internal, Key In, Track: Kick, Track: Vocal.
- **Smart Setup** (also in header) — applies `applySmartSetup(role)`.
- **Auto Makeup** toggle.

---

## 4. Tape Delay — Simple UI, Powerful Ducking

The delay UI stays deliberately uncluttered; the smart ducking engine handles
most of the complexity automatically.

### Big-screen area

```
 Tap tempo  ♩ = 120  [1/4 ▾]   [Tempo Sync ●]
 ┌─────────────────────────────────────────────────────┐
 │ ░░░░░░▓▓▓▓▓▓░░░░░░░░░░░▓▓▓▓░░░░░░░░░░░░░░▓▓░░░░░░  │  ← Input level (gray)
 │                                                     │
 │ ░░▓▓▓░░░░░░░░░░▓▓▓▓░░░░░░░░░░░░░░▓▓░░░░░░░░░░░░░░  │  ← Wet delay (teal)
 └─────────────────────────────────────────────────────┘
 Duck GR ▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░  0.0 dB
```

- **Input / wet level strips** — two horizontal running meter bars (gray = dry
  input, teal = delayed wet signal).  This directly shows ducking in action:
  when the artist raps (gray bar rises) the teal bar shrinks.
- **Duck GR strip** — thin bar below showing instantaneous duck gain reduction
  (derived from `duckEnvelope_`), so the engineer can see exactly how hard the
  ducking is working.
- **Tempo display** — tap-tempo button, BPM readout, note-division selector.

### Controls row

```
  Time        Feedback    Mix      Wow/Flutter    Tone HP    Tone LP
  knob        knob       slider     knob           knob       knob
────────────────────────────────────────────────────────────────────
  Duck Amount   Duck Threshold   Duck Attack   Duck Release   [Ping-Pong]
    slider          knob           knob           knob         toggle
```

**Top row** (primary — always visible):
- **Time** knob — 1–2000 ms; shows note value when tempo sync is on.
- **Feedback** knob — 0–95%.
- **Mix** slider — 0–100%.
- **Wow/Flutter** knob — 0–100%.
- **Tone HP** / **Tone LP** knobs — set the feedback path high-pass and
  low-pass frequencies.

**Bottom row** (ducking — clearly labelled "Duck" section):
- **Duck Amount** slider — 0–100%; 0 = ducking off, 100 = echoes fully mute
  when the input is active.  Bright orange accent so it's impossible to miss.
- **Duck Threshold** knob — −60 to 0 dBFS; default −30 dBFS.
- **Duck Attack** knob — 1–100 ms; default 5 ms.
- **Duck Release** knob — 10–600 ms; default 150 ms.
- **Ping-Pong** toggle.

### Menu options (Settings popover, Delay-specific)
- **Tempo Sync** toggle (also a header button).
- **Note Division** — 1/1, 1/2, 1/4, 1/8, 1/16, dotted, triplet variants.
- **Duck Preset** — Off, Light (20%), Vocal (60%), Hard (100%) — sets
  `duckAmount`, `duckThresholdDb`, `duckAttackMs`, `duckReleaseMs` together.

---

## 5. Relay — Track Utility

### Big-screen area
Large stereo level meter (peak + RMS + LUFS readout), wide layout.

### Controls row
```
  In Gain    HP Freq    LP Freq    Pan    Width    Phase    Out Gain
   knob       knob       knob     knob    knob    toggle     knob
```

---

## 6. Limiter

### Big-screen area
LUFS history graph (last 30 s) + true-peak ceiling line + gain-reduction bar.

### Controls row
```
  Ceiling   Threshold   Release   Lookahead   True Peak   Streaming Safe
   knob       knob       knob      knob        toggle       menu
```

---

## 7. Saturate3

### Big-screen area
Three vertical band columns (Low / Mid / High) each showing a mini waveform
scope of the saturated signal.

### Controls row (per band)
```
  Drive   Character   Mix   Solo   Mute
  knob    4-way btn  slider  btn    btn
```
Crossover frequencies shown as draggable dividers between the three column
areas in the visualisation.

---

## 8. Convolution IR Reverb

### Big-screen area
IR waveform display with decay-time overlay.

### Controls row
```
  IR Browser   Pre-Delay   Decay   Damping   Mix
  button        knob       knob     knob    slider
```

---

## 9. Layout and Sizing Guidelines

| Window size | Big-screen area height | Controls row height |
|---|---|---|
| Small  800×500  | 280 px | 120 px |
| Medium 1200×700 | 440 px | 140 px |
| Large  1600×900 | 600 px | 160 px |

- All knobs: 48 px diameter minimum at Medium size; double-click to type a
  value; right-click → reset to default / enter value / MIDI learn.
- All sliders: 8 px track width; thumb 16 px wide.
- Label font: Inter or system sans-serif, 11–13 px.
- Accent colour for ducking controls: `#FF7A00` (orange).
- EQ curve and active handles: `#FFFFFF` with `rgba(255,255,255,0.25)` glow.
- Gain-reduction meter fill: `#FF4040` (red→orange gradient).
- Wet delay meter fill: `#00C8A0` (teal).
