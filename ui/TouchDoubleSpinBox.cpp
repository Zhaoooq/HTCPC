#include "TouchDoubleSpinBox.h"

#include <QAbstractSpinBox>
#include <QDialog>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

TouchDoubleSpinBox::TouchDoubleSpinBox(const QString& inputTitle, QWidget *parent)
    : QDoubleSpinBox(parent), inputTitle_(inputTitle), dialogOpen_(false) {
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    setKeyboardTracking(false);
    setReadOnly(true);
    setToolTip("点击打开数字键盘");
    lineEdit()->setCursor(Qt::PointingHandCursor);
    lineEdit()->installEventFilter(this);
}

void TouchDoubleSpinBox::setInputTitle(const QString& title) {
    inputTitle_ = title;
}

bool TouchDoubleSpinBox::eventFilter(QObject *watched, QEvent *event) {
    if (watched == lineEdit() && event && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && isEnabled()) {
            QTimer::singleShot(0, this, [this]() { openNumericKeypad(); });
            return true;
        }
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}

void TouchDoubleSpinBox::mousePressEvent(QMouseEvent *event) {
    QDoubleSpinBox::mousePressEvent(event);
    if (event && event->button() == Qt::LeftButton && isEnabled()) {
        openNumericKeypad();
    }
}

void TouchDoubleSpinBox::wheelEvent(QWheelEvent *event) {
    if (event) event->ignore();
}

void TouchDoubleSpinBox::openNumericKeypad() {
    if (dialogOpen_) return;
    dialogOpen_ = true;

    QDialog dialog(this);
    dialog.setWindowTitle(inputTitle_.isEmpty() ? QStringLiteral("输入数值") : inputTitle_);
    dialog.setModal(true);
    dialog.setMinimumSize(500, 650);
    dialog.setStyleSheet(
        "QDialog { background: #EAF0F4; }"
        "QLabel { background: transparent; color: #526471; }"
        "QLineEdit { min-height: 62px; padding: 4px 14px; background: #FFFFFF; "
        "color: #1F4E68; border: 2px solid #2E86C1; border-radius: 9px; "
        "font-size: 30px; font-weight: bold; }"
        "QPushButton { min-width: 90px; min-height: 64px; background: #FFFFFF; "
        "color: #2C3E50; border: 1px solid #C6D2DA; border-radius: 9px; "
        "font-size: 25px; font-weight: bold; outline: none; }"
        "QPushButton:focus { outline: none; }"
        "QPushButton:pressed { background: #DCEAF3; border-color: #2E86C1; }"
        "QPushButton:disabled { background: #E8ECEF; color: #A6B0B7; }"
        "QPushButton#confirmButton { background: #167D68; color: #FFFFFF; border: none; }"
        "QPushButton#cancelButton { background: #7F8C8D; color: #FFFFFF; border: none; }"
        "QPushButton#clearButton { color: #A93226; }"
    );

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    QLabel *title = new QLabel(dialog.windowTitle());
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 21px; font-weight: bold; color: #2C5D7C;");
    root->addWidget(title);

    QLineEdit *display = new QLineEdit(
        QLocale::c().toString(value(), 'f', decimals()), &dialog);
    display->setReadOnly(true);
    display->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    root->addWidget(display);

    QLabel *rangeHint = new QLabel(
        QString("允许范围：%1 ～ %2%3")
            .arg(minimum(), 0, 'f', decimals())
            .arg(maximum(), 0, 'f', decimals())
            .arg(suffix()));
    rangeHint->setAlignment(Qt::AlignCenter);
    rangeHint->setStyleSheet("font-size: 14px; color: #607481;");
    root->addWidget(rangeHint);

    QGridLayout *keys = new QGridLayout();
    keys->setHorizontalSpacing(12);
    keys->setVerticalSpacing(12);
    root->addLayout(keys, 1);

    bool replaceOnNextDigit = true;
    auto makeButton = [&](const QString& text, int row, int column) {
        QPushButton *button = new QPushButton(text);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setFixedHeight(64);
        keys->addWidget(button, row, column, Qt::AlignVCenter);
        return button;
    };
    auto appendCharacter = [&](const QString& character) {
        QString text = display->text();
        if (replaceOnNextDigit) {
            text.clear();
            replaceOnNextDigit = false;
        }
        if (character == ".") {
            if (decimals() == 0 || text.contains('.')) return;
            if (text.isEmpty() || text == "-") text += "0";
        }
        if (text == "0" && character != ".") text.clear();
        if (text == "-0" && character != ".") text = "-";
        display->setText(text + character);
    };

    const char *digits[3][3] = {{"7", "8", "9"}, {"4", "5", "6"}, {"1", "2", "3"}};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            QPushButton *button = makeButton(QString::fromLatin1(digits[row][column]), row, column);
            QObject::connect(button, &QPushButton::clicked, &dialog,
                             [&, button]() { appendCharacter(button->text()); });
        }
    }

    QPushButton *signButton = makeButton(QStringLiteral("±"), 3, 0);
    signButton->setEnabled(minimum() < 0.0);
    QObject::connect(signButton, &QPushButton::clicked, &dialog, [&]() {
        QString text = display->text();
        if (text.startsWith('-')) text.remove(0, 1);
        else text.prepend('-');
        display->setText(text);
        replaceOnNextDigit = false;
    });
    QPushButton *zeroButton = makeButton(QStringLiteral("0"), 3, 1);
    QObject::connect(zeroButton, &QPushButton::clicked, &dialog, [&]() { appendCharacter("0"); });
    QPushButton *decimalButton = makeButton(QStringLiteral("."), 3, 2);
    decimalButton->setEnabled(decimals() > 0);
    QObject::connect(decimalButton, &QPushButton::clicked, &dialog, [&]() { appendCharacter("."); });
    for (int row = 0; row < 4; ++row) keys->setRowStretch(row, 1);

    QHBoxLayout *editButtons = new QHBoxLayout();
    editButtons->setSpacing(12);
    QPushButton *clearButton = new QPushButton(QStringLiteral("清空"));
    clearButton->setObjectName("clearButton");
    QPushButton *backspaceButton = new QPushButton(QStringLiteral("退格"));
    clearButton->setFixedHeight(64);
    backspaceButton->setFixedHeight(64);
    editButtons->addWidget(clearButton);
    editButtons->addWidget(backspaceButton);
    root->addLayout(editButtons);
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&]() {
        display->clear();
        replaceOnNextDigit = false;
    });
    QObject::connect(backspaceButton, &QPushButton::clicked, &dialog, [&]() {
        if (replaceOnNextDigit) {
            display->clear();
            replaceOnNextDigit = false;
        } else {
            display->backspace();
        }
    });

    QHBoxLayout *actionButtons = new QHBoxLayout();
    actionButtons->setSpacing(12);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("取消"));
    cancelButton->setObjectName("cancelButton");
    QPushButton *confirmButton = new QPushButton(QStringLiteral("确定"));
    confirmButton->setObjectName("confirmButton");
    cancelButton->setFixedHeight(64);
    confirmButton->setFixedHeight(64);
    actionButtons->addWidget(cancelButton);
    actionButtons->addWidget(confirmButton);
    root->addLayout(actionButtons);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(confirmButton, &QPushButton::clicked, &dialog, [&]() {
        bool ok = false;
        const double entered = QLocale::c().toDouble(display->text(), &ok);
        if (!ok || !std::isfinite(entered) || entered < minimum() || entered > maximum()) {
            rangeHint->setText(
                QString("请输入 %1 ～ %2 范围内的数值")
                    .arg(minimum(), 0, 'f', decimals())
                    .arg(maximum(), 0, 'f', decimals()));
            rangeHint->setStyleSheet("font-size: 14px; color: #C0392B; font-weight: bold;");
            return;
        }
        setValue(entered);
        dialog.accept();
    });

    dialog.exec();
    dialogOpen_ = false;
}
