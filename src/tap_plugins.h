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
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void updateFilters();

  Params params_{};
  double sampleRate_ = 0.0;
  OnePoleHighpass highpassLeft_;
  OnePoleHighpass highpassRight_;
  OnePoleLowpass lpLeft_;
  OnePoleLowpass lpRight_;
};

class CompressorProcessor {
 public:
  enum class Mode { Vca, Opto, VariMu };

  struct Params {
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    float mix = 1.0f;
    Mode mode = Mode::Vca;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);
  float gainReductionDb() const;

 private:
  void updateTimeConstants();

  Params params_{};
  double sampleRate_ = 0.0;
  float envelope_ = 0.0f;
  float gain_ = 1.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float gainReductionDb_ = 0.0f;
};

class EqProcessor {
 public:
  struct Band {
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.7f;
    bool enabled = false;
  };

  struct Params {
    std::array<Band, 6> bands{};
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void updateFilters();

  Params params_{};
  double sampleRate_ = 0.0;
  std::array<Biquad, 6> leftFilters_{};
  std::array<Biquad, 6> rightFilters_{};
};

class LimiterProcessor {
 public:
  struct Params {
    float thresholdDb = -6.0f;
    float ceilingDb = -0.1f;
    float releaseMs = 60.0f;
    float lookaheadMs = 1.0f;
    bool truePeak = false;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  Params params_{};
  double sampleRate_ = 0.0;
  float gain_ = 1.0f;
  float releaseCoeff_ = 0.0f;
};

class Saturate3Processor {
 public:
  struct Band {
    float driveDb = 6.0f;
    float mix = 1.0f;
  };

  struct Params {
    Band low{};
    Band mid{};
    Band high{};
    float mix = 1.0f;
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void updateSaturation();

  Params params_{};
  double sampleRate_ = 0.0;
  float wetMix_ = 1.0f;
  float dryMix_ = 0.0f;
  float lowDrive_ = 1.0f;
  float midDrive_ = 1.0f;
  float highDrive_ = 1.0f;
  float totalMixWeight_ = 1.0f;
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
  };

  void prepare(double sampleRate);
  void setParams(const Params& params);
  void reset();
  void process(AudioBufferView buffer);

 private:
  void updateDelaySamples();

  Params params_{};
  double sampleRate_ = 0.0;
  std::vector<float> delayLeft_;
  std::vector<float> delayRight_;
  std::size_t writeIndex_ = 0;
  std::size_t delaySamples_ = 1;
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

  Params params_{};
  double sampleRate_ = 0.0;
  std::vector<float> impulse_{1.0f, 0.4f, 0.2f};
  std::vector<float> historyLeft_;
  std::vector<float> historyRight_;
  std::size_t writeIndex_ = 0;
  std::size_t preDelaySamples_ = 0;
};

}  // namespace tap
