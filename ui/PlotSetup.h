#ifndef CPC_UI_PLOTSETUP_H
#define CPC_UI_PLOTSETUP_H

class QCustomPlot;

void setupOpcPlot(QCustomPlot *plot);
void setOpcPlotAutoView(QCustomPlot *plot, bool enabled);
bool opcPlotAutoViewEnabled(const QCustomPlot *plot);
void fitOpcPlotToData(QCustomPlot *plot, double visibleSeconds = 0.05);
void setupParticleConcentrationPlot(QCustomPlot *plot);

#endif // CPC_UI_PLOTSETUP_H
