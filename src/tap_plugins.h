#pragma once

#include <array>
#include <vector>

#include "tap_dsp.h"

namespace tap {

class RelayProcessor {
 public:
  struct Params {
    float gainInDb = 0.0f;
    float gainOutDb = 0.0f;
    float pan = 0.0f;
    float width = 1.0f;
    bool phaseInvert = false;
    float hpFreq = 20.0f;
    float lpFreq = 20000.0f;
    bool isRecording = false;  // True when DAW is recording.
    bool smartDeactivate = false;  // When true, bypass processing during recording.
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

  const MeterState& meters() const { return meters_; }
  float lufs() const { return lufsMeter_.lufs(); }

 private:
  void updateFilters();

  Params params_{};
  double sampleRate_ = 0.0;
  OnePoleHighpass highpassLeft_;
  OnePoleHighpass highpassRight_;
  OnePoleLowpass lowpassLeft_;
  OnePoleLowpass lowpassRight_;
  SmoothParam smoothGainIn_;
  SmoothParam smoothGainOut_;
  SmoothParam smoothPan_;
  MeterState meters_;
  LufsMeter lufsMeter_;
};

class CompressorProcessor {
 public:
  enum class Mode { Vca, Opto, VariMu };

  struct Params {
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    float kneeDb = 0.0f;
    float makeupGainDb = 0.0f;
    float mix = 1.0f;
    float lookaheadMs = 0.0f;  // Lookahead time (0–5 ms typical).
    Mode mode = Mode::Vca;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);
  void processWithSidechain(AudioBufferView buffer, AudioBufferView sidechain);
  float gainReductionDb() const;

  // Returns the approximate auto-makeup gain for the given threshold/ratio.
  static float computeAutoMakeupDb(float thresholdDb, float ratio);
  // Configures params for a typical setup based on the track role.
  void applySmartSetup(TrackRole role);

 private:
  void updateTimeConstants();
  void updateLookahead();

  Params params_{};
  double sampleRate_ = 0.0;
  float envelope_ = 0.0f;
  float gain_ = 1.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float gainReductionDb_ = 0.0f;
  std::vector<float> lookaheadLeft_;
  std::vector<float> lookaheadRight_;
  std::size_t lookaheadWriteIndex_ = 0;
  std::size_t lookaheadSamples_ = 0;
};

class EqProcessor {
 public:
  enum class BandType { Peak, LowShelf, HighShelf, LowCut, HighCut };
  enum class ClassicCurve { None, Neve1073, API550, SSL4000, Pultec };

  struct Band {
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.7f;
    BandType type = BandType::Peak;
    bool enabled = false;
    float saturation = 0.0f;  // 0=off, 1=full saturation (mild harmonic color).
  };

  struct Params {
    std::array<Band, 6> bands{};
    ClassicCurve classicCurve = ClassicCurve::None;  // Classic EQ emulation
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);
  // Apply a role-based starting EQ curve.
  void loadRolePreset(TrackRole role);
  // Load a classic EQ curve emulation (Neve, API, SSL, Pultec)
  void loadClassicCurve(ClassicCurve curve);
  // Compute the combined EQ magnitude response at the given frequency (dB).
  // Sums the response of all enabled bands; useful for drawing the EQ curve.
  float computeMagnitudeDb(float frequency) const;

 private:
  void updateFilters();
  float applySaturation(float x, float amount) const;

  Params params_{};
  double sampleRate_ = 0.0;
  std::array<Biquad, 6> leftFilters_{};
  std::array<Biquad, 6> rightFilters_{};
};

class LimiterProcessor {
 public:
  enum class Mode { Transparent, Hardware, Digital };

  struct Params {
    float thresholdDb = -6.0f;
    float ceilingDb = -0.1f;
    float releaseMs = 60.0f;
    float releaseMs2 = 500.0f;  // Slow second-stage release time.
    float lookaheadMs = 1.0f;
    bool truePeak = false;
    Mode mode = Mode::Transparent;  // Limiter character mode.
  };

  enum class StreamingPreset { None, Spotify, YouTube, AppleMusic };
  // Returns a Params preset tuned for the given streaming platform.
  static Params makeStreamingPreset(StreamingPreset preset);

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);
  float gainReductionDb() const { return gainReductionDb_; }

 private:
  void updateLookahead();
  float applySaturation(float x, Mode mode) const;

  Params params_{};
  double sampleRate_ = 0.0;
  float gain_ = 1.0f;
  float gain2_ = 1.0f;  // Slow-stage gain for two-stage release.
  float releaseCoeff_ = 0.0f;
  float releaseCoeff2_ = 0.0f;
  float gainReductionDb_ = 0.0f;
  std::vector<float> lookaheadLeft_;
  std::vector<float> lookaheadRight_;
  std::size_t lookaheadWriteIndex_ = 0;
  std::size_t lookaheadSamples_ = 0;
};

class Saturate3Processor {
 public:
  enum class Character { Tape, Tube, Transformer, Clean };

  struct Band {
    float driveDb = 6.0f;
    float mix = 1.0f;
    Character character = Character::Tape;
    bool muted = false;
    bool soloed = false;
  };

  struct Params {
    Band low{};
    Band mid{};
    Band high{};
    float lowCrossoverHz = 150.0f;
    float highCrossoverHz = 3000.0f;
    float mix = 1.0f;
    bool oversample = false;  // Enable 2× oversampling to reduce aliasing.
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void updateSaturation();
  static float shapeTape(float x);
  static float shapeTube(float x);
  static float shapeTransformer(float x);

  Params params_{};
  double sampleRate_ = 0.0;
  float wetMix_ = 1.0f;
  float dryMix_ = 0.0f;
  float lowDrive_ = 1.0f;
  float midDrive_ = 1.0f;
  float highDrive_ = 1.0f;
  // Previous-sample state for 2× oversampling interpolation (one per band/channel).
  bool oversampPrevReady_ = false;
  float oversampLowPrevL_ = 0.0f;
  float oversampLowPrevR_ = 0.0f;
  float oversampMidPrevL_ = 0.0f;
  float oversampMidPrevR_ = 0.0f;
  float oversampHighPrevL_ = 0.0f;
  float oversampHighPrevR_ = 0.0f;
  LinkwitzRileyCrossover crossoverLowLeft_;
  LinkwitzRileyCrossover crossoverLowRight_;
  LinkwitzRileyCrossover crossoverHighLeft_;
  LinkwitzRileyCrossover crossoverHighRight_;
};

class TapeDelayProcessor {
 public:
  static constexpr float kMaxDelayTimeSec = 2.0f;
  static constexpr float kMaxDelayTimeMs = kMaxDelayTimeSec * 1000.0f;

  struct Params {
    float timeMs = 320.0f;
    float feedback = 0.35f;
    float mix = 0.35f;
    float wowFlutter = 0.0f;
    float lowpassHz = 12000.0f;
    float highpassHz = 40.0f;
    bool pingPong = false;
    bool tempoSync = false;
    float bpm = 120.0f;          // Host BPM used when tempoSync is true.
    float beatDivision = 0.25f;  // Note fraction (e.g. 0.25 = 1/4 note).
    // Ducking: when the dry input (or sidechain) is above duckThresholdDb, the
    // wet delay signal is attenuated by duckAmount (0=off, 1=full duck).
    // Ideal for rap vocals: echoes are quiet while the artist is rapping and
    // bloom back in the gaps.
    float duckThresholdDb = -30.0f;
    float duckAmount = 0.0f;   // 0 = disabled, 1 = full wet attenuation.
    float duckAttackMs = 5.0f;
    float duckReleaseMs = 150.0f;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);
  // Same as process() but uses sidechain to drive ducking instead of the dry
  // input.  Pass {nullptr, nullptr, 0} to use the dry input as the detector.
  void processWithSidechain(AudioBufferView buffer, AudioBufferView sidechain);

 private:
  void updateDelaySamples();
  float readInterpolated(const std::vector<float>& buffer,
                         float delaySamples) const;

  Params params_{};
  double sampleRate_ = 0.0;
  std::vector<float> delayLeft_;
  std::vector<float> delayRight_;
  std::size_t writeIndex_ = 0;
  std::size_t delaySamples_ = 1;
  SimpleLFO lfo_;
  OnePoleLowpass feedbackLpLeft_;
  OnePoleLowpass feedbackLpRight_;
  OnePoleHighpass feedbackHpLeft_;
  OnePoleHighpass feedbackHpRight_;
  // Duck-envelope state.
  float duckEnvelope_ = 0.0f;
  float duckAttackCoeff_ = 0.0f;
  float duckReleaseCoeff_ = 0.0f;
};

class ConvolutionReverbProcessor {
 public:
  struct Params {
    float mix = 0.35f;
    float preDelayMs = 20.0f;
    float decay = 1.0f;
    float damping = 0.5f;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void setImpulse(std::vector<float> impulse);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void resizeHistory();
  void applyDecayToImpulse();

  Params params_{};
  double sampleRate_ = 0.0;
  std::vector<float> impulseRaw_{1.0f, 0.4f, 0.2f};
  std::vector<float> impulse_;
  std::vector<float> historyLeft_;
  std::vector<float> historyRight_;
  std::size_t writeIndex_ = 0;
  std::size_t preDelaySamples_ = 0;
  OnePoleLowpass dampingLeft_;
  OnePoleLowpass dampingRight_;
};

}  // namespace tap
