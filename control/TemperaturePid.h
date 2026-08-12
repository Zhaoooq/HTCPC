#ifndef CPC_CONTROL_TEMPERATUREPID_H
#define CPC_CONTROL_TEMPERATUREPID_H

#include <algorithm>

class HybridCoolingPID {
public:
    double target = 10.0;
    double prev_temp = 25.0;
    double integral = 0.0;
    bool is_first_run = true;

    double compute(double cur, double dt) {
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
            integral += error * dt;
            if (integral * 1.5 > 25.0) integral = 25.0 / 1.5;
        } else {
            integral = 0.0;
        }

        return std::max(0.0, std::min((20.0 * error) + (1.5 * integral) + (40.0 * rate), 50.0));
    }
};

class PredictiveHeatingPID {
public:
    double target = 40.0;
    double prev_temp = 25.0;
    double integral = 0.0;
    bool is_first_run = true;

    double compute(double cur, double dt) {
        if (is_first_run) {
            prev_temp = cur;
            is_first_run = false;
        }
        double error = target - cur;
        double rate = (cur - prev_temp) / dt;
        prev_temp = cur;

        double predicted_stop_temp = cur + (rate * 12.0);
        if (cur >= target || predicted_stop_temp > target + 0.2) {
            integral = 0.0;
            return 0.0;
        }

        if (error > 4.0) return 100.0;
        if (error < 1.0 && error > 0.0) {
            integral += error * dt;
        } else {
            integral = 0.0;
        }

        return std::max(0.0, std::min((10.0 * error) + (0.2 * integral) + (-60.0 * rate), 50.0));
    }
};

#endif // CPC_CONTROL_TEMPERATUREPID_H
