#include "control/TemperaturePid.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

PredictiveHeatingPID makeOpcController() {
    PredictiveHeatingPID pid;
    pid.target = 40.0;
    pid.setTunings(8.0, 0.10, 60.0);
    pid.prediction_seconds = 30.0;
    pid.full_power_error = 6.0;
    pid.max_output = 100.0;
    pid.approach_max_output = 50.0;
    pid.integral_band = 2.0;
    pid.preserve_integral_while_coasting = true;
    return pid;
}

PredictiveHeatingPID makeCondenserController() {
    PredictiveHeatingPID pid;
    pid.target = 40.0;
    pid.setTunings(8.0, 0.10, 90.0);
    pid.prediction_seconds = 20.0;
    pid.full_power_error = 6.0;
    pid.approach_max_output = 40.0;
    return pid;
}

} // namespace

int main() {
    for (double dt : {0.3, 0.5, 0.8}) {
        auto heating = makeOpcController();
        const double output = heating.compute(35.0, dt);
        assert(std::isfinite(output) && output >= 0.0 && output <= 100.0);
    }
    for (double dt : {0.0, -0.5, std::numeric_limits<double>::quiet_NaN(), 10.0}) {
        auto saturation = makeCondenserController();
        auto opc = makeOpcController();
        HybridCoolingPID cooling;
        assert(saturation.compute(35.0, dt) == 0.0);
        assert(opc.compute(35.0, dt) == 0.0);
        assert(cooling.compute(12.0, dt) == 0.0);
    }
    {
        auto pid = makeCondenserController();
        // The HTCPC condenser is a heater: it must heat below target and must
        // never energize at or above target (the old cooling PID did the reverse).
        assert(std::abs(pid.compute(25.0, 0.5) - 100.0) < 1e-9);
        assert(std::abs(pid.compute(40.0, 0.5)) < 1e-9);
        assert(std::abs(pid.compute(45.0, 0.5)) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        assert(std::abs(pid.compute(25.0, 0.5) - 100.0) < 1e-9);
    }
    {
        PredictiveHeatingPID pid;
        pid.target = 100.0;
        pid.max_output = 100.0;
        assert(std::abs(pid.compute(20.0, 0.5) - 100.0) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        pid.compute(30.0, 0.5);
        // A fast sustained rise makes the coast prediction cross the
        // target, so heat must be removed before the measured setpoint.
        assert(std::abs(pid.compute(30.5, 0.5)) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        double output = 0.0;
        // A slow 0.02 C/s rise near the target must retain useful heat instead
        // of being held more than one degree below the setpoint.
        for (int i = 0; i <= 20; ++i) {
            output = pid.compute(38.35 + (0.01 * i), 0.5);
        }
        assert(output > 5.0);
    }
    {
        auto pid = makeOpcController();
        double output = 0.0;
        // At a steady 39 C, integral action must build enough holding power to
        // remove the former one-degree steady-state error.
        for (int i = 0; i < 120; ++i) {
            output = pid.compute(39.0, 0.5);
        }
        assert(output > 13.0);

        const double accumulatedIntegral = pid.integral;
        assert(std::abs(pid.compute(39.5, 0.5)) < 1e-9);
        assert(std::abs(pid.integral - accumulatedIntegral) < 1e-9);
        assert(std::abs(pid.compute(40.0, 0.5)) < 1e-9);
        assert(std::abs(pid.integral) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        // The controller must never heat at or above the target.
        assert(std::abs(pid.compute(40.0, 0.5)) < 1e-9);
        assert(std::abs(pid.compute(45.0, 0.5)) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        // Aggressive tunings remain capped at 50% inside the approach region.
        pid.setTunings(100.0, 20.0, 0.0);
        assert(std::abs(pid.compute(35.0, 0.5) - 50.0) < 1e-9);
    }
    return 0;
}
