#ifndef CPC_CONTROL_TEMPERATUREPID_H
#define CPC_CONTROL_TEMPERATUREPID_H

#include <algorithm>
#include <cmath>

class HybridCoolingPID {
public:
    double target = 10.0;
    double prev_temp = 25.0;
    double integral = 0.0;
    bool is_first_run = true;
    double kp = 20.0;
    double ki = 1.5;
    double kd = 40.0;

    void reset() {
        integral = 0.0;
        is_first_run = true;
    }

    void setTunings(double proportional, double integralGain, double derivativeGain) {
        kp = std::max(0.0, proportional);
        ki = std::max(0.0, integralGain);
        kd = std::max(0.0, derivativeGain);
        // Keep the latest temperature history so a live tuning change does not
        // create a derivative kick; only accumulated integral action is cleared.
        integral = 0.0;
    }

    double compute(double cur, double dt) {
        if (!std::isfinite(cur) || !std::isfinite(dt) || dt <= 0.0) return 0.0;
        if (is_first_run) {
            prev_temp = cur;
            is_first_run = false;
        }
        double error = cur - target;
        double rate = (cur - prev_temp) / dt;
        prev_temp = cur;

        if (error > 3.0) {
            integral = 0.0;
            return 100.0;
        }

        double predicted_stop_temp = cur + (rate * 3.0);
        if (cur <= target || predicted_stop_temp < target - 0.2) {
            integral = 0.0;
            return 0.0;
        }

        if (error < 1.0 && error > 0.0) {
            if (ki > 0.0) {
                integral += error * dt;
                if (integral * ki > 25.0) integral = 25.0 / ki;
            } else {
                integral = 0.0;
            }
        } else {
            integral = 0.0;
        }

        return std::max(0.0, std::min((kp * error) + (ki * integral) + (kd * rate), 50.0));
    }
};

class PredictiveHeatingPID {
public:
    double target = 40.0;
    double prev_temp = 25.0;
    double integral = 0.0;
    double filtered_rate = 0.0;
    bool is_first_run = true;
    double kp = 10.0;
    double ki = 0.2;
    double kd = 60.0;
    double prediction_seconds = 12.0;
    double full_power_error = 4.0;
    double max_output = 100.0;
    double approach_max_output = 50.0;
    double integral_band = 1.0;
    bool preserve_integral_while_coasting = false;

    void reset() {
        integral = 0.0;
        filtered_rate = 0.0;
        is_first_run = true;
    }

    void setTunings(double proportional, double integralGain, double derivativeGain) {
        kp = std::max(0.0, proportional);
        ki = std::max(0.0, integralGain);
        kd = std::max(0.0, derivativeGain);
        // Preserve the filtered heating trend during live tuning. Forgetting it
        // could briefly re-apply heat while the bath still has thermal momentum.
        integral = 0.0;
    }

    double compute(double cur, double dt) {
        if (!std::isfinite(cur) || !std::isfinite(dt) || dt <= 0.0) return 0.0;
        if (is_first_run) {
            prev_temp = cur;
            is_first_run = false;
        }
        double error = target - cur;
        const double rate = (cur - prev_temp) / dt;
        prev_temp = cur;
        // PT100 readings at 500 ms are noisy. Filtering the measurement derivative
        // keeps the D term and the thermal-inertia prediction from chattering.
        const double rateAlpha = 0.35;
        filtered_rate += rateAlpha * (rate - filtered_rate);

        const double heatingRate = std::max(0.0, filtered_rate);
        const double predicted_stop_temp = cur + (heatingRate * prediction_seconds);
        if (cur >= target) {
            integral = 0.0;
            return 0.0;
        }
        if (predicted_stop_temp >= target) {
            if (!preserve_integral_while_coasting) integral = 0.0;
            return 0.0;
        }

        // The configured high output is only allowed while both the current
        // and predicted temperatures are safely outside the approach band.
        if (error > full_power_error &&
            predicted_stop_temp < target - (0.5 * full_power_error)) {
            integral = 0.0;
            return std::max(0.0, std::min(max_output, 100.0));
        }
        if (error < integral_band && error > 0.0) {
            if (ki > 0.0) {
                integral += error * dt;
                if (integral * ki > 10.0) integral = 10.0 / ki;
            } else {
                integral = 0.0;
            }
        } else {
            integral = 0.0;
        }

        const double pidOutput =
            (kp * error) + (ki * integral) - (kd * filtered_rate);
        const double outputLimit =
            std::max(0.0, std::min({approach_max_output, max_output, 100.0}));
        return std::max(0.0, std::min(pidOutput, outputLimit));
    }
};

#endif // CPC_CONTROL_TEMPERATUREPID_H
