#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tap {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

struct AudioBufferView {
  float* left = nullptr;
  float* right = nullptr;
  std::size_t numSamples = 0;
};

inline float clamp(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(value, maximum));
}

inline float dbToLinear(float db) {
  return std::pow(10.0f, db / 20.0f);
}

inline float linearToDb(float linear) {
  return 20.0f * std::log10(std::max(linear, 1.0e-6f));
}

inline float timeMsToCoeff(float timeMs, double sampleRate) {
  if (timeMs <= 0.0f || sampleRate <= 0.0) {
    return 0.0f;
  }
  return std::exp(-1.0f / (0.001f * timeMs * static_cast<float>(sampleRate)));
}

struct OnePoleLowpass {
  void reset(float value = 0.0f) { state = value; }

  void setCutoff(float frequency, double sampleRate) {
    if (sampleRate <= 0.0) {
      coefficient = 0.0f;
      return;
    }
    const float clampedFrequency = clamp(frequency, 20.0f, 20000.0f);
    coefficient = std::exp(-kTwoPi * clampedFrequency /
                           static_cast<float>(sampleRate));
  }

  float process(float input) {
    state = (1.0f - coefficient) * input + coefficient * state;
    return state;
  }

  float coefficient = 0.0f;
  float state = 0.0f;
};

struct OnePoleHighpass {
  void reset(float value = 0.0f) {
    state = value;
    lastInput = value;
  }

  void setCutoff(float frequency, double sampleRate) {
    if (sampleRate <= 0.0) {
      coefficient = 0.0f;
      return;
    }
    const float maxCutoff = 0.45f * static_cast<float>(sampleRate);
    const float clampedFrequency = clamp(frequency, 20.0f, maxCutoff);
    coefficient = std::exp(-kTwoPi * clampedFrequency /
                           static_cast<float>(sampleRate));
  }

  float process(float input) {
    const float output = coefficient * (state + input - lastInput);
    state = output;
    lastInput = input;
    return output;
  }

  float coefficient = 0.0f;
  float state = 0.0f;
  float lastInput = 0.0f;
};

struct Biquad {
  void reset() {
    z1 = 0.0f;
    z2 = 0.0f;
  }

  void setBypass() {
    b0 = 1.0f;
    b1 = 0.0f;
    b2 = 0.0f;
    a1 = 0.0f;
    a2 = 0.0f;
  }

  void setPeaking(float frequency, float q, float gainDb, double sampleRate) {
    if (sampleRate <= 0.0) {
      setBypass();
      return;
    }

    const float clampedFreq = clamp(
        frequency, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float clampedQ = std::max(0.1f, q);
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    const float omega = kTwoPi * clampedFreq / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * clampedQ);

    const float a0 = 1.0f + alpha / amplitude;
    b0 = (1.0f + alpha * amplitude) / a0;
    b1 = (-2.0f * cosOmega) / a0;
    b2 = (1.0f - alpha * amplitude) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha / amplitude) / a0;
  }

  void setLowShelf(float frequency, float q, float gainDb, double sampleRate) {
    if (sampleRate <= 0.0) {
      setBypass();
      return;
    }
    const float clampedFreq =
        clamp(frequency, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float omega =
        kTwoPi * clampedFreq / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * std::max(0.1f, q));
    const float sqrtA2alpha = 2.0f * std::sqrt(A) * alpha;

    const float a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + sqrtA2alpha;
    b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosOmega + sqrtA2alpha)) / a0;
    b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega)) / a0;
    b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosOmega - sqrtA2alpha)) / a0;
    a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega)) / a0;
    a2 = ((A + 1.0f) + (A - 1.0f) * cosOmega - sqrtA2alpha) / a0;
  }

  void setHighShelf(float frequency, float q, float gainDb,
                    double sampleRate) {
    if (sampleRate <= 0.0) {
      setBypass();
      return;
    }
    const float clampedFreq =
        clamp(frequency, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float omega =
        kTwoPi * clampedFreq / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * std::max(0.1f, q));
    const float sqrtA2alpha = 2.0f * std::sqrt(A) * alpha;

    const float a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + sqrtA2alpha;
    b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosOmega + sqrtA2alpha)) / a0;
    b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega)) / a0;
    b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosOmega - sqrtA2alpha)) / a0;
    a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega)) / a0;
    a2 = ((A + 1.0f) - (A - 1.0f) * cosOmega - sqrtA2alpha) / a0;
  }

  void setLowPass(float frequency, float q, double sampleRate) {
    if (sampleRate <= 0.0) {
      setBypass();
      return;
    }
    const float clampedFreq =
        clamp(frequency, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float omega =
        kTwoPi * clampedFreq / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * std::max(0.1f, q));

    const float a0 = 1.0f + alpha;
    b0 = ((1.0f - cosOmega) * 0.5f) / a0;
    b1 = (1.0f - cosOmega) / a0;
    b2 = ((1.0f - cosOmega) * 0.5f) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha) / a0;
  }

  void setHighPass(float frequency, float q, double sampleRate) {
    if (sampleRate <= 0.0) {
      setBypass();
      return;
    }
    const float clampedFreq =
        clamp(frequency, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float omega =
        kTwoPi * clampedFreq / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * std::max(0.1f, q));

    const float a0 = 1.0f + alpha;
    b0 = ((1.0f + cosOmega) * 0.5f) / a0;
    b1 = -(1.0f + cosOmega) / a0;
    b2 = ((1.0f + cosOmega) * 0.5f) / a0;
    a1 = (-2.0f * cosOmega) / a0;
    a2 = (1.0f - alpha) / a0;
  }

  float process(float input) {
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
  }

  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
  float z1 = 0.0f;
  float z2 = 0.0f;
};

// Simple one-pole parameter smoother to avoid zipper noise.
struct SmoothParam {
  void reset(float value) { state = value; }

  void setTime(float timeMs, double sampleRate) {
    coeff = timeMsToCoeff(timeMs, sampleRate);
  }

  float next(float target) {
    state = coeff * state + (1.0f - coeff) * target;
    return state;
  }

  float coeff = 0.0f;
  float state = 0.0f;
};

// Simple low-frequency oscillator for modulation (wow/flutter, etc.).
struct SimpleLFO {
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    phase_ = 0.0f;
  }

  void setFrequency(float hz) { frequency_ = hz; }

  void reset() { phase_ = 0.0f; }

  float next() {
    const float value = std::sin(kTwoPi * phase_);
    if (sampleRate_ > 0.0) {
      phase_ += frequency_ / static_cast<float>(sampleRate_);
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
    }
    return value;
  }

 private:
  double sampleRate_ = 0.0;
  float frequency_ = 1.0f;
  float phase_ = 0.0f;
};

// Two-pole Linkwitz-Riley crossover (2nd-order LP + HP pair).
struct LinkwitzRileyCrossover {
  void setCutoff(float frequency, double sampleRate) {
    lp.setLowPass(frequency, 0.707f, sampleRate);
    hp.setHighPass(frequency, 0.707f, sampleRate);
  }

  void reset() {
    lp.reset();
    hp.reset();
  }

  void process(float input, float& low, float& high) {
    low = lp.process(input);
    high = hp.process(input);
  }

  Biquad lp;
  Biquad hp;
};

// Real-time metering state.
struct MeterState {
  float peakLeft = 0.0f;
  float peakRight = 0.0f;
  float rmsLeft = 0.0f;
  float rmsRight = 0.0f;

  void reset() {
    peakLeft = 0.0f;
    peakRight = 0.0f;
    rmsLeft = 0.0f;
    rmsRight = 0.0f;
  }

  void update(const float* left, const float* right, std::size_t numSamples,
              float decayCoeff) {
    if (!left || !right || numSamples == 0) {
      return;
    }
    float sumSqL = 0.0f;
    float sumSqR = 0.0f;
    float peakL = peakLeft * decayCoeff;
    float peakR = peakRight * decayCoeff;
    for (std::size_t i = 0; i < numSamples; ++i) {
      const float absL = std::abs(left[i]);
      const float absR = std::abs(right[i]);
      if (absL > peakL) peakL = absL;
      if (absR > peakR) peakR = absR;
      sumSqL += left[i] * left[i];
      sumSqR += right[i] * right[i];
    }
    peakLeft = peakL;
    peakRight = peakR;
    rmsLeft = std::sqrt(sumSqL / static_cast<float>(numSamples));
    rmsRight = std::sqrt(sumSqR / static_cast<float>(numSamples));
  }
};

// Track role shared across plugins for smart-setup and preset loading.
enum class TrackRole {
  Generic,
  LeadVocal,
  AdLib,
  Bass808,
  Bass,
  Drums,
  Piano,
  Synth,
  Guitar,
  FXSend
};

// K-weighted short-term loudness estimator.
// Applies the ITU-R BS.1770 two-stage filter chain (high-shelf pre-filter +
// RLB high-pass) but does NOT implement the 400 ms block gating required by
// EBU R128 for true integrated LUFS.  Suitable for real-time metering
// feedback; not spec-compliant for integrated-loudness measurement.
struct LufsMeter {
  void prepare(double sampleRate) {
    // Stage 1: ITU-R BS.1770 K-weighting pre-filter.
    // High shelf at 1681.97 Hz (+3.9998 dB gain), Q = 1/√2.
    preL_.setHighShelf(1681.97f, 0.7071f, 3.9998f, sampleRate);
    preR_.setHighShelf(1681.97f, 0.7071f, 3.9998f, sampleRate);
    // Stage 2: ITU-R BS.1770 RLB high-pass filter.
    // Cutoff 38.13 Hz, Q = 1/√2, removes DC / sub-bass contribution.
    rlbL_.setHighPass(38.13f, 0.7071f, sampleRate);
    rlbR_.setHighPass(38.13f, 0.7071f, sampleRate);
    reset();
  }

  void reset() {
    preL_.reset();
    preR_.reset();
    rlbL_.reset();
    rlbR_.reset();
    sumSquares_ = 0.0;
    sampleCount_ = 0;
    lufs_ = -70.0f;
  }

  void process(const float* left, const float* right,
               std::size_t numSamples) {
    if (!left || !right || numSamples == 0) {
      return;
    }
    for (std::size_t i = 0; i < numSamples; ++i) {
      const float l = rlbL_.process(preL_.process(left[i]));
      const float r = rlbR_.process(preR_.process(right[i]));
      sumSquares_ +=
          static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    sampleCount_ += numSamples;
    if (sampleCount_ > 0) {
      const double mean =
          sumSquares_ / static_cast<double>(2 * sampleCount_);
      lufs_ = mean > 1.0e-10
                  ? static_cast<float>(10.0 * std::log10(mean) - 0.691)
                  : -70.0f;
    }
  }

  float lufs() const { return lufs_; }

 private:
  Biquad preL_, preR_, rlbL_, rlbR_;
  double sumSquares_ = 0.0;
  std::size_t sampleCount_ = 0;
  float lufs_ = -70.0f;
};

}  // namespace tap
