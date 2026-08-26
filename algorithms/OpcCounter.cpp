#include "OpcCounter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

double clampValue(double x, double low, double high) {
    return std::max(low, std::min(x, high));
}

double quantileFromSorted(const std::vector<double>& sortedValues, double quantile) {
    if (sortedValues.empty()) return 0.0;
    quantile = clampValue(quantile, 0.0, 1.0);
    int index = static_cast<int>(quantile * (sortedValues.size() - 1));
    index = std::max(0, std::min(index, static_cast<int>(sortedValues.size()) - 1));
    return sortedValues[index];
}

double estimateSamplingRate(const QVector<double>& time, int n) {
    if (n < 2) return 0.0;

    std::vector<double> dt;
    dt.reserve(n - 1);

    for (int i = 1; i < n; ++i) {
        double d = time.at(i) - time.at(i - 1);
        if (d > 0.0 && std::isfinite(d)) dt.push_back(d);
    }

    if (dt.empty()) return 0.0;
    std::sort(dt.begin(), dt.end());

    double medianDt = dt[dt.size() / 2];
    return medianDt > 0.0 ? 1.0 / medianDt : 0.0;
}

void computeAdaptiveThreshold(
    const std::vector<double>& signal,
    double fs,
    const OpcParams& params,
    std::vector<double>& baseline,
    std::vector<double>& noiseRange,
    std::vector<double>& threshold
) {
    int n = static_cast<int>(signal.size());
    baseline.assign(n, 0.0);
    noiseRange.assign(n, 0.0);
    threshold.assign(n, 0.0);

    if (n == 0 || fs <= 0.0) return;

    int windowSamples = static_cast<int>(fs * params.windowMs / 1000.0);
    windowSamples = std::max(windowSamples, 16);

    double lastBase = 0.0;
    double lastRange = 0.0;
    bool hasLast = false;

    for (int start = 0; start < n; start += windowSamples) {
        int end = std::min(n, start + windowSamples);
        std::vector<double> seg;
        seg.reserve(end - start);

        for (int i = start; i < end; ++i) {
            seg.push_back(signal[i]);
        }

        std::sort(seg.begin(), seg.end());

        double qBase = quantileFromSorted(seg, params.noiseQuantile);
        double qMedian = quantileFromSorted(seg, 0.50);
        double range = params.rangeGain * std::max(0.0, qMedian - qBase);
        range = clampValue(range, params.minRange, params.maxRange);
        range = std::max(0.0, range + params.thresholdOffset);
        double base = qBase;

        if (hasLast) {
            base = params.thresholdAlpha * base + (1.0 - params.thresholdAlpha) * lastBase;
            range = params.thresholdAlpha * range + (1.0 - params.thresholdAlpha) * lastRange;
        }

        double th = base + range;
        lastBase = base;
        lastRange = range;
        hasLast = true;

        for (int i = start; i < end; ++i) {
            baseline[i] = base;
            noiseRange[i] = range;
            threshold[i] = th;
        }
    }
}

std::vector<OpcPulseSegment> findPulseSegments(
    const QVector<double>& time,
    const std::vector<double>& signal,
    const std::vector<double>& threshold,
    double fs,
    const OpcParams& params
) {
    std::vector<OpcPulseSegment> segments;
    int n = static_cast<int>(signal.size());

    if (n == 0 || fs <= 0.0) return segments;

    int minSegmentSamples =
        std::max(1, static_cast<int>(fs * params.minSegmentWidthUs * 1e-6));
    int bridgeGapSamples =
        std::max(1, static_cast<int>(fs * params.bridgeGapUs * 1e-6));

    std::vector<int> above(n, 0);
    for (int i = 0; i < n; ++i) {
        if (signal[i] > threshold[i]) above[i] = 1;
    }

    int i = 0;
    while (i < n) {
        if (above[i] == 0) {
            int gapStart = i;
            while (i < n && above[i] == 0) ++i;
            int gapEnd = i - 1;
            int gapLen = gapEnd - gapStart + 1;
            int left = gapStart - 1;
            int right = gapEnd + 1;

            if (left >= 0 && right < n &&
                above[left] == 1 && above[right] == 1 &&
                gapLen <= bridgeGapSamples) {
                for (int k = gapStart; k <= gapEnd; ++k) above[k] = 1;
            }
        } else {
            ++i;
        }
    }

    i = 0;
    int segId = 0;

    while (i < n) {
        if (above[i] == 1) {
            int left = i;
            while (i < n && above[i] == 1) ++i;

            int right = i - 1;
            int widthSamples = right - left + 1;
            double widthUs = (time.at(right) - time.at(left)) * 1e6;

            if (widthSamples >= minSegmentSamples && widthUs >= params.minSegmentWidthUs) {
                OpcPulseSegment seg;
                seg.id = segId++;
                seg.left = left;
                seg.right = right;
                seg.widthUs = widthUs;
                segments.push_back(seg);
            }
        } else {
            ++i;
        }
    }

    return segments;
}

double localMinimum(const std::vector<double>& x, int left, int right) {
    int n = static_cast<int>(x.size());
    left = std::max(left, 0);
    right = std::min(right, n - 1);

    double m = std::numeric_limits<double>::infinity();

    for (int i = left; i <= right; ++i) {
        if (x[i] < m) m = x[i];
    }

    return m;
}

std::vector<int> findLocalPeaksInSegment(
    const std::vector<double>& signal,
    const std::vector<double>& threshold,
    int left,
    int right
) {
    std::vector<int> candidates;
    int n = static_cast<int>(signal.size());

    left = std::max(left, 1);
    right = std::min(right, n - 2);

    for (int i = left; i <= right; ++i) {
        bool isLocalMax = signal[i] >= signal[i - 1] && signal[i] > signal[i + 1];
        if (isLocalMax && signal[i] > threshold[i]) candidates.push_back(i);
    }

    return candidates;
}

std::vector<int> removeWeakPeaksByProminence(
    const std::vector<int>& peaks,
    const std::vector<double>& signal,
    const std::vector<double>& noiseRange,
    int segmentLeft,
    int segmentRight,
    const OpcParams& params
) {
    std::vector<int> valid;

    for (int idx : peaks) {
        double leftMin = localMinimum(signal, segmentLeft, idx);
        double rightMin = localMinimum(signal, idx, segmentRight);
        double nearValley = std::max(leftMin, rightMin);
        double prominence = signal[idx] - nearValley;
        double requiredProminence = params.minPeakProminenceFactor * noiseRange[idx];

        if (prominence >= requiredProminence) valid.push_back(idx);
    }

    return valid;
}

int findMaxIndexInSegment(const std::vector<double>& signal, int left, int right) {
    int maxIndex = left;

    for (int i = left; i <= right; ++i) {
        if (signal[i] > signal[maxIndex]) maxIndex = i;
    }

    return maxIndex;
}

double estimateReferencePulseWidthUs(const std::vector<OpcPulseSegment>& segments) {
    std::vector<double> widths;

    for (const auto& seg : segments) {
        if (seg.count == 1 && !seg.overlapped && seg.widthUs > 0.0) {
            widths.push_back(seg.widthUs);
        }
    }

    if (widths.empty()) return 0.0;
    std::sort(widths.begin(), widths.end());
    return widths[widths.size() / 2];
}

} // namespace

OpcCountResult analyzeOpcPulseSignal(
    const QVector<double>& time,
    const QVector<double>& voltage,
    const OpcParams& params
) {
    OpcCountResult result;
    int n = std::min(time.size(), voltage.size());

    if (n < 3) return result;

    double fs = estimateSamplingRate(time, n);
    if (fs <= 0.0) return result;

    std::vector<double> signal;
    signal.reserve(n);

    for (int i = 0; i < n; ++i) {
        signal.push_back(voltage.at(i));
    }

    std::vector<double> baseline;
    std::vector<double> noiseRange;
    std::vector<double> threshold;
    computeAdaptiveThreshold(signal, fs, params, baseline, noiseRange, threshold);

    // The last sample belongs to the newest adaptive-threshold window. Expose
    // its values so the UI can show what the counter is actually using without
    // feeding the calculated threshold back into the operator settings.
    if (!baseline.empty() && !noiseRange.empty() && !threshold.empty()) {
        result.adaptiveThresholdValid = true;
        result.currentBaseline = baseline.back();
        result.currentNoiseRange = noiseRange.back();
        result.currentThreshold = threshold.back();
    }

    std::vector<OpcPulseSegment> segments =
        findPulseSegments(time, signal, threshold, fs, params);

    result.segmentCount = static_cast<int>(segments.size());

    for (auto& seg : segments) {
        std::vector<int> candidates =
            findLocalPeaksInSegment(signal, threshold, seg.left, seg.right);
        // Do not collapse neighbouring local maxima into a single particle.
        // Overlapping pulses can share one above-threshold segment and have a
        // shallow valley, but each peak that passes the prominence check must
        // still contribute to the particle count.
        std::vector<int> validPeaks = removeWeakPeaksByProminence(
            candidates, signal, noiseRange, seg.left, seg.right, params);

        if (validPeaks.empty()) {
            validPeaks.push_back(findMaxIndexInSegment(signal, seg.left, seg.right));
        }

        seg.count = static_cast<int>(validPeaks.size());

        if (seg.count > 1) {
            seg.overlapped = true;
            result.valleySplitExtraCount += seg.count - 1;
        }

        seg.mainPeakIndex = validPeaks.front();

        for (int idx : validPeaks) {
            if (signal[idx] > signal[seg.mainPeakIndex]) seg.mainPeakIndex = idx;
            result.peakTimes.append(time.at(idx));
            result.peakVoltages.append(signal[idx]);
        }
    }

    result.referenceWidthUs = estimateReferencePulseWidthUs(segments);

    if (params.enableWidthCorrection && result.referenceWidthUs > 0.0) {
        for (auto& seg : segments) {
            if (seg.count == 1 &&
                seg.widthUs > params.overlapWidthFactor * result.referenceWidthUs) {
                int estimatedCount =
                    static_cast<int>(std::round(seg.widthUs / result.referenceWidthUs));
                estimatedCount = std::max(1, std::min(params.maxOverlapCount, estimatedCount));

                if (estimatedCount > 1) {
                    result.widthCorrectionExtraCount += estimatedCount - 1;
                    seg.count = estimatedCount;
                }
            }
        }
    }

    for (const auto& seg : segments) {
        result.totalCount += seg.count;
    }

    double duration = fs > 0.0
        ? static_cast<double>(n) / fs
        : time.at(n - 1) - time.at(0);
    result.countRate = duration > 0.0 ? result.totalCount / duration : 0.0;

    return result;
}

double estimateChunkDurationSeconds(const QVector<double>& time) {
    int n = time.size();
    if (n < 2) return 0.0;

    double fs = estimateSamplingRate(time, n);
    if (fs > 0.0) return static_cast<double>(n) / fs;

    double duration = time.at(n - 1) - time.at(0);
    return duration > 0.0 && std::isfinite(duration) ? duration : 0.0;
}

ParticleCountRateAccumulator::ParticleCountRateAccumulator(double targetDurationSeconds)
    : targetDurationSeconds_(
          std::isfinite(targetDurationSeconds) && targetDurationSeconds > 0.0
              ? targetDurationSeconds
              : 1.0) {}

void ParticleCountRateAccumulator::reset() {
    accumulatedCount_ = 0;
    accumulatedDurationSeconds_ = 0.0;
}

bool ParticleCountRateAccumulator::addChunk(
    int particleCount,
    double durationSeconds,
    double& countRate
) {
    if (particleCount < 0 || !std::isfinite(durationSeconds) || durationSeconds <= 0.0) {
        return false;
    }

    accumulatedCount_ += particleCount;
    accumulatedDurationSeconds_ += durationSeconds;
    if (accumulatedDurationSeconds_ < targetDurationSeconds_) return false;

    countRate = static_cast<double>(accumulatedCount_) / accumulatedDurationSeconds_;
    reset();
    return std::isfinite(countRate);
}

long long ParticleCountRateAccumulator::accumulatedCount() const {
    return accumulatedCount_;
}

double ParticleCountRateAccumulator::accumulatedDurationSeconds() const {
    return accumulatedDurationSeconds_;
}

double applyParticleCountCalibration(
    double rawCountRate,
    const ParticleCalibrationParams& calibration
) {
    if (!std::isfinite(rawCountRate) || rawCountRate < 0.0 ||
        !std::isfinite(calibration.a) || !std::isfinite(calibration.b) ||
        !std::isfinite(calibration.c)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double calibratedValue =
        calibration.a * rawCountRate * rawCountRate +
        calibration.b * rawCountRate + calibration.c;
    if (!std::isfinite(calibratedValue)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::max(0.0, calibratedValue);
}
