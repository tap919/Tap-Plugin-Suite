#include "tap_plugins.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tap {

void RelayProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  constexpr float kSmoothTimeMs = 5.0f;
  smoothGainIn_.setTime(kSmoothTimeMs, sampleRate);
  smoothGainOut_.setTime(kSmoothTimeMs, sampleRate);
  smoothPan_.setTime(kSmoothTimeMs, sampleRate);
  smoothGainIn_.reset(dbToLinear(params_.gainInDb));
  smoothGainOut_.reset(dbToLinear(params_.gainOutDb));
  smoothPan_.reset(params_.pan);
  lufsMeter_.prepare(sampleRate);
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
  lowpassLeft_.reset();
  lowpassRight_.reset();
  meters_.reset();
  lufsMeter_.reset();
}

void RelayProcessor::updateFilters() {
  highpassLeft_.setCutoff(params_.hpFreq, sampleRate_);
  highpassRight_.setCutoff(params_.hpFreq, sampleRate_);
  lowpassLeft_.setCutoff(params_.lpFreq, sampleRate_);
  lowpassRight_.setCutoff(params_.lpFreq, sampleRate_);
}

void RelayProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }
  if (sampleRate_ <= 0.0) {
    return;
  }

  // Smart deactivation: bypass processing when recording if enabled
  if (params_.smartDeactivate && params_.isRecording) {
    // Pass through audio unchanged when recording with smart deactivate enabled
    // Still update meters so the user can see levels
    constexpr float kMeterDecay = 0.95f;
    meters_.update(buffer.left, buffer.right, buffer.numSamples, kMeterDecay);
    lufsMeter_.process(buffer.left, buffer.right, buffer.numSamples);
    return;
  }

  const float targetGainIn = dbToLinear(params_.gainInDb);
  const float targetGainOut = dbToLinear(params_.gainOutDb);
  const float targetPan = clamp(params_.pan, -1.0f, 1.0f);
  constexpr float kMidSideScale = 0.5f;
  const float width = std::max(0.0f, params_.width);

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float gainIn = smoothGainIn_.next(targetGainIn);
    const float gainOut = smoothGainOut_.next(targetGainOut);
    const float pan = smoothPan_.next(targetPan);

    float panLeft = 1.0f;
    float panRight = 1.0f;
    if (pan < 0.0f) {
      panRight = 1.0f + pan;
    } else if (pan > 0.0f) {
      panLeft = 1.0f - pan;
    }

    float left = buffer.left[i] * gainIn;
    float right = buffer.right[i] * gainIn;

    if (params_.phaseInvert) {
      left = -left;
      right = -right;
    }

    left = highpassLeft_.process(left);
    right = highpassRight_.process(right);
    left = lowpassLeft_.process(left);
    right = lowpassRight_.process(right);

    const float mid = kMidSideScale * (left + right);
    const float side = kMidSideScale * (left - right) * width;
    left = mid + side;
    right = mid - side;

    buffer.left[i] = left * panLeft * gainOut;
    buffer.right[i] = right * panRight * gainOut;
  }

  constexpr float kMeterDecay = 0.95f;
  meters_.update(buffer.left, buffer.right, buffer.numSamples, kMeterDecay);
  lufsMeter_.process(buffer.left, buffer.right, buffer.numSamples);
}

void CompressorProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateTimeConstants();
  updateLookahead();
  reset();
}

void CompressorProcessor::setParams(const Params& params) {
  params_ = params;
  updateTimeConstants();
  updateLookahead();
}

void CompressorProcessor::reset() {
  envelope_ = 0.0f;
  gain_ = 1.0f;
  gainReductionDb_ = 0.0f;
  std::fill(lookaheadLeft_.begin(), lookaheadLeft_.end(), 0.0f);
  std::fill(lookaheadRight_.begin(), lookaheadRight_.end(), 0.0f);
  lookaheadWriteIndex_ = 0;
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

void CompressorProcessor::updateLookahead() {
  if (sampleRate_ <= 0.0) {
    return;
  }
  const float ms = clamp(params_.lookaheadMs, 0.0f, 5.0f);
  lookaheadSamples_ =
      static_cast<std::size_t>(ms * 0.001f * static_cast<float>(sampleRate_));
  if (lookaheadSamples_ < 1) {
    lookaheadSamples_ = 1;
  }
  const std::size_t bufSize = lookaheadSamples_ + 1;
  if (lookaheadLeft_.size() != bufSize) {
    lookaheadLeft_.assign(bufSize, 0.0f);
    lookaheadRight_.assign(bufSize, 0.0f);
    lookaheadWriteIndex_ = 0;
  }
}

void CompressorProcessor::process(AudioBufferView buffer) {
  processWithSidechain(buffer, {nullptr, nullptr, 0});
}

void CompressorProcessor::processWithSidechain(AudioBufferView buffer,
                                                AudioBufferView sidechain) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float mix = clamp(params_.mix, 0.0f, 1.0f);
  const float thresholdDb = params_.thresholdDb;
  const float ratio = std::max(1.0f, params_.ratio);
  const float kneeDb = std::max(0.0f, params_.kneeDb);
  const float makeupGain = dbToLinear(params_.makeupGainDb);
  constexpr float kDetectorFloorDb = -120.0f;

  const bool hasSidechain = sidechain.left && sidechain.right &&
                             sidechain.numSamples == buffer.numSamples;
  const bool useLookahead = lookaheadSamples_ > 1 && !lookaheadLeft_.empty();
  const std::size_t bufSize = lookaheadLeft_.size();

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];

    // When lookahead is enabled, write to lookahead buffer
    if (useLookahead) {
      lookaheadLeft_[lookaheadWriteIndex_] = inputLeft;
      lookaheadRight_[lookaheadWriteIndex_] = inputRight;
    }

    // Use sidechain for detection when provided, otherwise use input.
    // When lookahead is active, scan ahead to detect future peaks.
    float detectorDb = kDetectorFloorDb;
    if (useLookahead) {
      float futurePeak = 0.0f;
      for (std::size_t j = 0; j < lookaheadSamples_; ++j) {
        const std::size_t idx = (lookaheadWriteIndex_ + bufSize - j) % bufSize;
        const float detL = hasSidechain ? sidechain.left[i] : lookaheadLeft_[idx];
        const float detR = hasSidechain ? sidechain.right[i] : lookaheadRight_[idx];
        const float peak = std::max(std::abs(detL), std::abs(detR));
        if (peak > futurePeak) futurePeak = peak;
      }
      detectorDb = futurePeak > 1.0e-6f ? linearToDb(futurePeak) : kDetectorFloorDb;
    } else {
      const float detL = hasSidechain ? sidechain.left[i] : inputLeft;
      const float detR = hasSidechain ? sidechain.right[i] : inputRight;
      const float peak = std::max(std::abs(detL), std::abs(detR));
      detectorDb = peak > 1.0e-6f ? linearToDb(peak) : kDetectorFloorDb;
    }

    const float overDb = detectorDb - thresholdDb;
    float gainDb = 0.0f;
    if (kneeDb > 0.0f && overDb > -kneeDb * 0.5f &&
        overDb < kneeDb * 0.5f) {
      // Soft knee region.
      const float x = overDb + kneeDb * 0.5f;
      gainDb = -(x * x) / (2.0f * kneeDb) * (1.0f - 1.0f / ratio);
    } else if (overDb >= kneeDb * 0.5f) {
      gainDb = -overDb * (1.0f - 1.0f / ratio);
    }

    const float targetGain = dbToLinear(gainDb);
    if (targetGain < gain_) {
      gain_ = attackCoeff_ * gain_ + (1.0f - attackCoeff_) * targetGain;
    } else {
      gain_ = releaseCoeff_ * gain_ + (1.0f - releaseCoeff_) * targetGain;
    }

    // Read delayed samples when lookahead is active
    float processLeft = inputLeft;
    float processRight = inputRight;
    if (useLookahead) {
      const std::size_t readIdx =
          (lookaheadWriteIndex_ + bufSize - lookaheadSamples_ + 1) % bufSize;
      processLeft = lookaheadLeft_[readIdx];
      processRight = lookaheadRight_[readIdx];
      lookaheadWriteIndex_ = (lookaheadWriteIndex_ + 1) % bufSize;
    }

    const float wetLeft = processLeft * gain_ * makeupGain;
    const float wetRight = processRight * gain_ * makeupGain;

    buffer.left[i] = wetLeft * mix + processLeft * (1.0f - mix);
    buffer.right[i] = wetRight * mix + processRight * (1.0f - mix);
  }

  // Store gain reduction as a positive dB value (0 dB = no reduction).
  gainReductionDb_ = -linearToDb(gain_);
}

float CompressorProcessor::computeAutoMakeupDb(float thresholdDb,
                                               float ratio) {
  if (ratio <= 1.0f) {
    return 0.0f;
  }
  // Classic approximation: half the expected average gain reduction.
  return -(thresholdDb * (1.0f - 1.0f / ratio)) * 0.5f;
}

void CompressorProcessor::applySmartSetup(TrackRole role) {
  switch (role) {
    case TrackRole::LeadVocal:
      params_.thresholdDb = -20.0f;
      params_.ratio = 3.0f;
      params_.attackMs = 7.0f;
      params_.releaseMs = 120.0f;
      params_.kneeDb = 6.0f;
      break;
    case TrackRole::AdLib:
      params_.thresholdDb = -16.0f;
      params_.ratio = 4.0f;
      params_.attackMs = 5.0f;
      params_.releaseMs = 80.0f;
      params_.kneeDb = 4.0f;
      break;
    case TrackRole::Drums:
      params_.thresholdDb = -12.0f;
      params_.ratio = 6.0f;
      params_.attackMs = 2.0f;
      params_.releaseMs = 40.0f;
      params_.kneeDb = 2.0f;
      break;
    case TrackRole::Bass808:
    case TrackRole::Bass:
      params_.thresholdDb = -20.0f;
      params_.ratio = 4.0f;
      params_.attackMs = 20.0f;
      params_.releaseMs = 200.0f;
      params_.kneeDb = 4.0f;
      break;
    case TrackRole::Piano:
    case TrackRole::Guitar:
      params_.thresholdDb = -18.0f;
      params_.ratio = 3.5f;
      params_.attackMs = 15.0f;
      params_.releaseMs = 150.0f;
      params_.kneeDb = 5.0f;
      break;
    case TrackRole::FXSend:
      params_.thresholdDb = -12.0f;
      params_.ratio = 2.0f;
      params_.attackMs = 30.0f;
      params_.releaseMs = 300.0f;
      params_.kneeDb = 8.0f;
      break;
    default:
      params_.thresholdDb = -18.0f;
      params_.ratio = 4.0f;
      params_.attackMs = 10.0f;
      params_.releaseMs = 120.0f;
      params_.kneeDb = 3.0f;
      break;
  }
  params_.makeupGainDb =
      computeAutoMakeupDb(params_.thresholdDb, params_.ratio);
  updateTimeConstants();
}

float CompressorProcessor::gainReductionDb() const {
  return gainReductionDb_;
}

void EqProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateFilters();
  reset();
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
    if (!band.enabled) {
      leftFilters_[index].setBypass();
      rightFilters_[index].setBypass();
      continue;
    }
    switch (band.type) {
      case BandType::LowShelf:
        leftFilters_[index].setLowShelf(band.frequency, band.q, band.gainDb,
                                        sampleRate_);
        rightFilters_[index].setLowShelf(band.frequency, band.q, band.gainDb,
                                         sampleRate_);
        break;
      case BandType::HighShelf:
        leftFilters_[index].setHighShelf(band.frequency, band.q, band.gainDb,
                                         sampleRate_);
        rightFilters_[index].setHighShelf(band.frequency, band.q, band.gainDb,
                                          sampleRate_);
        break;
      case BandType::LowCut:
        leftFilters_[index].setHighPass(band.frequency, band.q, sampleRate_);
        rightFilters_[index].setHighPass(band.frequency, band.q, sampleRate_);
        break;
      case BandType::HighCut:
        leftFilters_[index].setLowPass(band.frequency, band.q, sampleRate_);
        rightFilters_[index].setLowPass(band.frequency, band.q, sampleRate_);
        break;
      case BandType::Peak:
      default:
        leftFilters_[index].setPeaking(band.frequency, band.q, band.gainDb,
                                       sampleRate_);
        rightFilters_[index].setPeaking(band.frequency, band.q, band.gainDb,
                                        sampleRate_);
        break;
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

      // Apply mild saturation after each enabled band if saturation > 0
      const auto& band = params_.bands[bandIndex];
      if (band.enabled && band.saturation > 0.0f) {
        left = applySaturation(left, band.saturation);
        right = applySaturation(right, band.saturation);
      }
    }

    buffer.left[i] = left;
    buffer.right[i] = right;
  }
}

float EqProcessor::applySaturation(float x, float amount) const {
  if (amount <= 0.0f) return x;
  // Mild tape-style saturation with adjustable amount
  // At amount=0, no saturation; at amount=1, moderate harmonic color
  const float drive = 1.0f + amount * 0.5f;  // 1.0 to 1.5x drive
  return std::tanh(x * drive) / std::tanh(drive);
}

void EqProcessor::loadRolePreset(TrackRole role) {
  // Reset all bands to unity/disabled first.
  for (auto& band : params_.bands) {
    band = {1000.0f, 0.0f, 0.707f, BandType::Peak, false, 0.0f};
  }

  switch (role) {
    case TrackRole::LeadVocal:
      params_.bands[0] = {80.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {300.0f, -2.5f, 1.2f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {4000.0f, 2.5f, 1.5f, BandType::Peak, true, 0.0f};
      params_.bands[3] = {10000.0f, 2.0f, 0.707f, BandType::HighShelf, true, 0.0f};
      break;
    case TrackRole::AdLib:
      params_.bands[0] = {100.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {350.0f, -2.0f, 1.2f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {5000.0f, 2.0f, 1.2f, BandType::Peak, true, 0.0f};
      break;
    case TrackRole::Bass808:
    case TrackRole::Bass:
      params_.bands[0] = {30.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {80.0f, 2.0f, 1.0f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {250.0f, -3.0f, 1.2f, BandType::Peak, true, 0.0f};
      params_.bands[3] = {2500.0f, 1.5f, 1.0f, BandType::Peak, true, 0.0f};
      break;
    case TrackRole::Drums:
      params_.bands[0] = {60.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {200.0f, -2.0f, 1.0f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {5000.0f, 2.5f, 1.2f, BandType::Peak, true, 0.0f};
      params_.bands[3] = {12000.0f, 1.5f, 0.707f, BandType::HighShelf, true, 0.0f};
      break;
    case TrackRole::Piano:
      params_.bands[0] = {80.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {200.0f, -1.5f, 1.0f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {3000.0f, 1.5f, 1.0f, BandType::Peak, true, 0.0f};
      break;
    case TrackRole::Synth:
      params_.bands[0] = {60.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {500.0f, -1.5f, 1.0f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {8000.0f, 1.5f, 0.707f, BandType::HighShelf, true, 0.0f};
      break;
    case TrackRole::Guitar:
      params_.bands[0] = {100.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {300.0f, -2.0f, 1.0f, BandType::Peak, true, 0.0f};
      params_.bands[2] = {2500.0f, 2.0f, 1.2f, BandType::Peak, true, 0.0f};
      params_.bands[3] = {8000.0f, 1.0f, 0.707f, BandType::HighShelf, true, 0.0f};
      break;
    case TrackRole::FXSend:
      params_.bands[0] = {200.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {8000.0f, 0.0f, 0.707f, BandType::HighCut, true, 0.0f};
      break;
    default:
      // Generic: leave all bands disabled.
      break;
  }
  updateFilters();
}

void EqProcessor::loadClassicCurve(ClassicCurve curve) {
  // Reset all bands to unity/disabled first.
  for (auto& band : params_.bands) {
    band = {1000.0f, 0.0f, 0.707f, BandType::Peak, false, 0.0f};
  }
  params_.classicCurve = curve;

  switch (curve) {
    case ClassicCurve::Neve1073:
      // Neve 1073: Classic warm British console sound
      // Fixed frequency shelves with characteristic curves
      params_.bands[0] = {220.0f, 0.0f, 0.8f, BandType::LowShelf, true, 0.3f};
      params_.bands[1] = {360.0f, 0.0f, 1.2f, BandType::Peak, true, 0.2f};
      params_.bands[2] = {3200.0f, 0.0f, 1.0f, BandType::Peak, true, 0.2f};
      params_.bands[3] = {12000.0f, 0.0f, 0.7f, BandType::HighShelf, true, 0.3f};
      break;

    case ClassicCurve::API550:
      // API 550: Proportional Q design (narrow when boosting)
      // Three overlapping bands with musical frequencies
      params_.bands[0] = {100.0f, 0.0f, 0.7f, BandType::LowShelf, true, 0.25f};
      params_.bands[1] = {560.0f, 0.0f, 1.5f, BandType::Peak, true, 0.2f};
      params_.bands[2] = {3000.0f, 0.0f, 1.3f, BandType::Peak, true, 0.2f};
      params_.bands[3] = {8000.0f, 0.0f, 1.2f, BandType::Peak, true, 0.2f};
      params_.bands[4] = {15000.0f, 0.0f, 0.7f, BandType::HighShelf, true, 0.25f};
      break;

    case ClassicCurve::SSL4000:
      // SSL 4000: Clean, precise British console
      // Bell curves with tight Q for surgical work
      params_.bands[0] = {60.0f, 0.0f, 0.707f, BandType::LowCut, true, 0.0f};
      params_.bands[1] = {200.0f, 0.0f, 0.8f, BandType::LowShelf, true, 0.15f};
      params_.bands[2] = {600.0f, 0.0f, 1.5f, BandType::Peak, true, 0.1f};
      params_.bands[3] = {3000.0f, 0.0f, 1.4f, BandType::Peak, true, 0.1f};
      params_.bands[4] = {10000.0f, 0.0f, 0.8f, BandType::HighShelf, true, 0.15f};
      break;

    case ClassicCurve::Pultec:
      // Pultec EQP-1A: Tube-based passive EQ with gentle curves
      // Famous for simultaneous boost/cut at low end
      params_.bands[0] = {30.0f, 0.0f, 0.5f, BandType::LowShelf, true, 0.4f};
      params_.bands[1] = {100.0f, 0.0f, 0.6f, BandType::Peak, true, 0.3f};
      params_.bands[2] = {5000.0f, 0.0f, 0.9f, BandType::Peak, true, 0.3f};
      params_.bands[3] = {12000.0f, 0.0f, 0.6f, BandType::HighShelf, true, 0.4f};
      break;

    case ClassicCurve::None:
    default:
      // No classic curve, leave bands disabled
      break;
  }
  updateFilters();
}

float EqProcessor::computeMagnitudeDb(float frequency) const {
  if (sampleRate_ <= 0.0) return 0.0f;
  float totalDb = 0.0f;
  for (std::size_t i = 0; i < params_.bands.size(); ++i) {
    if (!params_.bands[i].enabled) continue;
    totalDb += leftFilters_[i].magnitudeResponseDb(frequency, sampleRate_);
  }
  return totalDb;
}

void LimiterProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  releaseCoeff_ = timeMsToCoeff(params_.releaseMs, sampleRate_);
  releaseCoeff2_ = timeMsToCoeff(params_.releaseMs2, sampleRate_);
  updateLookahead();
  reset();
}

void LimiterProcessor::setParams(const Params& params) {
  params_ = params;
  releaseCoeff_ = timeMsToCoeff(params_.releaseMs, sampleRate_);
  releaseCoeff2_ = timeMsToCoeff(params_.releaseMs2, sampleRate_);
  updateLookahead();
}

void LimiterProcessor::reset() {
  gain_ = 1.0f;
  gain2_ = 1.0f;
  gainReductionDb_ = 0.0f;
  std::fill(lookaheadLeft_.begin(), lookaheadLeft_.end(), 0.0f);
  std::fill(lookaheadRight_.begin(), lookaheadRight_.end(), 0.0f);
  lookaheadWriteIndex_ = 0;
}

void LimiterProcessor::updateLookahead() {
  if (sampleRate_ <= 0.0) {
    return;
  }
  const float ms = clamp(params_.lookaheadMs, 0.0f, 10.0f);
  lookaheadSamples_ =
      static_cast<std::size_t>(ms * 0.001f * static_cast<float>(sampleRate_));
  if (lookaheadSamples_ < 1) {
    lookaheadSamples_ = 1;
  }
  const std::size_t bufSize = lookaheadSamples_ + 1;
  if (lookaheadLeft_.size() != bufSize) {
    lookaheadLeft_.assign(bufSize, 0.0f);
    lookaheadRight_.assign(bufSize, 0.0f);
    lookaheadWriteIndex_ = 0;
  }
}

void LimiterProcessor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  const float threshold = dbToLinear(params_.thresholdDb);
  const float ceiling = dbToLinear(params_.ceilingDb);
  const std::size_t bufSize = lookaheadLeft_.size();

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    // Write current sample into lookahead buffer.
    lookaheadLeft_[lookaheadWriteIndex_] = buffer.left[i];
    lookaheadRight_[lookaheadWriteIndex_] = buffer.right[i];

    // Scan ahead in the lookahead window to find the future peak.
    float futurePeak = 0.0f;
    for (std::size_t j = 0; j < lookaheadSamples_; ++j) {
      const std::size_t idx = (lookaheadWriteIndex_ + bufSize - j) % bufSize;
      const float p = std::max(std::abs(lookaheadLeft_[idx]),
                               std::abs(lookaheadRight_[idx]));

      // Simple inter-sample true-peak estimation: average of adjacent samples.
      if (params_.truePeak && j > 0) {
        const std::size_t prev = (idx + 1) % bufSize;
        const float midL =
            0.5f * (lookaheadLeft_[idx] + lookaheadLeft_[prev]);
        const float midR =
            0.5f * (lookaheadRight_[idx] + lookaheadRight_[prev]);
        const float tp = std::max(std::abs(midL), std::abs(midR));
        if (tp > futurePeak) futurePeak = tp;
      }

      if (p > futurePeak) futurePeak = p;
    }

    float targetGain = 1.0f;
    if (futurePeak > threshold && futurePeak > 0.0f) {
      targetGain = threshold / futurePeak;
    }

    // Apply mode-specific time constant scaling
    float attackCoeff = 0.0f;  // Instant attack for transparent
    float releaseCoeff = releaseCoeff_;
    float releaseCoeff2 = releaseCoeff2_;

    if (params_.mode == Mode::Hardware) {
      // Hardware mode: slightly slower attack (vintage behavior)
      attackCoeff = 0.3f;
      releaseCoeff = releaseCoeff_ * 1.2f;  // Slower release
      releaseCoeff2 = releaseCoeff2_ * 1.2f;
    } else if (params_.mode == Mode::Digital) {
      // Digital mode: ultra-fast attack, precise
      attackCoeff = 0.0f;
      releaseCoeff = releaseCoeff_ * 0.8f;  // Faster release
      releaseCoeff2 = releaseCoeff2_ * 0.8f;
    }

    if (targetGain < gain_) {
      // Attack phase
      gain_ = attackCoeff * gain_ + (1.0f - attackCoeff) * targetGain;
    } else {
      gain_ = releaseCoeff * gain_ + (1.0f - releaseCoeff) * targetGain;
    }

    // Two-stage release: slow stage follows fast stage, preventing
    // overshoot during recovery from dense transients.
    if (gain_ < gain2_) {
      gain2_ = gain_;
    } else {
      gain2_ = releaseCoeff2 * gain2_ + (1.0f - releaseCoeff2) * gain_;
    }
    const float outputGain = std::min(gain_, gain2_);

    // Read delayed sample from lookahead buffer.
    const std::size_t readIdx =
        (lookaheadWriteIndex_ + bufSize - lookaheadSamples_ + 1) % bufSize;
    float outLeft = lookaheadLeft_[readIdx] * outputGain;
    float outRight = lookaheadRight_[readIdx] * outputGain;

    // Apply mode-specific saturation
    outLeft = applySaturation(outLeft, params_.mode);
    outRight = applySaturation(outRight, params_.mode);

    buffer.left[i] = clamp(outLeft, -ceiling, ceiling);
    buffer.right[i] = clamp(outRight, -ceiling, ceiling);

    lookaheadWriteIndex_ = (lookaheadWriteIndex_ + 1) % bufSize;
  }

  gainReductionDb_ = -linearToDb(std::min(gain_, gain2_));
}

float LimiterProcessor::applySaturation(float x, Mode mode) const {
  switch (mode) {
    case Mode::Hardware:
      // Soft saturation like analog hardware (tape-style)
      return std::tanh(x * 1.2f) * 0.9f;
    case Mode::Digital:
      // Clean digital limiting with minimal color
      return x;
    case Mode::Transparent:
    default:
      // Very subtle saturation for smoothness
      return std::tanh(x * 1.05f) * 0.98f;
  }
}

LimiterProcessor::Params LimiterProcessor::makeStreamingPreset(
    StreamingPreset preset) {
  Params p;
  switch (preset) {
    case StreamingPreset::Spotify:
      p.thresholdDb = -2.0f;
      p.ceilingDb = -1.0f;
      p.releaseMs = 100.0f;
      p.releaseMs2 = 500.0f;
      p.truePeak = true;
      break;
    case StreamingPreset::YouTube:
      p.thresholdDb = -2.5f;
      p.ceilingDb = -1.0f;
      p.releaseMs = 80.0f;
      p.releaseMs2 = 400.0f;
      p.truePeak = true;
      break;
    case StreamingPreset::AppleMusic:
      p.thresholdDb = -3.0f;
      p.ceilingDb = -1.0f;
      p.releaseMs = 120.0f;
      p.releaseMs2 = 600.0f;
      p.truePeak = true;
      break;
    default:
      break;
  }
  return p;
}

void Saturate3Processor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  updateSaturation();
  reset();
}

void Saturate3Processor::setParams(const Params& params) {
  params_ = params;
  updateSaturation();
}

void Saturate3Processor::reset() {
  crossoverLowLeft_.reset();
  crossoverLowRight_.reset();
  crossoverHighLeft_.reset();
  crossoverHighRight_.reset();
  oversampPrevReady_ = false;
  oversampLowPrevL_ = 0.0f;
  oversampLowPrevR_ = 0.0f;
  oversampMidPrevL_ = 0.0f;
  oversampMidPrevR_ = 0.0f;
  oversampHighPrevL_ = 0.0f;
  oversampHighPrevR_ = 0.0f;
}

void Saturate3Processor::updateSaturation() {
  lowDrive_ = dbToLinear(params_.low.driveDb);
  midDrive_ = dbToLinear(params_.mid.driveDb);
  highDrive_ = dbToLinear(params_.high.driveDb);
  wetMix_ = clamp(params_.mix, 0.0f, 1.0f);
  dryMix_ = 1.0f - wetMix_;
  crossoverLowLeft_.setCutoff(params_.lowCrossoverHz, sampleRate_);
  crossoverLowRight_.setCutoff(params_.lowCrossoverHz, sampleRate_);
  crossoverHighLeft_.setCutoff(params_.highCrossoverHz, sampleRate_);
  crossoverHighRight_.setCutoff(params_.highCrossoverHz, sampleRate_);
}

float Saturate3Processor::shapeTape(float x) {
  return std::tanh(x);
}

float Saturate3Processor::shapeTube(float x) {
  // Asymmetric soft clipping typical of tube saturation.
  if (x >= 0.0f) {
    return 1.0f - std::exp(-x);
  }
  return -1.0f + std::exp(x);
}

float Saturate3Processor::shapeTransformer(float x) {
  // Harder saturation with a cubic-based soft clip.
  if (x > 1.0f) return 1.0f;
  if (x < -1.0f) return -1.0f;
  return 1.5f * x - 0.5f * x * x * x;
}

void Saturate3Processor::process(AudioBufferView buffer) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0) {
    return;
  }

  auto applyShape = [](float x, Character c) -> float {
    switch (c) {
      case Character::Tube:
        return shapeTube(x);
      case Character::Transformer:
        return shapeTransformer(x);
      case Character::Clean:
        return x;
      case Character::Tape:
      default:
        return shapeTape(x);
    }
  };

  // 2× oversampling: linear-interpolate a midpoint sub-sample, process both
  // through the non-linear shaper, then average to decimate.
  // On the first sample after reset, prev is seeded from cur to avoid an
  // artificial transient caused by the 0-initialised state.
  const bool os = params_.oversample;
  auto applyShapeOS = [&](float cur, float& prev, float drive,
                          Character c) -> float {
    if (!os) {
      prev = cur;
      return applyShape(cur * drive, c);
    }
    if (!oversampPrevReady_) {
      prev = cur;  // Warm-up: no click on first sample after reset.
    }
    const float mid = (prev + cur) * 0.5f;
    const float out1 = applyShape(mid * drive, c);
    const float out2 = applyShape(cur * drive, c);
    prev = cur;
    return (out1 + out2) * 0.5f;
  };

  // Precompute per-band active flags once per block (mute/solo is block-constant).
  const bool anySoloed =
      params_.low.soloed || params_.mid.soloed || params_.high.soloed;
  const bool lowActive =
      !params_.low.muted && (!anySoloed || params_.low.soloed);
  const bool midActive =
      !params_.mid.muted && (!anySoloed || params_.mid.soloed);
  const bool highActive =
      !params_.high.muted && (!anySoloed || params_.high.soloed);

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft = buffer.left[i];
    const float inputRight = buffer.right[i];

    // Split into low + rest using the low crossover.
    float lowL = 0.0f, restL = 0.0f;
    float lowR = 0.0f, restR = 0.0f;
    crossoverLowLeft_.process(inputLeft, lowL, restL);
    crossoverLowRight_.process(inputRight, lowR, restR);

    // Split rest into mid + high using the high crossover.
    float midL = 0.0f, highL = 0.0f;
    float midR = 0.0f, highR = 0.0f;
    crossoverHighLeft_.process(restL, midL, highL);
    crossoverHighRight_.process(restR, midR, highR);

    // Apply per-band saturation with character-specific waveshaping.
    const float satLowL =
        applyShapeOS(lowL, oversampLowPrevL_, lowDrive_,
                     params_.low.character) *
        params_.low.mix;
    const float satLowR =
        applyShapeOS(lowR, oversampLowPrevR_, lowDrive_,
                     params_.low.character) *
        params_.low.mix;
    const float satMidL =
        applyShapeOS(midL, oversampMidPrevL_, midDrive_,
                     params_.mid.character) *
        params_.mid.mix;
    const float satMidR =
        applyShapeOS(midR, oversampMidPrevR_, midDrive_,
                     params_.mid.character) *
        params_.mid.mix;
    const float satHighL =
        applyShapeOS(highL, oversampHighPrevL_, highDrive_,
                     params_.high.character) *
        params_.high.mix;
    const float satHighR =
        applyShapeOS(highR, oversampHighPrevR_, highDrive_,
                     params_.high.character) *
        params_.high.mix;

    // Mark prev values valid after the first sample.
    oversampPrevReady_ = true;

    const float wetLeft = (lowActive ? satLowL : 0.0f) +
                          (midActive ? satMidL : 0.0f) +
                          (highActive ? satHighL : 0.0f);
    const float wetRight = (lowActive ? satLowR : 0.0f) +
                           (midActive ? satMidR : 0.0f) +
                           (highActive ? satHighR : 0.0f);

    buffer.left[i] = inputLeft * dryMix_ + wetLeft * wetMix_;
    buffer.right[i] = inputRight * dryMix_ + wetRight * wetMix_;
  }
}

void TapeDelayProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  const std::size_t maxSamples =
      static_cast<std::size_t>(std::max(1.0, sampleRate_ * kMaxDelayTimeSec));
  delayLeft_.assign(maxSamples, 0.0f);
  delayRight_.assign(maxSamples, 0.0f);
  lfo_.prepare(sampleRate);
  lfo_.setFrequency(0.5f);
  feedbackLpLeft_.setCutoff(params_.lowpassHz, sampleRate);
  feedbackLpRight_.setCutoff(params_.lowpassHz, sampleRate);
  feedbackHpLeft_.setCutoff(params_.highpassHz, sampleRate);
  feedbackHpRight_.setCutoff(params_.highpassHz, sampleRate);
  duckAttackCoeff_  = timeMsToCoeff(params_.duckAttackMs,  sampleRate);
  duckReleaseCoeff_ = timeMsToCoeff(params_.duckReleaseMs, sampleRate);
  updateDelaySamples();
  reset();
}

void TapeDelayProcessor::setParams(const Params& params) {
  params_ = params;
  feedbackLpLeft_.setCutoff(params_.lowpassHz, sampleRate_);
  feedbackLpRight_.setCutoff(params_.lowpassHz, sampleRate_);
  feedbackHpLeft_.setCutoff(params_.highpassHz, sampleRate_);
  feedbackHpRight_.setCutoff(params_.highpassHz, sampleRate_);
  duckAttackCoeff_  = timeMsToCoeff(params_.duckAttackMs,  sampleRate_);
  duckReleaseCoeff_ = timeMsToCoeff(params_.duckReleaseMs, sampleRate_);
  updateDelaySamples();
}

void TapeDelayProcessor::reset() {
  std::fill(delayLeft_.begin(), delayLeft_.end(), 0.0f);
  std::fill(delayRight_.begin(), delayRight_.end(), 0.0f);
  writeIndex_ = 0;
  duckEnvelope_ = 0.0f;
  lfo_.reset();
  feedbackLpLeft_.reset();
  feedbackLpRight_.reset();
  feedbackHpLeft_.reset();
  feedbackHpRight_.reset();
}

void TapeDelayProcessor::process(AudioBufferView buffer) {
  processWithSidechain(buffer, {nullptr, nullptr, 0});
}

void TapeDelayProcessor::processWithSidechain(AudioBufferView buffer,
                                               AudioBufferView sidechain) {
  if (!buffer.left || !buffer.right || buffer.numSamples == 0 ||
      delayLeft_.empty()) {
    return;
  }

  const float mix = clamp(params_.mix, 0.0f, 1.0f);
  constexpr float kMaxFeedbackForStability = 0.95f;
  const float feedback = clamp(params_.feedback, 0.0f, kMaxFeedbackForStability);
  const float wowAmount = clamp(params_.wowFlutter, 0.0f, 1.0f);
  constexpr float kMaxWowDepthMs = 3.0f;

  const float duckAmount = clamp(params_.duckAmount, 0.0f, 1.0f);
  const float duckThreshold = dbToLinear(params_.duckThresholdDb);

  const bool hasSidechain = sidechain.left && sidechain.right &&
                             sidechain.numSamples == buffer.numSamples;

  for (std::size_t i = 0; i < buffer.numSamples; ++i) {
    const float inputLeft  = buffer.left[i];
    const float inputRight = buffer.right[i];

    // --- Duck envelope detector -------------------------------------------
    // Uses sidechain peak when provided, otherwise uses the dry input level.
    const float detL = hasSidechain ? sidechain.left[i]  : inputLeft;
    const float detR = hasSidechain ? sidechain.right[i] : inputRight;
    const float detPeak = std::max(std::abs(detL), std::abs(detR));
    const float targetEnv = detPeak > duckThreshold ? 1.0f : 0.0f;
    if (targetEnv > duckEnvelope_) {
      duckEnvelope_ = duckAttackCoeff_  * duckEnvelope_ +
                      (1.0f - duckAttackCoeff_)  * targetEnv;
    } else {
      duckEnvelope_ = duckReleaseCoeff_ * duckEnvelope_ +
                      (1.0f - duckReleaseCoeff_) * targetEnv;
    }
    // wetScale: 1 when no ducking, (1-duckAmount) at full duck.
    const float wetScale = 1.0f - duckAmount * duckEnvelope_;

    // --- Delay read --------------------------------------------------------
    const float lfoVal = lfo_.next();
    const float modOffsetSamples =
        wowAmount * kMaxWowDepthMs * 0.001f *
        static_cast<float>(sampleRate_) * lfoVal;
    const float effectiveDelay =
        static_cast<float>(delaySamples_) + modOffsetSamples;

    const float delayedLeft  = readInterpolated(delayLeft_,  effectiveDelay);
    const float delayedRight = readInterpolated(delayRight_, effectiveDelay);

    buffer.left[i]  = inputLeft  * (1.0f - mix) + delayedLeft  * mix * wetScale;
    buffer.right[i] = inputRight * (1.0f - mix) + delayedRight * mix * wetScale;
    // Note: the dry signal is intentionally kept at full level while only the
    // wet (echo) signal is attenuated.  This is the desired ducking behaviour
    // for vocals: the voice stays clear, the echoes duck into the background.

    // --- Feedback path (tone shaping) -------------------------------------
    float fbLeft  = feedbackLpLeft_.process(delayedLeft);
    fbLeft        = feedbackHpLeft_.process(fbLeft);
    float fbRight = feedbackLpRight_.process(delayedRight);
    fbRight       = feedbackHpRight_.process(fbRight);

    const float feedbackLeft  = inputLeft  + fbLeft  * feedback;
    const float feedbackRight = inputRight + fbRight * feedback;

    if (params_.pingPong) {
      delayLeft_[writeIndex_]  = feedbackRight;
      delayRight_[writeIndex_] = feedbackLeft;
    } else {
      delayLeft_[writeIndex_]  = feedbackLeft;
      delayRight_[writeIndex_] = feedbackRight;
    }

    writeIndex_ = (writeIndex_ + 1) % delayLeft_.size();
  }
}
void TapeDelayProcessor::updateDelaySamples() {
  if (delayLeft_.empty()) {
    delaySamples_ = 1;
    return;
  }

  // When tempo sync is active, derive delay time from BPM and beat division.
  constexpr float kMsPerMinute = 60000.0f;
  constexpr float kMinBeatDivision = 0.001f;
  float timeMs;
  if (params_.tempoSync && params_.bpm > 0.0f) {
    timeMs = (kMsPerMinute / params_.bpm) *
             std::max(kMinBeatDivision, params_.beatDivision);
  } else {
    timeMs = params_.timeMs;
  }

  const float clampedTime = clamp(timeMs, 1.0f, kMaxDelayTimeMs);
  const std::size_t desired =
      static_cast<std::size_t>(clampedTime * 0.001f * sampleRate_);
  const std::size_t maxDelaySamples =
      delayLeft_.size() > 1 ? delayLeft_.size() - 1 : static_cast<std::size_t>(1);
  delaySamples_ = std::min(desired, maxDelaySamples);
  if (delaySamples_ == 0) {
    delaySamples_ = 1;
  }
}

float TapeDelayProcessor::readInterpolated(const std::vector<float>& buffer,
                                           float delaySamplesF) const {
  const std::size_t bufSize = buffer.size();
  if (bufSize == 0) return 0.0f;
  const float maxDelay = static_cast<float>(bufSize - 1);
  delaySamplesF = clamp(delaySamplesF, 0.0f, maxDelay);
  const std::size_t idx0 =
      (writeIndex_ + bufSize -
       static_cast<std::size_t>(delaySamplesF)) %
      bufSize;
  const std::size_t idx1 = (idx0 + bufSize - 1) % bufSize;
  const float frac = delaySamplesF - std::floor(delaySamplesF);
  return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
}

void ConvolutionReverbProcessor::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  applyDecayToImpulse();
  resizeHistory();
  reset();
}

void ConvolutionReverbProcessor::setParams(const Params& params) {
  params_ = params;
  applyDecayToImpulse();
  resizeHistory();
  // Update damping filter: higher damping = lower cutoff (darker tail).
  const float dampFreq =
      clamp(20000.0f * (1.0f - params_.damping * 0.9f), 200.0f, 20000.0f);
  dampingLeft_.setCutoff(dampFreq, sampleRate_);
  dampingRight_.setCutoff(dampFreq, sampleRate_);
}

void ConvolutionReverbProcessor::setImpulse(std::vector<float> impulse) {
  if (impulse.empty()) {
    impulse = {1.0f};
  }
  impulseRaw_ = std::move(impulse);
  applyDecayToImpulse();
  resizeHistory();
}

void ConvolutionReverbProcessor::reset() {
  std::fill(historyLeft_.begin(), historyLeft_.end(), 0.0f);
  std::fill(historyRight_.begin(), historyRight_.end(), 0.0f);
  writeIndex_ = 0;
  dampingLeft_.reset();
  dampingRight_.reset();
}

void ConvolutionReverbProcessor::applyDecayToImpulse() {
  impulse_ = impulseRaw_;
  const float decay = clamp(params_.decay, 0.01f, 4.0f);
  for (std::size_t i = 0; i < impulse_.size(); ++i) {
    const float t = static_cast<float>(i) /
                    static_cast<float>(std::max<std::size_t>(impulse_.size(), 1));
    impulse_[i] *= std::exp(-3.0f * t / decay);
  }
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

    // Apply damping filter to the wet reverb tail.
    sumLeft = dampingLeft_.process(sumLeft);
    sumRight = dampingRight_.process(sumRight);

    buffer.left[i] = inputLeft * (1.0f - mix) + sumLeft * mix;
    buffer.right[i] = inputRight * (1.0f - mix) + sumRight * mix;

    writeIndex_ = (writeIndex_ + 1) % historySize;
  }
}

}  // namespace tap
