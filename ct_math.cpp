#include "ct_math.h"

#include "config.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

uint32_t saturatedClippingCount(const CtAccumulator& accumulator) {
  const uint64_t clippingCount = static_cast<uint64_t>(accumulator.nearZeroSamples) +
                                 static_cast<uint64_t>(accumulator.nearHighSamples) +
                                 static_cast<uint64_t>(accumulator.nearFullScaleSamples);
  return clippingCount > std::numeric_limits<uint32_t>::max()
             ? std::numeric_limits<uint32_t>::max()
             : static_cast<uint32_t>(clippingCount);
}

CtSampleResult invalidResult(const CtAccumulator& accumulator, CtQuality quality) {
  CtSampleResult result{};
  result.sampleCount = accumulator.count;
  result.readErrors = accumulator.readErrors;
  result.clippingCount = saturatedClippingCount(accumulator);
  result.meanMv = static_cast<float>(accumulator.meanMv);
  result.minMv = accumulator.minMv;
  result.maxMv = accumulator.maxMv;
  result.quality = quality;
  result.valid = false;
  return result;
}

bool isValidGain(float calibrationGain) {
  return std::isfinite(calibrationGain) && calibrationGain >= CT_CALIBRATION_GAIN_MIN &&
         calibrationGain <= CT_CALIBRATION_GAIN_MAX;
}

}  // namespace

void ctResetAccumulator(CtAccumulator& accumulator) {
  accumulator = {};
}

bool ctAddSample(CtAccumulator& accumulator, float millivolts) {
  if (!std::isfinite(millivolts)) {
    return false;
  }

  const uint32_t nextCount = accumulator.count + 1U;
  if (nextCount == 0U) {
    return false;
  }

  const double sample = static_cast<double>(millivolts);
  const double delta = sample - accumulator.meanMv;
  accumulator.meanMv += delta / static_cast<double>(nextCount);
  const double deltaAfterMeanUpdate = sample - accumulator.meanMv;
  accumulator.m2 += delta * deltaAfterMeanUpdate;
  accumulator.count = nextCount;

  if (nextCount == 1U) {
    accumulator.minMv = millivolts;
    accumulator.maxMv = millivolts;
  } else {
    if (millivolts < accumulator.minMv) {
      accumulator.minMv = millivolts;
    }
    if (millivolts > accumulator.maxMv) {
      accumulator.maxMv = millivolts;
    }
  }
  return true;
}

CtSampleResult ctFinishWindow(const CtAccumulator& accumulator, float calibrationGain) {
  const uint32_t clippingCount = saturatedClippingCount(accumulator);
  if (accumulator.count < CT_MIN_VALID_SAMPLES || !isValidGain(calibrationGain)) {
    return invalidResult(accumulator, CtQuality::SensorError);
  }
  if (clippingCount != 0U) {
    return invalidResult(accumulator, CtQuality::Degraded);
  }

  double variance = accumulator.m2 / static_cast<double>(accumulator.count);
  if (variance < 0.0) {
    variance = 0.0;
  }
  const float rmsMv = static_cast<float>(std::sqrt(variance));
  CtSampleResult result{};
  result.rmsMv = rmsMv;
  result.currentA = (rmsMv / 1000.0f) * CT_CURRENT_A_PER_V_RMS * calibrationGain;
  result.meanMv = static_cast<float>(accumulator.meanMv);
  result.minMv = accumulator.minMv;
  result.maxMv = accumulator.maxMv;
  result.sampleCount = accumulator.count;
  result.readErrors = accumulator.readErrors;
  result.clippingCount = clippingCount;
  result.quality = CtQuality::Ok;
  result.valid = true;
  return result;
}
