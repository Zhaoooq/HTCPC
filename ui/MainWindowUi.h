#ifndef CPC_UI_MAINWINDOWUI_H
#define CPC_UI_MAINWINDOWUI_H

class QApplication;
class QCustomPlot;
class QDoubleSpinBox;
class QLabel;
class QMainWindow;
class QPushButton;
class QSlider;
class QTabWidget;
class QTextBrowser;
class QToolButton;
class QWidget;
struct OpcParams;

struct MainWindowUi {
    QTabWidget *tabs = nullptr;
    QWidget *opcTab = nullptr;
    QCustomPlot *opcPlot = nullptr;
    QCustomPlot *particleConcentrationPlot = nullptr;

    QLabel *lblParticleConcentration = nullptr;
    QLabel *lblStatus = nullptr;
    QLabel *lblCaptureState = nullptr;
    QLabel *lblOverviewCondLamp = nullptr;
    QLabel *lblOverviewSatLamp = nullptr;
    QLabel *lblOverviewOpcLamp = nullptr;
    QLabel *lblOverviewPump = nullptr;
    QLabel *lblOverviewFan = nullptr;
    QLabel *lblOverviewLiquidLamp = nullptr;
    QLabel *lblCompactDeviceState = nullptr;

    QToolButton *btnShutdown = nullptr;

    QPushButton *btnAcqStart = nullptr;
    QPushButton *btnAcqStop = nullptr;
    QPushButton *btnSaveRaw = nullptr;
    QPushButton *btnResetParticlePlot = nullptr;

    QDoubleSpinBox *sbCond = nullptr;
    QDoubleSpinBox *sbSat = nullptr;
    QDoubleSpinBox *sbOpc = nullptr;
    QPushButton *btnCondStart = nullptr;
    QPushButton *btnCondStop = nullptr;
    QPushButton *btnSatStart = nullptr;
    QPushButton *btnSatStop = nullptr;
    QPushButton *btnOpcStart = nullptr;
    QPushButton *btnOpcStop = nullptr;
    QLabel *lblCondTemp = nullptr;
    QLabel *lblCondPwm = nullptr;
    QLabel *lblSatTemp = nullptr;
    QLabel *lblSatPwm = nullptr;
    QLabel *lblOpcTemp = nullptr;
    QLabel *lblOpcPwm = nullptr;

    QPushButton *btnPumpStart = nullptr;
    QPushButton *btnPumpStop = nullptr;
    QSlider *sliderPump = nullptr;
    QLabel *lblPumpValue = nullptr;

    QDoubleSpinBox *sbValveOpening = nullptr;
    QPushButton *btnValveApply = nullptr;
    QPushButton *btnValveRead = nullptr;
    QPushButton *btnValveClose = nullptr;
    QLabel *lblValveCurrent = nullptr;
    QLabel *lblValveStatus = nullptr;

    QPushButton *btnOpcFanStart = nullptr;
    QPushButton *btnOpcFanStop = nullptr;
    QPushButton *btnCaseFan1Start = nullptr;
    QPushButton *btnCaseFan1Stop = nullptr;
    QLabel *lblAuxState = nullptr;

    QPushButton *btnLiquidStart = nullptr;
    QPushButton *btnLiquidStop = nullptr;
    QPushButton *btnDrain = nullptr;
    QLabel *lblLiquidState = nullptr;
    QTextBrowser *liquidLog = nullptr;
};

MainWindowUi buildMainWindow(QApplication& app, QMainWindow& window, OpcParams& opcParams);

#endif // CPC_UI_MAINWINDOWUI_H
