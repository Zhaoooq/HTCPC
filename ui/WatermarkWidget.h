#ifndef CPC_UI_WATERMARKWIDGET_H
#define CPC_UI_WATERMARKWIDGET_H

#include <QWidget>

class WatermarkOverlay;

class WatermarkWidget : public QWidget {
public:
    explicit WatermarkWidget(QWidget *parent = nullptr);

    void raiseWatermark();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    WatermarkOverlay *overlay;
};

#endif // CPC_UI_WATERMARKWIDGET_H
