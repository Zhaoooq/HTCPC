#include "MainWindowUi.h"

#include "ControlWidgets.h"
#include "PlotSetup.h"
#include "WatermarkWidget.h"
#include "../algorithms/OpcCounter.h"
#include "../hardware/PinMap.h"
#include "../qcustomplot.h"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString chineseStandardButtonText(QDialogButtonBox::StandardButton button) {
    switch (button) {
    case QDialogButtonBox::Ok:              return QStringLiteral("确定");
    case QDialogButtonBox::Save:            return QStringLiteral("保存");
    case QDialogButtonBox::SaveAll:         return QStringLiteral("全部保存");
    case QDialogButtonBox::Open:            return QStringLiteral("打开");
    case QDialogButtonBox::Yes:             return QStringLiteral("是");
    case QDialogButtonBox::YesToAll:        return QStringLiteral("全部选是");
    case QDialogButtonBox::No:              return QStringLiteral("否");
    case QDialogButtonBox::NoToAll:         return QStringLiteral("全部选否");
    case QDialogButtonBox::Abort:           return QStringLiteral("中止");
    case QDialogButtonBox::Retry:           return QStringLiteral("重试");
    case QDialogButtonBox::Ignore:          return QStringLiteral("忽略");
    case QDialogButtonBox::Close:           return QStringLiteral("关闭");
    case QDialogButtonBox::Cancel:          return QStringLiteral("取消");
    case QDialogButtonBox::Discard:         return QStringLiteral("放弃");
    case QDialogButtonBox::Help:            return QStringLiteral("帮助");
    case QDialogButtonBox::Apply:           return QStringLiteral("应用");
    case QDialogButtonBox::Reset:           return QStringLiteral("重置");
    case QDialogButtonBox::RestoreDefaults: return QStringLiteral("恢复默认");
    default:                                return QString();
    }
}

class DialogUiFilter : public QObject {
public:
    explicit DialogUiFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::Show || !watched->inherits("QDialog")) {
            return QObject::eventFilter(watched, event);
        }

        QDialog *dialog = static_cast<QDialog *>(watched);
        if (QMessageBox *messageBox = qobject_cast<QMessageBox *>(dialog)) {
            if (QLabel *textLabel = messageBox->findChild<QLabel *>("qt_msgbox_label")) {
                textLabel->setWordWrap(true);
                textLabel->setMinimumWidth(440);
                textLabel->setMaximumWidth(620);
            }
            QTimer::singleShot(0, messageBox, [messageBox]() {
                messageBox->adjustSize();
            });
        }

        if (QFileDialog *fileDialog = qobject_cast<QFileDialog *>(dialog)) {
            fileDialog->setLabelText(QFileDialog::LookIn, QStringLiteral("查找位置："));
            fileDialog->setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));
            fileDialog->setLabelText(QFileDialog::FileType, QStringLiteral("文件类型："));
            fileDialog->setLabelText(
                QFileDialog::Accept,
                fileDialog->acceptMode() == QFileDialog::AcceptSave
                    ? QStringLiteral("保存")
                    : QStringLiteral("打开"));
            fileDialog->setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
        }

        const QList<QDialogButtonBox *> buttonBoxes = dialog->findChildren<QDialogButtonBox *>();
        for (QDialogButtonBox *buttonBox : buttonBoxes) {
            const QList<QAbstractButton *> buttons = buttonBox->buttons();
            for (QAbstractButton *button : buttons) {
                const QDialogButtonBox::StandardButton standardButton = buttonBox->standardButton(button);
                const QString translatedText = chineseStandardButtonText(standardButton);
                if (!translatedText.isEmpty()) button->setText(translatedText);

                const bool primary = standardButton == QDialogButtonBox::Ok ||
                                     standardButton == QDialogButtonBox::Save ||
                                     standardButton == QDialogButtonBox::SaveAll ||
                                     standardButton == QDialogButtonBox::Open ||
                                     standardButton == QDialogButtonBox::Yes ||
                                     standardButton == QDialogButtonBox::YesToAll ||
                                     standardButton == QDialogButtonBox::Apply ||
                                     standardButton == QDialogButtonBox::Retry;
                button->setProperty("dialogPrimary", primary);
                button->style()->unpolish(button);
                button->style()->polish(button);
            }
        }

        return QObject::eventFilter(watched, event);
    }
};

QPixmap loadHeaderLogo() {
    QPixmap pixmap;
    QString appDir = QCoreApplication::applicationDirPath();
    if (pixmap.isNull()) pixmap.load(appDir + "/buaa_header.png");
    if (pixmap.isNull()) pixmap.load(appDir + "/assets/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/CPC_1/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/CPC_Control_System/buaa_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/image.png");
    return pixmap;
}

QIcon createPowerIcon() {
    QPixmap pixmap(56, 56);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(80, 30, 30, 45));
    painter.drawEllipse(QRectF(6.0, 8.0, 44.0, 44.0));

    QLinearGradient powerGradient(8.0, 6.0, 48.0, 50.0);
    powerGradient.setColorAt(0.0, QColor("#E96A65"));
    powerGradient.setColorAt(1.0, QColor("#B93636"));
    painter.setBrush(powerGradient);
    painter.setPen(QPen(QColor("#A52F2F"), 1.2));
    painter.drawEllipse(QRectF(6.0, 5.0, 44.0, 44.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 3.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(28.0, 13.0), QPointF(28.0, 27.0));
    painter.drawArc(QRectF(15.0, 17.0, 26.0, 26.0), 135 * 16, 270 * 16);

    return QIcon(pixmap);
}

} // namespace

MainWindowUi buildMainWindow(QApplication& app, QMainWindow& window, OpcParams& opcParams) {
    MainWindowUi ui;

    app.installEventFilter(new DialogUiFilter(&app));

    window.setWindowTitle("CPC 纳米凝结核计数器总控面板");
    window.resize(1280, 720);
    app.setStyleSheet(
        "QMainWindow, QWidget { background-color: #F4F6F8; color: #333333; font-family: 'Microsoft YaHei', Arial; }"
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab { background: #E5E8EC; color: #2c3e50; min-width: 108px; min-height: 42px; padding: 6px 16px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; font-size: 16px; font-weight: bold; }"
        "QTabBar::tab:selected { background: #005bac; color: white; }"
        "QPushButton { min-height: 44px; border-radius: 6px; font-size: 15px; font-weight: bold; }"
        "QDialog { background-color: #FFFFFF; color: #2C3E50; }"
        "QMessageBox { background-color: #FFFFFF; }"
        "QMessageBox QLabel { background: transparent; color: #2C3E50; }"
        "QMessageBox QLabel#qt_msgbox_label { font-size: 17px; min-width: 440px; max-width: 620px; padding: 4px; }"
        "QDialogButtonBox { background: transparent; }"
        "QDialogButtonBox QPushButton { min-width: 96px; min-height: 38px; padding: 0 18px; background-color: #F2F5F7; color: #34495E; border: 1px solid #B8C4CE; border-radius: 6px; font-size: 15px; font-weight: bold; outline: none; }"
        "QDialogButtonBox QPushButton:hover { background-color: #E4EBF0; border-color: #8FA2B1; }"
        "QDialogButtonBox QPushButton:pressed { background-color: #D6E0E7; }"
        "QDialogButtonBox QPushButton:focus { outline: none; border: 1px solid #7F96A8; }"
        "QDialogButtonBox QPushButton[dialogPrimary=\"true\"] { background-color: #1769AA; color: #FFFFFF; border: 1px solid #1769AA; }"
        "QDialogButtonBox QPushButton[dialogPrimary=\"true\"]:hover { background-color: #12598F; border-color: #12598F; }"
        "QDialogButtonBox QPushButton[dialogPrimary=\"true\"]:pressed { background-color: #0D4977; border-color: #0D4977; }"
        "QFileDialog { min-width: 760px; min-height: 500px; }"
        "QFileDialog QLabel { background: transparent; color: #34495E; }"
        "QFileDialog QLineEdit, QFileDialog QComboBox { min-height: 32px; background-color: #FFFFFF; border: 1px solid #B8C4CE; border-radius: 4px; padding: 2px 6px; }"
    );

    QWidget *centralWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    QFrame *headerFrame = new QFrame();
    headerFrame->setObjectName("headerFrame");
    headerFrame->setStyleSheet(
        "QFrame#headerFrame { background-color: #FFFFFF; border: 1px solid #D6EAF8; border-radius: 5px; }"
    );
    headerFrame->setFixedHeight(68);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(14, 0, 14, 0);

    QLabel *lblLogo = new QLabel();
    QPixmap logoPixmap = loadHeaderLogo();
    if (!logoPixmap.isNull()) {
        lblLogo->setPixmap(logoPixmap.scaledToHeight(46, Qt::SmoothTransformation));
        lblLogo->setStyleSheet("background: transparent; border: none;");
    } else {
        lblLogo->setText("北京航空航天大学");
        lblLogo->setStyleSheet("color: #005bac; font-size: 20px; font-weight: bold;");
    }
    lblLogo->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    headerLayout->addWidget(lblLogo);

    QLabel *lblPlatformTitle = new QLabel(" | CPC主控平台");
    lblPlatformTitle->setStyleSheet("color: #005bac; font-size: 22px; font-weight: bold; background: transparent; border: none;");
    headerLayout->addWidget(lblPlatformTitle);
    headerLayout->addStretch();

    QWidget *headerStatusBar = new QWidget();
    headerStatusBar->setStyleSheet("background: transparent;");
    QHBoxLayout *headerStatusLayout = new QHBoxLayout(headerStatusBar);
    headerStatusLayout->setContentsMargins(0, 0, 8, 0);
    headerStatusLayout->setSpacing(12);
    auto addHeaderStatusIndicator = [&](const QString& title, QLabel *&lamp) {
        QWidget *indicator = new QWidget();
        indicator->setStyleSheet("background: transparent;");
        QHBoxLayout *indicatorLayout = new QHBoxLayout(indicator);
        indicatorLayout->setContentsMargins(0, 0, 0, 0);
        indicatorLayout->setSpacing(4);
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setStyleSheet("font-size: 14px; color: #34495E; font-weight: bold; background: transparent;");
        lamp = new QLabel();
        lamp->setFixedSize(20, 20);
        lamp->setStyleSheet("background-color: #D64541; border: 2px solid #A93226; border-radius: 10px;");
        indicatorLayout->addWidget(titleLabel);
        indicatorLayout->addWidget(lamp);
        headerStatusLayout->addWidget(indicator);
    };
    addHeaderStatusIndicator("冷凝段", ui.lblOverviewCondLamp);
    addHeaderStatusIndicator("饱和段", ui.lblOverviewSatLamp);
    addHeaderStatusIndicator("OPC段", ui.lblOverviewOpcLamp);
    addHeaderStatusIndicator("液位", ui.lblOverviewLiquidLamp);
    headerLayout->addWidget(headerStatusBar);

    QComboBox *pageSelector = new QComboBox();
    pageSelector->setFixedSize(150, 42);
    pageSelector->setFocusPolicy(Qt::NoFocus);
    pageSelector->setMaxVisibleItems(7);
    pageSelector->setToolTip("切换功能页面");
    pageSelector->setStyleSheet(
        "QComboBox { background-color: #FFFFFF; color: #2C3E50; border: 1px solid #AEBBC6; border-radius: 7px; padding: 5px 38px 5px 13px; font-size: 15px; font-weight: bold; }"
        "QComboBox:hover { border-color: #7F96A8; }"
        "QComboBox:on { border-color: #005BAC; }"
        "QComboBox:disabled { background-color: #EFF1F3; color: #9AA6AF; border-color: #CDD4DA; }"
        "QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: 34px; background-color: #F5F7F9; border-left: 1px solid #D5DDE4; border-top-right-radius: 7px; border-bottom-right-radius: 7px; }"
        "QComboBox::drop-down:hover { background-color: #E9EEF2; }"
        "QComboBox::down-arrow { image: url(:/ui/ui/dropdown_arrow.svg); width: 14px; height: 9px; }"
        "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #2C3E50; border: 1px solid #AEBBC6; border-radius: 7px; padding: 4px; outline: none; selection-background-color: #005BAC; selection-color: #FFFFFF; font-size: 15px; font-weight: bold; }"
        "QComboBox QAbstractItemView::item { min-height: 34px; padding: 3px 10px; }"
    );
    headerLayout->addWidget(pageSelector);

    ui.btnShutdown = new QToolButton();
    ui.btnShutdown->setIcon(createPowerIcon());
    ui.btnShutdown->setIconSize(QSize(46, 46));
    ui.btnShutdown->setFixedSize(54, 54);
    ui.btnShutdown->setAutoRaise(false);
    ui.btnShutdown->setToolTip("安全关闭树莓派");
    ui.btnShutdown->setStyleSheet(
        "QToolButton { background-color: #FFF7F7; border: 1px solid #E8C5C5; border-radius: 11px; padding: 2px; margin: 0; }"
        "QToolButton:hover { background-color: #FDEAEA; border-color: #D99090; }"
        "QToolButton:pressed { background-color: #F7DADA; border-color: #C86666; padding-top: 4px; }"
        "QToolButton:disabled { background-color: #F2F2F2; border-color: #D8D8D8; }"
        "QToolButton:focus { outline: none; }"
    );
    headerLayout->addWidget(ui.btnShutdown);
    mainLayout->addWidget(headerFrame);

    ui.tabs = new QTabWidget();
    ui.tabs->setDocumentMode(true);

    WatermarkWidget *overviewTab = new WatermarkWidget();
    QGridLayout *overviewLayout = new QGridLayout(overviewTab);
    overviewLayout->setContentsMargins(2, 8, 2, 2);
    overviewLayout->setSpacing(6);
    ui.lblParticleConcentration = new QLabel("-- 个/ml");
    ui.lblParticleConcentration->setStyleSheet("font-size: 42px; color: #0E8F78; font-weight: bold; font-family: 'Courier New';");
    ui.lblParticleConcentration->setAlignment(Qt::AlignCenter);
    ui.lblStatus = new QLabel("状态: 待机（执行器关闭）");
    ui.lblStatus->setAlignment(Qt::AlignCenter);
    ui.lblStatus->setWordWrap(true);
    ui.lblStatus->setStyleSheet("color: #E67E22; font-weight:bold; font-size: 14px;");
    QGroupBox *particleConcentrationCard = createOverviewCard("颗粒数目浓度", ui.lblParticleConcentration, "#16A085");

    QGroupBox *systemCard = new QGroupBox("系统采集");
    systemCard->setStyleSheet("QGroupBox { border: 2px solid #2980B9; border-radius: 7px; background-color: #FFFFFF; font-weight: bold; margin-top: 14px;} QGroupBox::title { color: #2980B9; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    systemCard->setMinimumHeight(82);
    systemCard->setMaximumHeight(118);
    QGridLayout *systemLayout = new QGridLayout(systemCard);
    systemLayout->setContentsMargins(8, 18, 8, 8);
    systemLayout->setSpacing(5);
    ui.btnAcqStart = new QPushButton("开始采集");
    ui.btnAcqStop = new QPushButton("停止采集");
    ui.btnSaveRaw = new QPushButton("保存数据");
    ui.btnAcqStart->setMinimumWidth(118);
    ui.btnAcqStop->setMinimumWidth(118);
    ui.btnSaveRaw->setMinimumWidth(118);
    ui.btnAcqStart->setFixedHeight(36);
    ui.btnAcqStop->setFixedHeight(36);
    ui.btnSaveRaw->setFixedHeight(36);
    ui.btnAcqStart->setStyleSheet("background-color: #2980B9; color: white; min-height: 34px; font-size: 13px;");
    ui.btnAcqStop->setStyleSheet("background-color: #95a5a6; color: white; min-height: 34px; font-size: 13px;");
    ui.btnSaveRaw->setStyleSheet("background-color: #566573; color: white; min-height: 34px; font-size: 13px;");
    ui.lblCaptureState = new QLabel("采集: 未启动");
    ui.lblCaptureState->setAlignment(Qt::AlignCenter);
    ui.lblCaptureState->setStyleSheet("font-size: 13px; font-weight: bold; color: #566573;");
    systemLayout->addWidget(ui.lblStatus, 0, 0, 1, 2);
    systemLayout->addWidget(ui.lblCaptureState, 0, 2);
    systemLayout->addWidget(ui.btnAcqStart, 1, 0);
    systemLayout->addWidget(ui.btnAcqStop, 1, 1);
    systemLayout->addWidget(ui.btnSaveRaw, 1, 2);

    ui.lblOverviewPump = createOverviewValue("关", "#27AE60");
    ui.lblOverviewFan = createOverviewValue("OPC: 关\n整机1: 关\n整机2: 关", "#16A085");

    ui.lblCompactDeviceState = new QLabel("气泵: 关    OPC风扇: 关    整机风扇1: 关    压差1/2/3: 预留");
    ui.lblCompactDeviceState->setAlignment(Qt::AlignCenter);
    ui.lblCompactDeviceState->setStyleSheet("font-size: 12px; color: #566573; background: #FFFFFF; border: 1px solid #D5DBDB; border-radius: 6px; padding: 3px;");
    ui.lblCompactDeviceState->setMaximumHeight(24);

    ui.particleConcentrationPlot = new QCustomPlot();
    ui.particleConcentrationPlot->setMinimumHeight(410);
    setupParticleConcentrationPlot(ui.particleConcentrationPlot);
    QWidget *particlePlotPanel = new QWidget();
    QVBoxLayout *particlePlotPanelLayout = new QVBoxLayout(particlePlotPanel);
    particlePlotPanelLayout->setContentsMargins(0, 0, 0, 0);
    particlePlotPanelLayout->setSpacing(3);
    QHBoxLayout *particlePlotToolbar = new QHBoxLayout();
    particlePlotToolbar->setContentsMargins(0, 0, 0, 0);
    ui.btnResetParticlePlot = new QPushButton("还原视图");
    ui.btnResetParticlePlot->setFixedSize(88, 28);
    ui.btnResetParticlePlot->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #2c3e50; border: 1px solid #B8C2CC; border-radius: 5px; min-height: 24px; font-size: 12px; font-weight: bold; } QPushButton:pressed { background-color: #E5E8EC; }");
    particlePlotToolbar->addStretch();
    particlePlotToolbar->addWidget(ui.btnResetParticlePlot);
    particlePlotPanelLayout->addLayout(particlePlotToolbar);
    particlePlotPanelLayout->addWidget(ui.particleConcentrationPlot, 1);

    overviewLayout->addWidget(particleConcentrationCard, 0, 0, 1, 3);
    overviewLayout->addWidget(systemCard, 0, 3, 1, 3);
    overviewLayout->addWidget(ui.lblCompactDeviceState, 1, 0, 1, 6);
    overviewLayout->addWidget(particlePlotPanel, 2, 0, 1, 6);
    for (int column = 0; column < 6; ++column) overviewLayout->setColumnStretch(column, 1);
    overviewLayout->setRowStretch(0, 0);
    overviewLayout->setRowStretch(1, 0);
    overviewLayout->setRowStretch(2, 8);
    QTimer::singleShot(0, overviewTab, [overviewTab]() { overviewTab->raiseWatermark(); });
    ui.tabs->addTab(overviewTab, "总览");

    QWidget *tempTab = new QWidget();
    QGridLayout *tempLayout = new QGridLayout(tempTab);
    tempLayout->setContentsMargins(2, 8, 2, 2);
    tempLayout->setSpacing(10);

    QGroupBox *condGroup = createTempGroup("冷凝段 (制冷)", "#3498DB", ui.sbCond, ui.btnCondStart, ui.btnCondStop, ui.lblCondTemp, ui.lblCondPwm);
    QGroupBox *satGroup = createTempGroup("饱和段 (加热)", "#E74C3C", ui.sbSat, ui.btnSatStart, ui.btnSatStop, ui.lblSatTemp, ui.lblSatPwm);
    QGroupBox *opcGroup = createTempGroup("OPC段 (温度监测)", "#F39C12", ui.sbOpc, ui.btnOpcStart, ui.btnOpcStop, ui.lblOpcTemp, ui.lblOpcPwm);

    ui.sbCond->setValue(10.0);
    ui.sbSat->setValue(40.0);
    ui.sbOpc->setValue(40.0);
    ui.sbOpc->setEnabled(false);
    ui.btnOpcStart->setEnabled(false);
    ui.btnOpcStop->setEnabled(false);
    ui.lblOpcPwm->setText(QString("GPIO%1 功率: 0 %").arg(PinMap::PIN_OPC_HEATER_PWM));

    QLabel *tempHint = new QLabel("温控执行器默认关闭。OPC 段启动时 GPIO6 满功率输出，停止时关闭。");
    tempHint->setWordWrap(true);
    tempHint->setStyleSheet("font-size: 15px; color: #566573; padding: 6px;");
    tempLayout->addWidget(condGroup, 0, 0);
    tempLayout->addWidget(satGroup, 0, 1);
    tempLayout->addWidget(opcGroup, 0, 2);
    tempLayout->setRowStretch(3, 1);

    QGroupBox *auxGroup = new QGroupBox("风扇控制");
    auxGroup->setStyleSheet("QGroupBox { border: 2px solid #16A085; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #16A085; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QGridLayout *auxLayout = new QGridLayout(auxGroup);
    auxLayout->setContentsMargins(10, 28, 10, 10);
    auxLayout->setSpacing(10);
    ui.btnOpcFanStart = new QPushButton(QString("OPC风扇开 G%1").arg(PinMap::PIN_OPC_FAN));
    ui.btnOpcFanStop = new QPushButton("OPC风扇关");
    ui.btnCaseFan1Start = new QPushButton(QString("整机风扇1开 G%1").arg(PinMap::PIN_CASE_FAN_1));
    ui.btnCaseFan1Stop = new QPushButton("整机风扇1关");
    ui.btnOpcFanStart->setMinimumWidth(170);
    ui.btnOpcFanStop->setMinimumWidth(170);
    ui.btnCaseFan1Start->setMinimumWidth(170);
    ui.btnCaseFan1Stop->setMinimumWidth(170);
    ui.lblAuxState = new QLabel("OPC风扇: 关 | 整机风扇1: 关");
    ui.lblAuxState->setStyleSheet("font-size: 13px; color: #2c3e50;");
    auxLayout->addWidget(ui.btnOpcFanStart, 0, 0);
    auxLayout->addWidget(ui.btnOpcFanStop, 0, 1);
    auxLayout->addWidget(ui.btnCaseFan1Start, 1, 0);
    auxLayout->addWidget(ui.btnCaseFan1Stop, 1, 1);
    auxLayout->addWidget(ui.lblAuxState, 3, 0, 1, 2);
    tempLayout->addWidget(auxGroup, 1, 0, 1, 3);
    tempLayout->addWidget(tempHint, 2, 0, 1, 3);
    ui.tabs->addTab(tempTab, "温控");

    QWidget *gasTab = new QWidget();
    QVBoxLayout *gasMainLayout = new QVBoxLayout(gasTab);
    gasMainLayout->setContentsMargins(2, 8, 2, 2);
    gasMainLayout->setSpacing(10);
    QGroupBox *pumpGroup = new QGroupBox("气路动力控制");
    pumpGroup->setStyleSheet("QGroupBox { border: 2px solid #27AE60; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #27AE60; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QGridLayout *pumpLayout = new QGridLayout(pumpGroup);
    pumpLayout->setContentsMargins(10, 28, 10, 10);
    pumpLayout->setSpacing(10);
    ui.btnPumpStart = new QPushButton("启动气泵");
    ui.btnPumpStop = new QPushButton("停止气泵");
    ui.btnPumpStart->setStyleSheet("background-color: #27AE60; color: white; border-radius: 6px; font-weight:bold;");
    ui.btnPumpStop->setStyleSheet("background-color: #95a5a6; color: white; border-radius: 6px; font-weight:bold;");
    ui.sliderPump = new QSlider(Qt::Horizontal);
    ui.sliderPump->setRange(0, 100);
    ui.sliderPump->setValue(30);
    ui.lblPumpValue = new QLabel("30 %");
    ui.lblPumpValue->setStyleSheet("font-size: 18px; color: #27AE60; font-weight: bold;");
    QLabel *pumpPowerLabel = new QLabel("抽气功率");
    pumpPowerLabel->setStyleSheet("font-size: 15px; color: #566573;");
    pumpLayout->addWidget(ui.btnPumpStart, 0, 0);
    pumpLayout->addWidget(ui.btnPumpStop, 0, 1);
    pumpLayout->addWidget(pumpPowerLabel, 1, 0);
    pumpLayout->addWidget(ui.sliderPump, 1, 1, 1, 2);
    pumpLayout->addWidget(ui.lblPumpValue, 1, 3);

    QGroupBox *valveGroup = new QGroupBox("比例阀开度控制 (N4IOA01)");
    valveGroup->setStyleSheet("QGroupBox { border: 2px solid #2980B9; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #2980B9; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QGridLayout *valveLayout = new QGridLayout(valveGroup);
    valveLayout->setContentsMargins(10, 28, 10, 10);
    valveLayout->setSpacing(10);
    QLabel *valveOpeningLabel = new QLabel("目标开度");
    valveOpeningLabel->setStyleSheet("font-size: 15px; color: #566573;");
    ui.sbValveOpening = new QDoubleSpinBox();
    ui.sbValveOpening->setRange(0.0, 100.0);
    ui.sbValveOpening->setDecimals(1);
    ui.sbValveOpening->setSingleStep(1.0);
    ui.sbValveOpening->setSuffix(" %");
    ui.sbValveOpening->setValue(0.0);
    ui.sbValveOpening->setMinimumHeight(42);
    ui.sbValveOpening->setStyleSheet("QDoubleSpinBox { padding: 5px; border: 1px solid #bdc3c7; border-radius: 5px; font-size: 16px; }");
    ui.lblValveCurrent = new QLabel("对应输出: 4.00 mA");
    ui.lblValveCurrent->setStyleSheet("font-size: 17px; color: #2980B9; font-weight: bold;");
    ui.btnValveApply = new QPushButton("设置开度");
    ui.btnValveRead = new QPushButton("读取输出");
    ui.btnValveClose = new QPushButton("安全关闭 (4 mA)");
    ui.btnValveApply->setStyleSheet("background-color: #2980B9; color: white; border-radius: 6px; font-weight: bold;");
    ui.btnValveRead->setStyleSheet("background-color: #566573; color: white; border-radius: 6px; font-weight: bold;");
    ui.btnValveClose->setStyleSheet("background-color: #C0392B; color: white; border-radius: 6px; font-weight: bold;");
    ui.lblValveStatus = new QLabel("串口 /dev/ttyAMA0 | 9600-8N1 | 模块地址 0x01 | 尚未通信");
    ui.lblValveStatus->setWordWrap(true);
    ui.lblValveStatus->setStyleSheet("font-size: 13px; color: #566573;");
    valveLayout->addWidget(valveOpeningLabel, 0, 0);
    valveLayout->addWidget(ui.sbValveOpening, 0, 1);
    valveLayout->addWidget(ui.lblValveCurrent, 0, 2);
    valveLayout->addWidget(ui.btnValveApply, 1, 0);
    valveLayout->addWidget(ui.btnValveRead, 1, 1);
    valveLayout->addWidget(ui.btnValveClose, 1, 2);
    valveLayout->addWidget(ui.lblValveStatus, 2, 0, 1, 3);
    valveLayout->setColumnStretch(1, 1);
    valveLayout->setColumnStretch(2, 1);

    QGroupBox *pressureGroup = new QGroupBox("压差传感器预留");
    pressureGroup->setStyleSheet("QGroupBox { border: 2px solid #7F8C8D; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #7F8C8D; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QGridLayout *pressureLayout = new QGridLayout(pressureGroup);
    pressureLayout->setContentsMargins(10, 28, 10, 10);
    pressureLayout->setSpacing(10);
    QStringList pressureNames = {"压差传感器 1", "压差传感器 2", "压差传感器 3"};
    for (int i = 0; i < pressureNames.size(); ++i) {
        QLabel *nameLabel = new QLabel(pressureNames.at(i));
        nameLabel->setStyleSheet("font-size: 15px; color: #2c3e50; font-weight: bold;");
        QLabel *valueLabel = new QLabel("预留，待选型后分配专属 ADC 芯片");
        valueLabel->setStyleSheet("font-size: 15px; color: #566573;");
        pressureLayout->addWidget(nameLabel, i, 0);
        pressureLayout->addWidget(valueLabel, i, 1);
    }
    pressureLayout->setColumnStretch(1, 1);

    gasMainLayout->addWidget(pumpGroup);
    gasMainLayout->addWidget(valveGroup);
    gasMainLayout->addWidget(pressureGroup);
    gasMainLayout->addStretch();
    ui.tabs->addTab(gasTab, "气路");

    QWidget *liquidTab = new QWidget();
    QVBoxLayout *liquidTabLayout = new QVBoxLayout(liquidTab);
    liquidTabLayout->setContentsMargins(2, 8, 2, 2);
    liquidTabLayout->setSpacing(10);
    QGroupBox *liquidGroup = new QGroupBox("液位与补液监控");
    liquidGroup->setStyleSheet("QGroupBox { border: 2px solid #F39C12; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #F39C12; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QVBoxLayout *liquidLayout = new QVBoxLayout(liquidGroup);
    liquidLayout->setContentsMargins(10, 28, 10, 10);
    liquidLayout->setSpacing(10);
    QHBoxLayout *liquidButtonLayout = new QHBoxLayout();
    liquidButtonLayout->setSpacing(10);
    ui.btnLiquidStart = new QPushButton("开始监控液位");
    ui.btnLiquidStop = new QPushButton("停止液位监控");
    ui.btnDrain = new QPushButton("按住排液");
    ui.btnLiquidStart->setVisible(false);
    ui.btnLiquidStop->setVisible(false);
    ui.btnDrain->setStyleSheet("QPushButton { background-color: #C0392B; color: white; border-radius: 6px; font-weight:bold; } QPushButton:pressed { background-color: #922B21; }");
    ui.lblLiquidState = new QLabel("异常");
    ui.lblLiquidState->setStyleSheet("font-size: 16px; color: #2c3e50;");
    ui.liquidLog = new QTextBrowser();
    ui.liquidLog->setMinimumHeight(330);
    ui.liquidLog->setStyleSheet("font-size: 12px; background-color: #F8F9F9;");
    liquidButtonLayout->addWidget(ui.btnLiquidStart);
    liquidButtonLayout->addWidget(ui.btnLiquidStop);
    liquidButtonLayout->addWidget(ui.btnDrain);
    liquidLayout->addLayout(liquidButtonLayout);
    liquidLayout->addWidget(ui.lblLiquidState);
    liquidLayout->addWidget(ui.liquidLog);
    liquidTabLayout->addWidget(liquidGroup);
    ui.tabs->addTab(liquidTab, "液位");

    QWidget *algorithmTab = new QWidget();
    QVBoxLayout *algorithmLayout = new QVBoxLayout(algorithmTab);
    algorithmLayout->setContentsMargins(2, 8, 2, 2);
    algorithmLayout->setSpacing(10);
    QGroupBox *algorithmGroup = new QGroupBox("OPC 算法设置");
    algorithmGroup->setStyleSheet("QGroupBox { border: 2px solid #2E86C1; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #2E86C1; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QGridLayout *algorithmGrid = new QGridLayout(algorithmGroup);
    algorithmGrid->setContentsMargins(12, 30, 12, 12);
    algorithmGrid->setSpacing(10);

    QDoubleSpinBox *sbCutoff = new QDoubleSpinBox();
    sbCutoff->setRange(0.001, 2.0);
    sbCutoff->setDecimals(4);
    sbCutoff->setSingleStep(0.005);
    sbCutoff->setSuffix(" V");
    sbCutoff->setValue(opcParams.minRange);

    QDoubleSpinBox *sbCutoffOffset = new QDoubleSpinBox();
    sbCutoffOffset->setRange(-1.0, 1.0);
    sbCutoffOffset->setDecimals(4);
    sbCutoffOffset->setSingleStep(0.001);
    sbCutoffOffset->setSuffix(" V");
    sbCutoffOffset->setValue(opcParams.thresholdOffset);

    QDoubleSpinBox *sbCutoffInterval = new QDoubleSpinBox();
    sbCutoffInterval->setRange(1.0, 1000.0);
    sbCutoffInterval->setDecimals(1);
    sbCutoffInterval->setSingleStep(1.0);
    sbCutoffInterval->setSuffix(" ms");
    sbCutoffInterval->setValue(opcParams.windowMs);

    QLabel *lblAlgorithmSummary = new QLabel();
    lblAlgorithmSummary->setStyleSheet("font-size: 15px; color: #2c3e50; font-weight: bold; padding: 8px; background: #F8F9F9; border: 1px solid #D5DBDB; border-radius: 6px;");
    lblAlgorithmSummary->setWordWrap(true);

    auto addAlgorithmControl = [&](int row, const QString& title, QDoubleSpinBox *spinBox) {
        QLabel *label = new QLabel(title);
        label->setStyleSheet("font-size: 15px; color: #2c3e50; font-weight: bold;");
        spinBox->setMinimumHeight(42);
        spinBox->setStyleSheet("QDoubleSpinBox { padding: 5px; border: 1px solid #bdc3c7; border-radius: 5px; font-size: 15px; }");
        algorithmGrid->addWidget(label, row, 0);
        algorithmGrid->addWidget(spinBox, row, 1);
    };

    addAlgorithmControl(0, "阈值下限 cutoff", sbCutoff);
    addAlgorithmControl(1, "阈值偏置 offset", sbCutoffOffset);
    addAlgorithmControl(2, "阈值计算间隔", sbCutoffInterval);
    algorithmGrid->addWidget(lblAlgorithmSummary, 3, 0, 1, 2);
    algorithmGrid->setColumnStretch(1, 1);

    auto refreshAlgorithmSummary = [lblAlgorithmSummary, &opcParams]() {
        lblAlgorithmSummary->setText(QString("阈值 = 本底 + clamp(噪声宽度, %1 V, %2 V) + %3 V    计算间隔: %4 ms")
            .arg(opcParams.minRange, 0, 'f', 4)
            .arg(opcParams.maxRange, 0, 'f', 4)
            .arg(opcParams.thresholdOffset, 0, 'f', 4)
            .arg(opcParams.windowMs, 0, 'f', 1));
    };

    QObject::connect(sbCutoff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.minRange = val;
        refreshAlgorithmSummary();
    });
    QObject::connect(sbCutoffOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.thresholdOffset = val;
        refreshAlgorithmSummary();
    });
    QObject::connect(sbCutoffInterval, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.windowMs = val;
        refreshAlgorithmSummary();
    });
    refreshAlgorithmSummary();

    algorithmLayout->addWidget(algorithmGroup);
    algorithmLayout->addStretch();
    ui.tabs->addTab(algorithmTab, "算法");

    ui.opcTab = new QWidget();
    QVBoxLayout *opcLayout = new QVBoxLayout(ui.opcTab);
    opcLayout->setContentsMargins(2, 8, 2, 2);
    opcLayout->setSpacing(10);
    QGroupBox *opcControlGroup = new QGroupBox("OPC 原始信号");
    opcControlGroup->setStyleSheet("QGroupBox { border: 2px solid #8E44AD; border-radius: 8px; background-color: #FFFFFF; font-weight: bold; margin-top: 18px; } QGroupBox::title { color: #8E44AD; subcontrol-origin: margin; left: 12px; padding: 0 6px;}");
    QVBoxLayout *opcControlLayout = new QVBoxLayout(opcControlGroup);
    opcControlLayout->setContentsMargins(12, 30, 12, 12);
    opcControlLayout->setSpacing(8);
    QLabel *lblProcess = new QLabel("空气入口 -> 饱和段 -> 冷凝段 -> OPC 光腔");
    lblProcess->setAlignment(Qt::AlignCenter);
    lblProcess->setStyleSheet("color: #566573; font-size: 16px;");
    ui.opcPlot = new QCustomPlot();
    ui.opcPlot->setMinimumHeight(470);
    setupOpcPlot(ui.opcPlot);
    opcControlLayout->addWidget(lblProcess);
    opcControlLayout->addWidget(ui.opcPlot, 1);
    opcLayout->addWidget(opcControlGroup);
    ui.tabs->addTab(ui.opcTab, "OPC");

    ui.tabs->tabBar()->hide();
    for (int i = 0; i < ui.tabs->count(); ++i) {
        pageSelector->addItem(ui.tabs->tabText(i));
    }
    QObject::connect(pageSelector, QOverload<int>::of(&QComboBox::activated), ui.tabs, &QTabWidget::setCurrentIndex);
    QObject::connect(ui.tabs, &QTabWidget::currentChanged, pageSelector, [pageSelector](int index) {
        pageSelector->setCurrentIndex(index);
    });

    mainLayout->addWidget(ui.tabs, 1);
    window.setCentralWidget(centralWidget);

    return ui;
}
