#include "Formatters.h"

#include <cmath>

QString formatTemp(float value) {
    if (std::isnan(value)) return "--";
    return QString::number(value, 'f', 2);
}

QString formatParticleConcentration(double value) {
    if (!std::isfinite(value)) return "-- 个/ml";
    if (value >= 1000000.0) return QString("%1 个/ml").arg(value, 0, 'e', 2);
    if (value >= 1000.0) return QString("%1 个/ml").arg(value, 0, 'f', 0);
    if (value >= 10.0) return QString("%1 个/ml").arg(value, 0, 'f', 1);
    return QString("%1 个/ml").arg(value, 0, 'f', 2);
}
