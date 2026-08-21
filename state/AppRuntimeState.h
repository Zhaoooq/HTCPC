#ifndef CPC_STATE_APPRUNTIMESTATE_H
#define CPC_STATE_APPRUNTIMESTATE_H

#include <limits>

struct ActuatorState {
    bool condRunning = false;
    bool satRunning = false;
    bool opcHeaterRunning = false;
    bool pumpRunning = false;
    bool bypassValveOpen = false;
    double pumpCurrentPower = 30.0;
};

struct AcquisitionState {
    bool acquiring = false;
    bool hasLatestOpcFrame = false;
    bool hasLatestParticleConcentration = false;
    bool latestParticleConcentrationValid = false;
    bool particlePlotFollowLatest = true;
    bool particlePlotAutoY = true;
    double latestParticleConcentration = std::numeric_limits<double>::quiet_NaN();
    double smoothedParticleConcentration = std::numeric_limits<double>::quiet_NaN();
    double latestParticleConcentrationTime = 0.0;
};

#endif // CPC_STATE_APPRUNTIMESTATE_H
