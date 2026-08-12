#ifndef CPC_UI_CONTROLWIDGETS_H
#define CPC_UI_CONTROLWIDGETS_H

#include <QString>

class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;

QGroupBox* createTempGroup(
    const QString& title,
    const QString& colorHex,
    QDoubleSpinBox*& spinBox,
    QPushButton*& btnStart,
    QPushButton*& btnStop,
    QLabel*& lblTemp,
    QLabel*& lblPwm
);

QLabel* createOverviewValue(const QString& text, const QString& colorHex = "#2c3e50");
QGroupBox* createOverviewCard(const QString& title, QLabel *valueLabel, const QString& colorHex);

#endif // CPC_UI_CONTROLWIDGETS_H
