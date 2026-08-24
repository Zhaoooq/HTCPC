#include "PlotSetup.h"

#include "../qcustomplot.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPen>

void setupOpcPlot(QCustomPlot *plot) {
    plot->setBackground(QBrush(QColor("#FFFFFF")));
    plot->axisRect()->setBackground(QBrush(QColor("#FBFCFD")));
    const QPen axisPen(QColor("#71828E"));
    plot->xAxis->setBasePen(axisPen);
    plot->xAxis->setTickPen(axisPen);
    plot->xAxis->setSubTickPen(axisPen);
    plot->xAxis->setTickLabelColor(QColor("#526471"));
    plot->xAxis->setLabelColor(QColor("#3C596B"));
    plot->yAxis->setBasePen(axisPen);
    plot->yAxis->setTickPen(axisPen);
    plot->yAxis->setSubTickPen(axisPen);
    plot->yAxis->setTickLabelColor(QColor("#526471"));
    plot->yAxis->setLabelColor(QColor("#3C596B"));
    plot->xAxis->grid()->setPen(QPen(QColor("#E3E9ED"), 1, Qt::DashLine));
    plot->yAxis->grid()->setPen(QPen(QColor("#E3E9ED"), 1, Qt::DashLine));
    plot->setStyleSheet("border: none; background: #FFFFFF;");

    plot->addGraph();
    QPen graphPen;
    graphPen.setColor(QColor("#005bac"));
    graphPen.setWidthF(1.5);
    plot->graph(0)->setPen(graphPen);
    plot->xAxis->setLabel("时间 (s)");
    plot->yAxis->setLabel("OPC 电压 (V)");
    plot->xAxis->setRange(0, 0.05);
    plot->yAxis->setRange(-0.5, 5.5);
    plot->xAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 11));
    plot->yAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 11));
    plot->xAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 12));
    plot->yAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 12));
    plot->setNotAntialiasedElements(QCP::aeAll);
    plot->setOpenGl(false);
}

void setupParticleConcentrationPlot(QCustomPlot *plot) {
    plot->setBackground(QBrush(QColor("#FFFFFF")));
    plot->axisRect()->setBackground(QBrush(QColor("#FBFCFD")));
    plot->addGraph();
    QPen pen;
    pen.setColor(QColor("#16A085"));
    pen.setWidthF(2.5);
    plot->graph(0)->setPen(pen);
    plot->xAxis->setLabel("时间 (s)");
    plot->yAxis->setLabel("颗粒数目浓度 (个/ml)");
    plot->xAxis->setRange(0, 60);
    plot->yAxis->setRange(0, 10);
    const QPen axisPen(QColor("#71828E"));
    plot->xAxis->setBasePen(axisPen);
    plot->xAxis->setTickPen(axisPen);
    plot->xAxis->setSubTickPen(axisPen);
    plot->yAxis->setBasePen(axisPen);
    plot->yAxis->setTickPen(axisPen);
    plot->yAxis->setSubTickPen(axisPen);
    plot->xAxis->setTickLabelColor(QColor("#526471"));
    plot->yAxis->setTickLabelColor(QColor("#526471"));
    plot->xAxis->setLabelColor(QColor("#3C596B"));
    plot->yAxis->setLabelColor(QColor("#3C596B"));
    plot->xAxis->grid()->setPen(QPen(QColor("#E3E9ED"), 1, Qt::DashLine));
    plot->yAxis->grid()->setPen(QPen(QColor("#E3E9ED"), 1, Qt::DashLine));
    plot->setStyleSheet("border: none; background: #FFFFFF;");
    plot->xAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 11));
    plot->yAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 11));
    plot->xAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 12));
    plot->yAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 12));
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->axisRect()->setRangeDrag(Qt::Horizontal);
    plot->axisRect()->setRangeDragAxes(plot->xAxis, nullptr);
    plot->axisRect()->setRangeZoom(Qt::Vertical);
    plot->axisRect()->setRangeZoomAxes(nullptr, plot->yAxis);
    plot->axisRect()->setRangeZoomFactor(0.85);
}
