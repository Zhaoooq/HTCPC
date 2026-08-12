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
    QVector<double> peakTimes;
    QVector<double> peakVoltages;
};

OpcCountResult analyzeOpcPulseSignal(
    const QVector<double>& time,
    const QVector<double>& voltage,
    const OpcParams& params
);

double estimateChunkDurationSeconds(const QVector<double>& time);

#endif // CPC_ALGORITHMS_OPCCOUNTER_H
