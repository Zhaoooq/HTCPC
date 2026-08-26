#include "algorithms/OpcCounter.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool nearlyEqual(double actual, double expected, double tolerance = 1e-9) {
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    QVector<double> thresholdTime;
    QVector<double> thresholdVoltage;
    for (int i = 0; i < 100; ++i) {
        thresholdTime.append(i * 0.001);
        thresholdVoltage.append(1.0);
    }
    OpcParams thresholdParams;
    thresholdParams.minRange = 0.008;
    thresholdParams.thresholdOffset = 0.002;
    const OpcCountResult thresholdResult =
        analyzeOpcPulseSignal(thresholdTime, thresholdVoltage, thresholdParams);
    assert(thresholdResult.adaptiveThresholdValid);
    assert(nearlyEqual(thresholdResult.currentBaseline, 1.0));
    assert(nearlyEqual(thresholdResult.currentNoiseRange, 0.010));
    assert(nearlyEqual(thresholdResult.currentThreshold, 1.010));
    assert(nearlyEqual(estimateChunkDurationSeconds(thresholdTime), 0.1));

    QVector<double> overlappingTime;
    QVector<double> overlappingVoltage(200, 0.0);
    for (int i = 0; i < overlappingVoltage.size(); ++i) {
        overlappingTime.append(i * 1e-6);
    }
    const double overlappingPulse[] = {
        0.012, 0.025, 0.045, 0.070, 0.090, 0.100, 0.097, 0.092, 0.090,
        0.093, 0.098, 0.096, 0.091, 0.089, 0.092, 0.097, 0.095, 0.090,
        0.088, 0.091, 0.096, 0.090, 0.075, 0.055, 0.030, 0.012
    };
    for (int i = 0; i < 26; ++i) {
        overlappingVoltage[70 + i] = overlappingPulse[i];
    }
    const OpcCountResult overlappingResult =
        analyzeOpcPulseSignal(overlappingTime, overlappingVoltage, OpcParams());
    assert(overlappingResult.segmentCount == 1);
    assert(overlappingResult.totalCount == 4);
    assert(overlappingResult.valleySplitExtraCount == 3);
    assert(overlappingResult.peakTimes.size() == 4);

    ParticleCalibrationParams calibration;
    assert(nearlyEqual(applyParticleCountCalibration(125.0, calibration), 125.0));

    calibration.a = 0.5;
    calibration.b = 2.0;
    calibration.c = 3.0;
    assert(nearlyEqual(applyParticleCountCalibration(4.0, calibration), 19.0));

    calibration.a = 0.0;
    calibration.b = 1.0;
    calibration.c = -10.0;
    assert(nearlyEqual(applyParticleCountCalibration(4.0, calibration), 0.0));

    assert(std::isnan(applyParticleCountCalibration(-1.0, calibration)));
    assert(std::isnan(applyParticleCountCalibration(
        std::numeric_limits<double>::quiet_NaN(), calibration)));

    calibration.a = std::numeric_limits<double>::infinity();
    assert(std::isnan(applyParticleCountCalibration(4.0, calibration)));

    ParticleCountRateAccumulator accumulator(1.0);
    double countRate = -1.0;
    assert(!accumulator.addChunk(2, 0.4, countRate));
    assert(!accumulator.addChunk(3, 0.4, countRate));
    assert(accumulator.accumulatedCount() == 5);
    assert(nearlyEqual(accumulator.accumulatedDurationSeconds(), 0.8));
    assert(accumulator.addChunk(5, 0.2, countRate));
    assert(nearlyEqual(countRate, 10.0));
    assert(accumulator.accumulatedCount() == 0);
    assert(nearlyEqual(accumulator.accumulatedDurationSeconds(), 0.0));

    assert(!accumulator.addChunk(10, 0.6, countRate));
    assert(accumulator.addChunk(5, 0.6, countRate));
    assert(nearlyEqual(countRate, 12.5));

    assert(!accumulator.addChunk(-1, 1.0, countRate));
    assert(!accumulator.addChunk(1, 0.0, countRate));
    assert(accumulator.accumulatedCount() == 0);
    return 0;
}
