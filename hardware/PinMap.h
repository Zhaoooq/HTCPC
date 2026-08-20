#ifndef CPC_HARDWARE_PINMAP_H
#define CPC_HARDWARE_PINMAP_H

namespace PinMap {
    constexpr int GPIO_CHIP = 4;
    constexpr int HARDWARE_PWM_CHIP = 0;     // RP1 PWM0: /sys/class/pwm/pwmchip0

    constexpr int PIN_PELTIER_COND = 12;     // PWM0_CHAN0, Pin 32
    constexpr int PIN_HEATER_SAT = 13;       // PWM0_CHAN1, Pin 33
    constexpr int PIN_VACUUM_PUMP = 23;      // Pin 16, lgpio PWM
    constexpr int PIN_LEVEL_SENSOR = 27;     // Pin 13
    constexpr int PIN_INLET_VALVE = 22;      // Pin 15
    constexpr int PIN_OUTLET_VALVE = 25;     // Pin 22
    constexpr int PIN_OPC_FAN = 5;           // Pin 29
    constexpr int PIN_BYPASS_VALVE = 24;     // Pin 18; high: 1.5 L/min, low: 0.3 L/min
    constexpr int PIN_OPC_HEATER_PWM = 6;    // Pin 31, OPC heater PWM
}

#endif // CPC_HARDWARE_PINMAP_H
