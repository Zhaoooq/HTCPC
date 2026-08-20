#include "WatermarkWidget.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>

namespace {

QPixmap loadBuaaHeaderPixmap() {
    QPixmap pixmap;
    QString appDir = QCoreApplication::applicationDirPath();
    if (pixmap.isNull()) pixmap.load(appDir + "/buaa_header.png");
    if (pixmap.isNull()) pixmap.load(appDir + "/assets/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/CPC_1/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/CPC_Control_System/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/image.png");
    return pixmap;
}

QPixmap extractBuaaSealPixmap(const QPixmap& source) {
    if (source.isNull()) return QPixmap();
    int side = qMin(source.height(), source.width());
    return source.copy(0, 0, side, side);
}

} // namespace

class WatermarkOverlay : public QWidget {
public:
    explicit WatermarkOverlay(const QPixmap& pixmap, QWidget *parent = nullptr)
        : QWidget(parent), seal(pixmap) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        if (seal.isNull()) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setOpacity(0.05);

        int side = qMin(width(), height()) * 0.64;
        QPixmap scaled = seal.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPoint pos((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        painter.drawPixmap(pos, scaled);
    }

private:
    QPixmap seal;
};

WatermarkWidget::WatermarkWidget(QWidget *parent)
    : QWidget(parent),
      overlay(new WatermarkOverlay(extractBuaaSealPixmap(loadBuaaHeaderPixmap()), this)) {}

void WatermarkWidget::raiseWatermark() {
    overlay->setGeometry(rect());
    overlay->raise();
}

void WatermarkWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    raiseWatermark();
}
