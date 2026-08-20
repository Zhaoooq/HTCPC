#include "PressureValveController.h"

#include <algorithm>
#include <cmath>

namespace {

double clamp(double value, double minimum, double maximum) {
    return std::max(minimum, std::min(maximum, value));
}

} // namespace

void PressureValveController::setParameters(const Parameters& parameters) {
    parameters_ = parameters;
    parameters_.kp = std::max(0.0, parameters_.kp);
    parameters_.ki = std::max(0.0, parameters_.ki);
    parameters_.deadbandPercentOfFullScale =
        std::max(0.0, parameters_.deadbandPercentOfFullScale);
    parameters_.maxStepPercent =
        clamp(parameters_.maxStepPercent, 0.1, 100.0);
}

void PressureValveController::reset(double openingPercent) {
    openingPercent_ = clamp(openingPercent, 0.0, 100.0);
    previousActionErrorPercent_ = 0.0;
    initialized_ = true;
}

double PressureValveController::update(double targetPressurePa,
                                       double measuredPressurePa,
                                       double fullScalePressurePa,
                                       double elapsedSeconds,
                                       bool openingRaisesPressure) {
    if (!initialized_) reset(openingPercent_);
    if (!std::isfinite(targetPressurePa) ||
        !std::isfinite(measuredPressurePa) ||
        !std::isfinite(fullScalePressurePa) ||
        !std::isfinite(elapsedSeconds) ||
        fullScalePressurePa <= 0.0 || elapsedSeconds <= 0.0) {
        return openingPercent_;
    }

    double errorPercent =
        (targetPressurePa - measuredPressurePa) * 100.0 / fullScalePressurePa;
    if (std::abs(errorPercent) <= parameters_.deadbandPercentOfFullScale) {
        errorPercent = 0.0;
    }

    const double actionErrorPercent = openingRaisesPressure
        ? errorPercent
        : -errorPercent;
    const double proportionalChange =
        parameters_.kp * (actionErrorPercent - previousActionErrorPercent_);
    const double integralChange =
        parameters_.ki * actionErrorPercent * elapsedSeconds;
    const double requestedChange = clamp(proportionalChange + integralChange,
                                         -parameters_.maxStepPercent,
                                         parameters_.maxStepPercent);

    openingPercent_ = clamp(openingPercent_ + requestedChange, 0.0, 100.0);
    previousActionErrorPercent_ = actionErrorPercent;
    return openingPercent_;
}

double PressureValveController::openingPercent() const {
    return openingPercent_;
}
