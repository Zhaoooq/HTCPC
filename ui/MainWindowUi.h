#ifndef CPC_UI_MAINWINDOWUI_H
#define CPC_UI_MAINWINDOWUI_H

class QApplication;
class QComboBox;
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
    QLabel *lblWarmupCountdown = nullptr;
    QLabel *lblOverviewCondLamp = nullptr;
    QLabel *lblOverviewSatLamp = nullptr;
    QLabel *lblOverviewOpcLamp = nullptr;
    QLabel *lblOverviewPump = nullptr;
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
    QComboBox *cmbTempPidSegment = nullptr;
    QDoubleSpinBox *sbTempKp = nullptr;
    QDoubleSpinBox *sbTempKi = nullptr;
    QDoubleSpinBox *sbTempKd = nullptr;
    QPushButton *btnTempPidApply = nullptr;
    QPushButton *btnTempPidReset = nullptr;
    QLabel *lblTempPidStatus = nullptr;

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

    QComboBox *cmbPressureControlChannel = nullptr;
    QDoubleSpinBox *sbPressureTarget = nullptr;
    QComboBox *cmbPressureControlDirection = nullptr;
    QDoubleSpinBox *sbPressureKp = nullptr;
    QDoubleSpinBox *sbPressureKi = nullptr;
    QPushButton *btnPressureControlStart = nullptr;
    QPushButton *btnPressureControlStop = nullptr;
    QLabel *lblPressureControlStatus = nullptr;

    QLabel *lblPressureValue[3] = {nullptr, nullptr, nullptr};
    QLabel *lblPressureDetails[3] = {nullptr, nullptr, nullptr};
    QLabel *lblPressureStatus[3] = {nullptr, nullptr, nullptr};
    QPushButton *btnPressureZero = nullptr;

    QPushButton *btnBypassHighFlow = nullptr;
    QPushButton *btnBypassLowFlow = nullptr;
    QLabel *lblFlowModeState = nullptr;

    QPushButton *btnLiquidStart = nullptr;
    QPushButton *btnLiquidStop = nullptr;
    QPushButton *btnDrain = nullptr;
    QLabel *lblLiquidState = nullptr;
    QTextBrowser *liquidLog = nullptr;
};

MainWindowUi buildMainWindow(QApplication& app, QMainWindow& window, OpcParams& opcParams);

#endif // CPC_UI_MAINWINDOWUI_H
