#include "control/TemperaturePid.h"

#include <cassert>
#include <cmath>

namespace {

PredictiveHeatingPID makeOpcController() {
    PredictiveHeatingPID pid;
    pid.target = 40.0;
    pid.setTunings(6.0, 0.05, 100.0);
    pid.prediction_seconds = 90.0;
    pid.full_power_error = 8.0;
    pid.max_output = 50.0;
    pid.approach_max_output = 20.0;
    return pid;
}

} // namespace

int main() {
    {
        auto pid = makeOpcController();
        assert(std::abs(pid.compute(25.0, 0.5) - 50.0) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        pid.compute(30.0, 0.5);
        // A sustained rise makes the 90-second coast prediction cross the
        // target, so heat must be removed before the measured setpoint.
        assert(std::abs(pid.compute(30.5, 0.5)) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        // The controller must never heat at or above the target.
        assert(std::abs(pid.compute(40.0, 0.5)) < 1e-9);
        assert(std::abs(pid.compute(45.0, 0.5)) < 1e-9);
    }
    {
        auto pid = makeOpcController();
        // Even with aggressive user tunings, the approach cap remains a hard
        // constraint on the 30 W heater.
        pid.setTunings(100.0, 20.0, 0.0);
        assert(pid.compute(35.0, 0.5) <= 20.0);
    }
    return 0;
}
