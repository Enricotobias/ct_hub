#pragma once

#include "ct_types.h"

void ctResetAccumulator(CtAccumulator& accumulator);
bool ctAddSample(CtAccumulator& accumulator, float millivolts);
CtSampleResult ctFinishWindow(const CtAccumulator& accumulator, float calibrationGain);
