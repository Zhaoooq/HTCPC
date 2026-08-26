#ifndef CPC_ALGORITHMS_OPCCOUNTER_H
#define CPC_ALGORITHMS_OPCCOUNTER_H

#include <QVector>

struct OpcParams {
    double windowMs = 20.0;
    double thresholdOffset = 0.0;
    double noiseQuantile = 0.30;
    double rangeGain = 3.0;
    double minRange = 0.008;
    double maxRange = 0.03;
    double thresholdAlpha = 0.25;
    double minSegmentWidthUs = 8.0;
    double bridgeGapUs = 8.0;
    double splitPeakDistanceUs = 20.0;
    double valleyDropFactor = 0.6;
    double valleyDropAmplitudeRatio = 0.30;
    double minPeakProminenceFactor = 1.0;
    bool enableWidthCorrection = false;
    double overlapWidthFactor = 1.8;
    int maxOverlapCount = 5;
};

struct ParticleCalibrationParams {
    double a = 0.0;
    double b = 1.0;
    double c = 0.0;
};

class ParticleCountRateAccumulator {
public:
    explicit ParticleCountRateAccumulator(double targetDurationSeconds = 1.0);

    void reset();
    bool addChunk(int particleCount, double durationSeconds, double& countRate);

    long long accumulatedCount() const;
    double accumulatedDurationSeconds() const;

private:
    double targetDurationSeconds_ = 1.0;
    long long accumulatedCount_ = 0;
    double accumulatedDurationSeconds_ = 0.0;
};

struct OpcPulseSegment {
    int id = -1;
    int left = -1;
    int right = -1;
    double widthUs = 0.0;
    int count = 1;
    int mainPeakIndex = -1;
    bool overlapped = false;
};

struct OpcCountResult {
    int totalCount = 0;
    int segmentCount = 0;
    int valleySplitExtraCount = 0;
    int widthCorrectionExtraCount = 0;
    double countRate = 0.0;
    double referenceWidthUs = 0.0;
    bool adaptiveThresholdValid = false;
    double currentBaseline = 0.0;
    double currentNoiseRange = 0.0;
    double currentThreshold = 0.0;
    QVector<double> peakTimes;
    QVector<double> peakVoltages;
};

OpcCountResult analyzeOpcPulseSignal(
    const QVector<double>& time,
    const QVector<double>& voltage,
    const OpcParams& params
);

double estimateChunkDurationSeconds(const QVector<double>& time);

// x 为 OPC 识别得到的原始颗粒计数速率（个/s），返回标定后的计数速率。
// 二次标定公式：y = a*x^2 + b*x + c；颗粒数不允许为负，因此结果下限为 0。
double applyParticleCountCalibration(
    double rawCountRate,
    const ParticleCalibrationParams& calibration
);

#endif // CPC_ALGORITHMS_OPCCOUNTER_H
