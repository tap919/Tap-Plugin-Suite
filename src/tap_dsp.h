#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tap {

constexpr float kPi = 3.141593f;
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
    const float clampedFrequency = clamp(frequency, 20.0f, 20000.0f);
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

}  // namespace tap
