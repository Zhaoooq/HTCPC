#include "PlotSetup.h"

#include "../qcustomplot.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPen>

void setupOpcPlot(QCustomPlot *plot) {
    plot->setBackground(QBrush(QColor("#FFFFFF")));
    plot->xAxis->setBasePen(QPen(Qt::black));
    plot->xAxis->setTickPen(QPen(Qt::black));
    plot->xAxis->setSubTickPen(QPen(Qt::black));
    plot->xAxis->setTickLabelColor(Qt::black);
    plot->xAxis->setLabelColor(Qt::black);
    plot->yAxis->setBasePen(QPen(Qt::black));
    plot->yAxis->setTickPen(QPen(Qt::black));
    plot->yAxis->setSubTickPen(QPen(Qt::black));
    plot->yAxis->setTickLabelColor(Qt::black);
    plot->yAxis->setLabelColor(Qt::black);

    plot->addGraph();
    QPen graphPen;
    graphPen.setColor(QColor("#005bac"));
    graphPen.setWidthF(1.5);
    plot->graph(0)->setPen(graphPen);
    plot->xAxis->setLabel("时间 (s)");
    plot->yAxis->setLabel("OPC 电压 (V)");
    plot->xAxis->setRange(0, 0.05);
    plot->yAxis->setRange(-0.5, 5.5);
    plot->setNotAntialiasedElements(QCP::aeAll);
    plot->setOpenGl(false);
}

void setupParticleConcentrationPlot(QCustomPlot *plot) {
    plot->setBackground(QBrush(QColor("#FFFFFF")));
    plot->addGraph();
    QPen pen;
    pen.setColor(QColor("#16A085"));
    pen.setWidthF(2.5);
    plot->graph(0)->setPen(pen);
    plot->xAxis->setLabel("时间 (s)");
    plot->yAxis->setLabel("颗粒数目浓度 (个/ml)");
    plot->xAxis->setRange(0, 60);
    plot->yAxis->setRange(0, 10);
    plot->xAxis->setTickLabelFont(QFont("Arial", 10));
    plot->yAxis->setTickLabelFont(QFont("Arial", 10));
    plot->xAxis->setLabelFont(QFont("Arial", 11));
    plot->yAxis->setLabelFont(QFont("Arial", 11));
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->axisRect()->setRangeDrag(Qt::Horizontal);
    plot->axisRect()->setRangeDragAxes(plot->xAxis, nullptr);
    plot->axisRect()->setRangeZoom(Qt::Vertical);
    plot->axisRect()->setRangeZoomAxes(nullptr, plot->yAxis);
    plot->axisRect()->setRangeZoomFactor(0.85);
}
