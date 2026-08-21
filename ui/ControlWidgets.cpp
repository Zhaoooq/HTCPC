#include "ControlWidgets.h"
#include "TouchDoubleSpinBox.h"

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
    group->setStyleSheet(QString(
        "QGroupBox { border: 1px solid #DCE4EA; border-top: 3px solid %1; border-radius: 10px; "
        "font-weight: bold; background-color: #FFFFFF; margin-top: 28px; }"
        "QGroupBox::title { color: %1; subcontrol-origin: margin; subcontrol-position: top left; "
        "left: 10px; padding: 0; background: transparent; }")
        .arg(colorHex));
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setContentsMargins(12, 26, 12, 12);
    layout->setSpacing(7);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    btnStart = new QPushButton("启动");
    btnStop = new QPushButton("停止");
    btnStart->setMinimumHeight(44);
    btnStop->setMinimumHeight(44);
    btnStart->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 7px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { border: 2px solid #34495E; }"
        "QPushButton:pressed { border: 2px solid #263746; padding-top: 2px; }"
        "QPushButton:disabled { background-color: #D8DEE3; color: #8A969F; }")
        .arg(colorHex));
    btnStop->setStyleSheet(
        "QPushButton { background-color: #7F8C8D; color: white; border: none; border-radius: 7px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #626F70; }"
        "QPushButton:pressed { background-color: #4D5859; padding-top: 2px; }"
        "QPushButton:disabled { background-color: #D8DEE3; color: #8A969F; }");
    btnLayout->addWidget(btnStart);
    btnLayout->addWidget(btnStop);

    spinBox = new TouchDoubleSpinBox(QString("设置%1目标温度").arg(title));
    spinBox->setRange(-20, 100);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(0.5);
    spinBox->setMinimumHeight(38);
    spinBox->setSuffix(" ℃");
    spinBox->setStyleSheet("QDoubleSpinBox { padding: 4px 8px; background: #F9FBFC; border: 1px solid #C7D1D9; border-radius: 6px; font-size: 15px; } QDoubleSpinBox:focus { border: 2px solid #2E86C1; background: #FFFFFF; }");

    QLabel *targetLabel = new QLabel("目标温度");
    targetLabel->setStyleSheet("font-size: 13px; color: #566573;");
    lblTemp = new QLabel("当前: -- ℃");
    lblTemp->setAlignment(Qt::AlignCenter);
    lblTemp->setStyleSheet(QString("font-size: 23px; font-weight: bold; color: %1; background: #F7FAFC; border-radius: 7px; padding: 7px;").arg(colorHex));
    lblPwm = new QLabel("功率: -- %");
    lblPwm->setAlignment(Qt::AlignCenter);
    lblPwm->setStyleSheet("font-size: 13px; color: #667884;");

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
    group->setStyleSheet(QString(
        "QGroupBox { border: 1px solid #DCE4EA; border-top: 3px solid %1; border-radius: 10px; "
        "background-color: #FFFFFF; font-weight: bold; margin-top: 28px; }"
        "QGroupBox::title { color: %1; subcontrol-origin: margin; subcontrol-position: top left; "
        "left: 10px; padding: 0; background: transparent; }")
        .arg(colorHex));
    group->setFixedHeight(118);
    QVBoxLayout *layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 12, 8, 8);
    layout->addWidget(valueLabel);
    return group;
}
