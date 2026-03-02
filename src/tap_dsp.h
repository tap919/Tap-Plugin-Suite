#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tap {

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
    const float normalized = clamp(frequency, 20.0f, 20000.0f);
    constexpr float kTwoPi = 6.28318530718f;
    coefficient = std::exp(-kTwoPi * normalized /
                           static_cast<float>(sampleRate));
  }

  float process(float input) {
    state = (1.0f - coefficient) * input + coefficient * state;
    return state;
  }

  float coefficient = 0.0f;
  float state = 0.0f;
};

}  // namespace tap
