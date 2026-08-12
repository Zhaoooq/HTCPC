#include "ControlWidgets.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

QGroupBox* createTempGroup(
    const QString& title,
    const QString& colorHex,
    QDoubleSpinBox*& spinBox,
    QPushButton*& btnStart,
    QPushButton*& btnStop,
    QLabel*& lblTemp,
    QLabel*& lblPwm
) {
    QGroupBox *group = new QGroupBox(title);
    group->setStyleSheet(QString("QGroupBox { border: 2px solid %1; border-radius: 7px; font-weight: bold; background-color: #FFFFFF; margin-top: 18px;} QGroupBox::title { color: %1; subcontrol-origin: margin; left: 12px; padding: 0 6px;}").arg(colorHex));
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 26, 10, 10);
    layout->setSpacing(8);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    btnStart = new QPushButton("启动");
    btnStop = new QPushButton("停止");
    btnStart->setMinimumHeight(44);
    btnStop->setMinimumHeight(44);
    btnStart->setStyleSheet("QPushButton { background-color: #005bac; color: white; border-radius: 6px; font-size: 15px; font-weight:bold; } QPushButton:hover { background-color: #004080; }");
    btnStop->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; border-radius: 6px; font-size: 15px; font-weight:bold; }");
    btnLayout->addWidget(btnStart);
    btnLayout->addWidget(btnStop);

    spinBox = new QDoubleSpinBox();
    spinBox->setRange(-20, 100);
    spinBox->setMinimumHeight(42);
    spinBox->setSuffix(" ℃");
    spinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; border: 1px solid #bdc3c7; border-radius: 5px; font-size: 15px; }");

    QLabel *targetLabel = new QLabel("目标温度");
    targetLabel->setStyleSheet("font-size: 13px; color: #566573;");
    lblTemp = new QLabel("当前: -- ℃");
    lblTemp->setStyleSheet("font-size: 20px; font-weight: bold; color: #2c3e50;");
    lblPwm = new QLabel("功率: -- %");
    lblPwm->setStyleSheet("font-size: 14px; color: #7f8c8d;");

    layout->addLayout(btnLayout);
    layout->addWidget(targetLabel);
    layout->addWidget(spinBox);
    layout->addWidget(lblTemp);
    layout->addWidget(lblPwm);
    return group;
}

QLabel* createOverviewValue(const QString& text, const QString& colorHex) {
    QLabel *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setStyleSheet(QString("font-size: 20px; font-weight: bold; color: %1;").arg(colorHex));
    return label;
}

QGroupBox* createOverviewCard(const QString& title, QLabel *valueLabel, const QString& colorHex) {
    QGroupBox *group = new QGroupBox(title);
    group->setStyleSheet(QString("QGroupBox { border: 2px solid %1; border-radius: 7px; background-color: #FFFFFF; font-weight: bold; margin-top: 14px;} QGroupBox::title { color: %1; subcontrol-origin: margin; left: 12px; padding: 0 6px;}").arg(colorHex));
    group->setMinimumHeight(66);
    group->setMaximumHeight(96);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 16, 6, 4);
    layout->addWidget(valueLabel);
    return group;
}
