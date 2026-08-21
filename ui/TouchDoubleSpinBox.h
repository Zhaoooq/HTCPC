#ifndef CPC_UI_TOUCHDOUBLESPINBOX_H
#define CPC_UI_TOUCHDOUBLESPINBOX_H

#include <QDoubleSpinBox>
#include <QString>

class QMouseEvent;
class QObject;
class QEvent;
class QWheelEvent;

class TouchDoubleSpinBox : public QDoubleSpinBox {
public:
    explicit TouchDoubleSpinBox(const QString& inputTitle = QString(),
                                QWidget *parent = nullptr);

    void setInputTitle(const QString& title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QString inputTitle_;
    bool dialogOpen_;

    void openNumericKeypad();
};

#endif // CPC_UI_TOUCHDOUBLESPINBOX_H
