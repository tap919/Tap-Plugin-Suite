#include "tap_plugins.h"

#include <algorithm>
#include <cmath>

namespace tap {

void RelayProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateFilters();
  reset();
}

void RelayProcessor::setParams(const Params& params) {
  params_ = params;
  updateFilters();
}

void RelayProcessor::reset() {
  highpassLeft_.reset();
  highpassRight_.reset();
  lpLeft_.reset();
  lpRight_.reset();
}

void RelayProcessor::updateFilters() {
  highpassLeft_.setCutoff(params_.hpFreq, sampleRate_);
  highpassRight_.setCutoff(params_.hpFreq, sampleRate_);
  lpLeft_.setCutoff(params_.lpFreq, sampleRate_);
  lpRight_.setCutoff(params_.lpFreq, sampleRate_);
}

void RelayProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float gainIn = dbToLinear(params_.gainInDb);
  const float gainOut = dbToLinear(params_.gainOutDb);
  const float pan = clamp(params_.pan, -1.0f, 1.0f);
  const float panLeft = 0.5f * (1.0f - pan);
  const float panRight = 0.5f * (1.0f + pan);
  constexpr float kMidSideScale = 0.5f;
  const float width = std::max(0.0f, params_.width);

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    float left = buffer.left[i] * gainIn;
    float right = buffer.right[i] * gainIn;

    if (params_.phaseInvert) {
      left = -left;
      right = -right;
    }

    left = highpassLeft_.process(left);
    right = highpassRight_.process(right);
    left = lpLeft_.process(left);
    right = lpRight_.process(right);

    const float mid = kMidSideScale * (left + right);
    const float side = kMidSideScale * (left - right) * width;
    left = mid + side;
    right = mid - side;

    buffer.left[i] = left * panLeft * gainOut;
    buffer.right[i] = right * panRight * gainOut;
  }
}

void CompressorProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateTimeConstants();
  reset();
}

void CompressorProcessor::setParams(const Params& params) {
  params_ = params;
  updateTimeConstants();
}

void CompressorProcessor::reset() {
  envelope_ = 0.0f;
  gain_ = 1.0f;
  gainReductionDb_ = 0.0f;
}

void CompressorProcessor::updateTimeConstants() {
  float modeScale = 1.0f;
  if (params_.mode == Mode::Opto) {
    modeScale = 1.5f;
  } else if (params_.mode == Mode::VariMu) {
    modeScale = 2.0f;
  }
  attackCoeff_ = timeMsToCoeff(params_.attackMs * modeScale, sampleRate_);
  releaseCoeff_ = timeMsToCoeff(params_.releaseMs * modeScale, sampleRate_);
}

void CompressorProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float mix = clamp(params_.mix, 0.0f, 1.0f);
  const float thresholdDb = params_.thresholdDb;
  const float ratio = std::max(1.0f, params_.ratio);
  constexpr float kDetectorFloorDb = -120.0f;

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];
    const float peak = std::max(std::abs(inputLeft), std::abs(inputRight));
    const float detectorDb =
        peak > 1.0e-6f ? linearToDb(peak) : kDetectorFloorDb;

    const float overDb = detectorDb - thresholdDb;
    float gainDb = 0.0f;
    if (overDb > 0.0f) {
      gainDb = -(overDb - overDb / ratio);
    }

    const float targetGain = dbToLinear(gainDb);
    if (targetGain < gain_) {
      gain_ = attackCoeff_ * gain_ + (1.0f - attackCoeff_) * targetGain;
    } else {
      gain_ = releaseCoeff_ * gain_ + (1.0f - releaseCoeff_) * targetGain;
    }

    const float wetLeft = inputLeft * gain_;
    const float wetRight = inputRight * gain_;

    buffer.left[i] = wetLeft * mix + inputLeft * (1.0f - mix);
    buffer.right[i] = wetRight * mix + inputRight * (1.0f - mix);
  }

  gainReductionDb_ = linearToDb(gain_);
}

float CompressorProcessor::gainReductionDb() const {
  return gainReductionDb_;
}

void EqProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateFilters();
}

void EqProcessor::setParams(const Params& params) {
  params_ = params;
  updateFilters();
}

void EqProcessor::reset() {
  for (auto& filter : leftFilters_) {
    filter.reset();
  }
  for (auto& filter : rightFilters_) {
    filter.reset();
  }
}

void EqProcessor::updateFilters() {
  for (std::size_t index = 0; index < params_.bands.size(); ++index) {
    const auto& band = params_.bands[index];
    if (band.enabled) {
      leftFilters_[index].setPeaking(band.frequency, band.q, band.gainDb,
                                     sampleRate_);
      rightFilters_[index].setPeaking(band.frequency, band.q, band.gainDb,
                                      sampleRate_);
    } else {
      leftFilters_[index].setBypass();
      rightFilters_[index].setBypass();
    }
  }
}

void EqProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    float left = buffer.left[i];
    float right = buffer.right[i];

    for (std::size_t bandIndex = 0; bandIndex < leftFilters_.size();
         ++bandIndex) {
      left = leftFilters_[bandIndex].process(left);
      right = rightFilters_[bandIndex].process(right);
    }

    buffer.left[i] = left;
    buffer.right[i] = right;
  }
}

void LimiterProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  releaseCoeff_ = timeMsToCoeff(params_.releaseMs, sampleRate_);
  reset();
}

void LimiterProcessor::setParams(const Params& params) {
  params_ = params;
  releaseCoeff_ = timeMsToCoeff(params_.releaseMs, sampleRate_);
}

void LimiterProcessor::reset() {
  gain_ = 1.0f;
}

void LimiterProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float threshold = dbToLinear(params_.thresholdDb);
  const float ceiling = dbToLinear(params_.ceilingDb);

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];
    const float peak = std::max(std::abs(inputLeft), std::abs(inputRight));
    float targetGain = 1.0f;
    if (peak > threshold && peak > 0.0f) {
      targetGain = threshold / peak;
    }

    if (targetGain < gain_) {
      gain_ = targetGain;
    } else {
      gain_ = releaseCoeff_ * gain_ + (1.0f - releaseCoeff_) * targetGain;
    }

    buffer.left[i] = clamp(inputLeft * gain_, -ceiling, ceiling);
    buffer.right[i] = clamp(inputRight * gain_, -ceiling, ceiling);
  }
}

void Saturate3Processor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateSaturation();
}

void Saturate3Processor::setParams(const Params& params) {
  params_ = params;
  updateSaturation();
}

void Saturate3Processor::reset() {}

void Saturate3Processor::updateSaturation() {
  wetMix_ = clamp(params_.mix, 0.0f, 1.0f);
  dryMix_ = 1.0f - wetMix_;
  lowDrive_ = dbToLinear(params_.low.driveDb);
  midDrive_ = dbToLinear(params_.mid.driveDb);
  highDrive_ = dbToLinear(params_.high.driveDb);
  totalMixWeight_ = params_.low.mix + params_.mid.mix + params_.high.mix;
  if (totalMixWeight_ <= 0.0f) {
    totalMixWeight_ = 1.0f;
    wetMix_ = 0.0f;
    dryMix_ = 1.0f;
  }
}

void Saturate3Processor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float lowDrive = lowDrive_;
  const float midDrive = midDrive_;
  const float highDrive = highDrive_;
  const float totalMixWeight = totalMixWeight_;

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];

    const float satLeft =
        (std::tanh(inputLeft * lowDrive) * params_.low.mix +
         std::tanh(inputLeft * midDrive) * params_.mid.mix +
         std::tanh(inputLeft * highDrive) * params_.high.mix) /
        totalMixWeight;

    const float satRight =
        (std::tanh(inputRight * lowDrive) * params_.low.mix +
         std::tanh(inputRight * midDrive) * params_.mid.mix +
         std::tanh(inputRight * highDrive) * params_.high.mix) /
        totalMixWeight;

    buffer.left[i] = inputLeft * dryMix_ + satLeft * wetMix_;
    buffer.right[i] = inputRight * dryMix_ + satRight * wetMix_;
  }
}

void TapeDelayProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  const std::size_t maxSamples =
      static_cast<std::size_t>(std::max(1.0, sampleRate_ * kMaxDelayTimeSec));
  delayLeft_.assign(maxSamples, 0.0f);
  delayRight_.assign(maxSamples, 0.0f);
  updateDelaySamples();
  reset();
}

void TapeDelayProcessor::setParams(const Params& params) {
  params_ = params;
  updateDelaySamples();
}

void TapeDelayProcessor::reset() {
  std::fill(delayLeft_.begin(), delayLeft_.end(), 0.0f);
  std::fill(delayRight_.begin(), delayRight_.end(), 0.0f);
  writeIndex_ = 0;
}

void TapeDelayProcessor::updateDelaySamples() {
  if (delayLeft_.empty()) {
    delaySamples_ = 1;
    return;
  }

  const float clampedTime = clamp(params_.timeMs, 1.0f, kMaxDelayTimeMs);
  const std::size_t desired =
      static_cast<std::size_t>(clampedTime * 0.001f * sampleRate_);
  delaySamples_ = std::min(desired, delayLeft_.size() - 1);
}

void TapeDelayProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0 ||
      delayLeft_.empty()) {
    return;
  }

  const float mix = clamp(params_.mix, 0.0f, 1.0f);
  constexpr float kMaxFeedback = 0.95f;
  const float feedback = clamp(params_.feedback, 0.0f, kMaxFeedback);

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];

    const std::size_t readIndex =
        (writeIndex_ + delayLeft_.size() - delaySamples_) % delayLeft_.size();
    const float delayedLeft = delayLeft_[readIndex];
    const float delayedRight = delayRight_[readIndex];

    buffer.left[i] = inputLeft * (1.0f - mix) + delayedLeft * mix;
    buffer.right[i] = inputRight * (1.0f - mix) + delayedRight * mix;

    const float feedbackLeft = inputLeft + delayedLeft * feedback;
    const float feedbackRight = inputRight + delayedRight * feedback;

    if (params_.pingPong) {
      delayLeft_[writeIndex_] = feedbackRight;
      delayRight_[writeIndex_] = feedbackLeft;
    } else {
      delayLeft_[writeIndex_] = feedbackLeft;
      delayRight_[writeIndex_] = feedbackRight;
    }

    writeIndex_ = (writeIndex_ + 1) % delayLeft_.size();
  }
}

void ConvolutionReverbProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  resizeHistory();
  reset();
}

void ConvolutionReverbProcessor::setParams(const Params& params) {
  params_ = params;
  resizeHistory();
}

void ConvolutionReverbProcessor::setImpulse(std::vector<float> impulse) {
  if (impulse.empty()) {
    impulse = {1.0f};
  }
  impulse_ = std::move(impulse);
  resizeHistory();
}

void ConvolutionReverbProcessor::reset() {
  std::fill(historyLeft_.begin(), historyLeft_.end(), 0.0f);
  std::fill(historyRight_.begin(), historyRight_.end(), 0.0f);
  writeIndex_ = 0;
}

void ConvolutionReverbProcessor::resizeHistory() {
  if (sampleRate_ <= 0.0) {
    return;
  }
  const double preDelayMs =
      std::max(0.0, static_cast<double>(params_.preDelayMs));
  preDelaySamples_ =
      static_cast<std::size_t>(preDelayMs * 0.001 * sampleRate_);
  const std::size_t historySize =
      std::max<std::size_t>(impulse_.size() + preDelaySamples_ + 1, 1);
  historyLeft_.assign(historySize, 0.0f);
  historyRight_.assign(historySize, 0.0f);
  writeIndex_ = 0;
}

void ConvolutionReverbProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0 ||
      historyLeft_.empty()) {
    return;
  }

  const float mix = clamp(params_.mix, 0.0f, 1.0f);
  const std::size_t historySize = historyLeft_.size();

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];

    historyLeft_[writeIndex_] = inputLeft;
    historyRight_[writeIndex_] = inputRight;

    const std::size_t startIndex =
        (writeIndex_ + historySize - preDelaySamples_) % historySize;

    float sumLeft = 0.0f;
    float sumRight = 0.0f;
    for (std::size_t j = 0; j < impulse_.size(); ++j) {
      const std::size_t idx = (startIndex + historySize - j) % historySize;
      sumLeft += historyLeft_[idx] * impulse_[j];
      sumRight += historyRight_[idx] * impulse_[j];
    }

    buffer.left[i] = inputLeft * (1.0f - mix) + sumLeft * mix;
    buffer.right[i] = inputRight * (1.0f - mix) + sumRight * mix;

    writeIndex_ = (writeIndex_ + 1) % historySize;
  }
}

}  // namespace tap
