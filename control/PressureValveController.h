#ifndef CPC_CONTROL_PRESSUREVALVECONTROLLER_H
#define CPC_CONTROL_PRESSUREVALVECONTROLLER_H

class PressureValveController {
public:
    struct Parameters {
        double kp = 0.40;
        double ki = 0.08;
        double deadbandPercentOfFullScale = 0.20;
        double maxStepPercent = 5.0;
    };

    void setParameters(const Parameters& parameters);
    void reset(double openingPercent);
    double update(double targetPressurePa,
                  double measuredPressurePa,
                  double fullScalePressurePa,
                  double elapsedSeconds,
                  bool openingRaisesPressure);

    double openingPercent() const;

private:
    Parameters parameters_;
    double openingPercent_ = 0.0;
    double previousActionErrorPercent_ = 0.0;
    bool initialized_ = false;
};

#endif // CPC_CONTROL_PRESSUREVALVECONTROLLER_H
