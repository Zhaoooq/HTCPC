#include "PlotSetup.h"

#include "../qcustomplot.h"

#include <QBrush>
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QPen>
#include <QTouchEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr const char *OPC_AUTO_VIEW_PROPERTY = "opcAutoView";
constexpr double MIN_TOUCH_X_SPAN_SECONDS = 0.0002;
constexpr double MAX_TOUCH_X_SPAN_SECONDS = 0.5;
constexpr double MIN_TOUCH_Y_SPAN_VOLTS = 0.02;
constexpr double MAX_TOUCH_Y_SPAN_VOLTS = 50.0;
constexpr qint64 TOUCH_REPLOT_INTERVAL_MS = 33;

QPointF touchCenter(const QList<QTouchEvent::TouchPoint>& points) {
    QPointF center;
    for (const QTouchEvent::TouchPoint& point : points) center += point.pos();
    return points.isEmpty() ? center : center / points.size();
}

double touchDistance(const QList<QTouchEvent::TouchPoint>& points) {
    if (points.size() < 2) return 0.0;
    const QPointF delta = points.at(0).pos() - points.at(1).pos();
    return std::hypot(delta.x(), delta.y());
}

void scaleAxisAroundPixel(QCPAxis *axis,
                          double factor,
                          double centerPixel,
                          double minimumSpan,
                          double maximumSpan) {
    if (!axis || !std::isfinite(factor) || factor <= 0.0) return;
    const double currentSpan = axis->range().size();
    if (!std::isfinite(currentSpan) || currentSpan <= 0.0) return;
    const double requestedSpan = qBound(minimumSpan, currentSpan * factor, maximumSpan);
    axis->scaleRange(requestedSpan / currentSpan, axis->pixelToCoord(centerPixel));
}

class TouchPlotController final : public QObject {
public:
    explicit TouchPlotController(QCustomPlot *plot)
        : QObject(plot), plot_(plot) {
        clock_.start();
        replotClock_.start();
        plot_->setAttribute(Qt::WA_AcceptTouchEvents, true);
        plot_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != plot_) return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::TouchBegin:
            return beginTouch(static_cast<QTouchEvent *>(event));
        case QEvent::TouchUpdate:
            return updateTouch(static_cast<QTouchEvent *>(event));
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            return endTouch(static_cast<QTouchEvent *>(event));
        default:
            return QObject::eventFilter(watched, event);
        }
    }

private:
    bool beginTouch(QTouchEvent *event) {
        const QList<QTouchEvent::TouchPoint> points = event->touchPoints();
        if (points.isEmpty()) return false;
        const QPointF center = touchCenter(points);
        if (!plot_->axisRect()->rect().contains(center.toPoint())) return false;

        active_ = true;
        moved_ = false;
        startedWithOnePoint_ = points.size() == 1;
        lastPointCount_ = points.size();
        lastCenter_ = center;
        lastDistance_ = touchDistance(points);
        event->accept();
        return true;
    }

    bool updateTouch(QTouchEvent *event) {
        const QList<QTouchEvent::TouchPoint> points = event->touchPoints();
        if (!active_ || points.isEmpty()) return false;

        const QPointF center = touchCenter(points);
        if (points.size() >= 2) {
            const double distance = touchDistance(points);
            setOpcPlotAutoView(plot_, false);
            if (lastPointCount_ >= 2 && lastDistance_ > 1.0 && distance > 1.0) {
                const double factor = qBound(0.5, lastDistance_ / distance, 2.0);
                scaleAxisAroundPixel(plot_->xAxis, factor, center.x(),
                                     MIN_TOUCH_X_SPAN_SECONDS, MAX_TOUCH_X_SPAN_SECONDS);
                scaleAxisAroundPixel(plot_->yAxis, factor, center.y(),
                                     MIN_TOUCH_Y_SPAN_VOLTS, MAX_TOUCH_Y_SPAN_VOLTS);
            }
            moved_ = true;
            lastDistance_ = distance;
        } else if (lastPointCount_ == 1) {
            const QPointF movement = center - lastCenter_;
            if (std::hypot(movement.x(), movement.y()) >= 1.0) {
                setOpcPlotAutoView(plot_, false);
                const double xShift = plot_->xAxis->pixelToCoord(lastCenter_.x()) -
                                      plot_->xAxis->pixelToCoord(center.x());
                const double yShift = plot_->yAxis->pixelToCoord(lastCenter_.y()) -
                                      plot_->yAxis->pixelToCoord(center.y());
                plot_->xAxis->moveRange(xShift);
                plot_->yAxis->moveRange(yShift);
                moved_ = true;
            }
        }

        lastPointCount_ = points.size();
        lastCenter_ = center;
        requestReplot();
        event->accept();
        return true;
    }

    bool endTouch(QTouchEvent *event) {
        if (!active_) return false;

        bool restoredAutoView = false;
        if (startedWithOnePoint_ && !moved_) {
            const qint64 now = clock_.elapsed();
            if (now - lastTapMs_ <= 450 &&
                std::hypot(lastCenter_.x() - lastTapPosition_.x(),
                           lastCenter_.y() - lastTapPosition_.y()) <= 45.0) {
                setOpcPlotAutoView(plot_, true);
                fitOpcPlotToData(plot_);
                restoredAutoView = true;
                lastTapMs_ = -1000;
            } else {
                lastTapMs_ = now;
                lastTapPosition_ = lastCenter_;
            }
        }

        // A throttled update may still be pending when the fingers leave the
        // screen. Always render the final pan/zoom range before ending it.
        if (moved_ || restoredAutoView) requestReplot(true);
        active_ = false;
        lastPointCount_ = 0;
        lastDistance_ = 0.0;
        event->accept();
        return true;
    }

    void requestReplot(bool force = false) {
        if (!force && replotClock_.elapsed() < TOUCH_REPLOT_INTERVAL_MS) return;
        plot_->replot(QCustomPlot::rpQueuedReplot);
        replotClock_.restart();
    }

    QCustomPlot *plot_ = nullptr;
    QElapsedTimer clock_;
    QElapsedTimer replotClock_;
    bool active_ = false;
    bool moved_ = false;
    bool startedWithOnePoint_ = false;
    int lastPointCount_ = 0;
    QPointF lastCenter_;
    double lastDistance_ = 0.0;
    qint64 lastTapMs_ = -1000;
    QPointF lastTapPosition_;
};

} // namespace

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
    // 屏幕水平分辨率低于 50 ms 窗口内的采样点数时，
    // 由 QCustomPlot 合并同一像素列的点，同时保留局部极值以免丢失脉冲。
    plot->graph(0)->setAdaptiveSampling(true);

    plot->addGraph();
    plot->graph(1)->setLineStyle(QCPGraph::lsNone);
    plot->graph(1)->setScatterStyle(QCPScatterStyle(
        QCPScatterStyle::ssCircle,
        QPen(QColor("#C0392B"), 1.2),
        QBrush(QColor("#E74C3C")),
        7.0));
    plot->xAxis->setLabel("时间 (s)");
    plot->yAxis->setLabel("OPC 电压 (V)");
    plot->xAxis->setRange(0, 0.05);
    plot->yAxis->setRange(-0.5, 10.5);
    plot->xAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 14));
    plot->yAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 14));
    plot->xAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 15));
    plot->yAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 15));
    plot->setNotAntialiasedElements(QCP::aeAll);
    plot->setOpenGl(false);
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->axisRect()->setRangeDrag(Qt::Vertical);
    plot->axisRect()->setRangeDragAxes(nullptr, plot->yAxis);
    plot->axisRect()->setRangeZoom(Qt::Vertical);
    plot->axisRect()->setRangeZoomAxes(nullptr, plot->yAxis);
    plot->axisRect()->setRangeZoomFactor(0.85);
    setOpcPlotAutoView(plot, true);
    new TouchPlotController(plot);
}

void setOpcPlotAutoView(QCustomPlot *plot, bool enabled) {
    if (plot) plot->setProperty(OPC_AUTO_VIEW_PROPERTY, enabled);
}

bool opcPlotAutoViewEnabled(const QCustomPlot *plot) {
    return plot && plot->property(OPC_AUTO_VIEW_PROPERTY).toBool();
}

void fitOpcPlotToData(QCustomPlot *plot, double visibleSeconds) {
    if (!plot || plot->graphCount() == 0 || visibleSeconds <= 0.0) return;
    const QSharedPointer<QCPGraphDataContainer> data = plot->graph(0)->data();
    if (!data || data->isEmpty()) {
        plot->xAxis->setRange(0.0, visibleSeconds);
        plot->yAxis->setRange(-0.5, 10.5);
        return;
    }

    // Graph 数据按时间排序，直接取末点并二分定位可见窗口，
    // 避免在每一帧为了自动量程重复遍历过期数据。
    const double latestTime = (data->constEnd() - 1)->key;
    if (!std::isfinite(latestTime)) return;

    const double earliestVisibleTime = latestTime - visibleSeconds;
    double minimumVoltage = std::numeric_limits<double>::infinity();
    double maximumVoltage = -std::numeric_limits<double>::infinity();
    for (auto it = data->findBegin(earliestVisibleTime, false);
         it != data->constEnd(); ++it) {
        if (!std::isfinite(it->value)) continue;
        minimumVoltage = std::min(minimumVoltage, it->value);
        maximumVoltage = std::max(maximumVoltage, it->value);
    }

    plot->xAxis->setRange(latestTime, visibleSeconds, Qt::AlignRight);
    if (!std::isfinite(minimumVoltage) || !std::isfinite(maximumVoltage)) return;

    const double center = (minimumVoltage + maximumVoltage) * 0.5;
    const double signalSpan = maximumVoltage - minimumVoltage;
    const double paddedSpan = std::max(0.2, signalSpan * 1.24);
    plot->yAxis->setRange(center - paddedSpan * 0.5, center + paddedSpan * 0.5);
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
    plot->xAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 14));
    plot->yAxis->setTickLabelFont(QFont("WenQuanYi Micro Hei", 14));
    plot->xAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 15));
    plot->yAxis->setLabelFont(QFont("WenQuanYi Micro Hei", 15));
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->axisRect()->setRangeDrag(Qt::Horizontal);
    plot->axisRect()->setRangeDragAxes(plot->xAxis, nullptr);
    plot->axisRect()->setRangeZoom(Qt::Vertical);
    plot->axisRect()->setRangeZoomAxes(nullptr, plot->yAxis);
    plot->axisRect()->setRangeZoomFactor(0.85);
}
