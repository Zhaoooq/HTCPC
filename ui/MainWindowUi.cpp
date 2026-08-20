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
#include <QFont>
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
#include <QScrollArea>
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

QString cardStyle(const QString& accent) {
    return QString(
        "QGroupBox { border: 1px solid #DCE4EA; border-top: 3px solid %1; "
        "border-radius: 10px; background-color: #FFFFFF; font-weight: bold; "
        "margin-top: 28px; }"
        "QGroupBox::title { color: %1; subcontrol-origin: margin; "
        "subcontrol-position: top left; left: 10px; padding: 0; background: transparent; }")
        .arg(accent);
}

QString solidButtonStyle(const QString& color, const QString& hoverColor) {
    return QString(
        "QPushButton { background-color: %1; color: #FFFFFF; border: none; "
        "border-radius: 7px; font-weight: bold; padding: 0 12px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %2; padding-top: 2px; }"
        "QPushButton:disabled { background-color: #D8DEE3; color: #8A969F; }")
        .arg(color, hoverColor);
}

} // namespace

MainWindowUi buildMainWindow(QApplication& app, QMainWindow& window, OpcParams& opcParams) {
    MainWindowUi ui;

    app.installEventFilter(new DialogUiFilter(&app));

    window.setWindowTitle("CPC 纳米凝结核计数器总控面板");
    window.resize(1280, 720);
    app.setFont(QFont("WenQuanYi Micro Hei", 10));
    app.setStyleSheet(
        "QMainWindow, QWidget { background-color: #EEF2F6; color: #263746; font-family: 'WenQuanYi Micro Hei'; }"
        "QLabel { background: transparent; }"
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab { background: #E5E8EC; color: #2c3e50; min-width: 108px; min-height: 42px; padding: 6px 16px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; font-size: 16px; font-weight: bold; }"
        "QTabBar::tab:selected { background: #005bac; color: white; }"
        "QPushButton { min-height: 42px; border: 1px solid #CBD5DC; border-radius: 7px; "
        "background-color: #FFFFFF; color: #34495E; font-size: 14px; font-weight: bold; padding: 0 12px; }"
        "QPushButton:hover { background-color: #F4F8FB; border-color: #91A5B4; }"
        "QPushButton:pressed { background-color: #E6EDF2; }"
        "QPushButton:disabled { background-color: #E8ECEF; color: #98A3AB; border-color: #D8DEE3; }"
        "QDoubleSpinBox, QComboBox { min-height: 36px; background-color: #FFFFFF; color: #2C3E50; "
        "border: 1px solid #C7D1D9; border-radius: 6px; padding: 2px 8px; font-size: 14px; }"
        "QDoubleSpinBox:hover, QComboBox:hover { border-color: #7F96A8; }"
        "QDoubleSpinBox:focus, QComboBox:focus { border: 2px solid #2E86C1; }"
        "QDoubleSpinBox:disabled, QComboBox:disabled { background-color: #E8ECEF; color: #929DA5; }"
        "QSlider::groove:horizontal { height: 6px; background: #D9E1E7; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #2E86C1; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 18px; margin: -6px 0; background: #FFFFFF; "
        "border: 2px solid #2E86C1; border-radius: 9px; }"
        "QTextBrowser { background-color: #F8FAFB; border: 1px solid #D7E0E6; border-radius: 8px; padding: 8px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #B8C4CD; border-radius: 5px; min-height: 28px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QToolTip { background-color: #263746; color: #FFFFFF; border: none; border-radius: 4px; padding: 5px 8px; }"
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
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    QFrame *headerFrame = new QFrame();
    headerFrame->setObjectName("headerFrame");
    headerFrame->setStyleSheet(
        "QFrame#headerFrame { background-color: #FFFFFF; border: 1px solid #D8E2E9; border-radius: 10px; }"
    );
    headerFrame->setFixedHeight(70);
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
    overviewLayout->setContentsMargins(4, 8, 4, 4);
    overviewLayout->setSpacing(8);
    ui.lblParticleConcentration = new QLabel("-- 个/ml");
    ui.lblParticleConcentration->setStyleSheet("font-size: 42px; color: #0E8F78; font-weight: bold; font-family: 'WenQuanYi Micro Hei';");
    ui.lblParticleConcentration->setAlignment(Qt::AlignCenter);
    ui.lblStatus = new QLabel("状态: 待机（执行器关闭）");
    ui.lblStatus->setAlignment(Qt::AlignCenter);
    ui.lblStatus->setWordWrap(true);
    ui.lblStatus->setStyleSheet("color: #E67E22; font-weight:bold; font-size: 14px;");
    QGroupBox *particleConcentrationCard = createOverviewCard("颗粒数目浓度", ui.lblParticleConcentration, "#16A085");

    QGroupBox *systemCard = new QGroupBox("系统采集");
    systemCard->setStyleSheet(cardStyle("#2980B9"));
    systemCard->setFixedHeight(118);
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
    ui.btnAcqStart->setStyleSheet(solidButtonStyle("#2980B9", "#21618C"));
    ui.btnAcqStop->setStyleSheet(solidButtonStyle("#7F8C8D", "#626F70"));
    ui.btnSaveRaw->setStyleSheet(solidButtonStyle("#566573", "#3F4D55"));
    ui.lblCaptureState = new QLabel("采集: 未启动");
    ui.lblCaptureState->setAlignment(Qt::AlignCenter);
    ui.lblCaptureState->setStyleSheet("font-size: 13px; font-weight: bold; color: #566573;");
    systemLayout->addWidget(ui.lblStatus, 0, 0, 1, 2);
    systemLayout->addWidget(ui.lblCaptureState, 0, 2);
    systemLayout->addWidget(ui.btnAcqStart, 1, 0);
    systemLayout->addWidget(ui.btnAcqStop, 1, 1);
    systemLayout->addWidget(ui.btnSaveRaw, 1, 2);

    ui.lblOverviewPump = createOverviewValue("关", "#27AE60");
    ui.lblOverviewAux = createOverviewValue("OPC风扇: 关\n旁路: 0.3 L/min", "#16A085");

    ui.lblCompactDeviceState = new QLabel("气泵: 关    OPC风扇: 关    旁路: 0.3 L/min    压差 A0:初始化  A1:初始化  A2:初始化");
    ui.lblCompactDeviceState->setAlignment(Qt::AlignCenter);
    ui.lblCompactDeviceState->setStyleSheet("font-size: 12px; color: #526471; background: #FFFFFF; border: 1px solid #D8E1E7; border-radius: 7px; padding: 5px 10px;");
    ui.lblCompactDeviceState->setMaximumHeight(28);

    ui.particleConcentrationPlot = new QCustomPlot();
    ui.particleConcentrationPlot->setMinimumHeight(410);
    setupParticleConcentrationPlot(ui.particleConcentrationPlot);
    QWidget *particlePlotPanel = new QWidget();
    particlePlotPanel->setObjectName("particlePlotPanel");
    particlePlotPanel->setStyleSheet(
        "QWidget#particlePlotPanel { background-color: #FFFFFF; border: 1px solid #DCE4EA; border-radius: 10px; }");
    QVBoxLayout *particlePlotPanelLayout = new QVBoxLayout(particlePlotPanel);
    particlePlotPanelLayout->setContentsMargins(10, 8, 10, 10);
    particlePlotPanelLayout->setSpacing(6);
    QHBoxLayout *particlePlotToolbar = new QHBoxLayout();
    particlePlotToolbar->setContentsMargins(0, 0, 0, 0);
    QLabel *particlePlotTitle = new QLabel("浓度趋势");
    particlePlotTitle->setStyleSheet("font-size: 14px; color: #3C596B; font-weight: bold;");
    ui.btnResetParticlePlot = new QPushButton("还原视图");
    ui.btnResetParticlePlot->setFixedSize(88, 28);
    ui.btnResetParticlePlot->setStyleSheet("QPushButton { background-color: #F7FAFC; color: #3C596B; border: 1px solid #C8D4DC; border-radius: 5px; min-height: 24px; font-size: 12px; font-weight: bold; } QPushButton:hover { background-color: #EAF2F7; border-color: #8FA7B7; } QPushButton:pressed { background-color: #DDE8EF; }");
    particlePlotToolbar->addWidget(particlePlotTitle);
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
    tempLayout->setContentsMargins(4, 8, 4, 4);
    tempLayout->setSpacing(8);

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
    tempHint->setStyleSheet("font-size: 13px; color: #526471; padding: 8px 12px; background: #EAF2F8; border: 1px solid #D4E6F1; border-radius: 7px;");
    tempLayout->addWidget(condGroup, 0, 0);
    tempLayout->addWidget(satGroup, 0, 1);
    tempLayout->addWidget(opcGroup, 0, 2);
    tempLayout->setRowStretch(3, 1);

    QGroupBox *auxGroup = new QGroupBox("辅助设备与流量模式");
    auxGroup->setStyleSheet(cardStyle("#16A085"));
    QGridLayout *auxLayout = new QGridLayout(auxGroup);
    auxLayout->setContentsMargins(10, 28, 10, 10);
    auxLayout->setSpacing(10);
    ui.btnOpcFanStart = new QPushButton(QString("OPC风扇开 G%1").arg(PinMap::PIN_OPC_FAN));
    ui.btnOpcFanStop = new QPushButton("OPC风扇关");
    ui.btnBypassHighFlow = new QPushButton(
        QString("大流量 1.5 L/min  G%1高电平").arg(PinMap::PIN_BYPASS_VALVE));
    ui.btnBypassLowFlow = new QPushButton(
        QString("小流量 0.3 L/min  G%1低电平").arg(PinMap::PIN_BYPASS_VALVE));
    ui.btnOpcFanStart->setMinimumWidth(170);
    ui.btnOpcFanStop->setMinimumWidth(170);
    ui.btnBypassHighFlow->setMinimumWidth(210);
    ui.btnBypassLowFlow->setMinimumWidth(210);
    ui.btnOpcFanStart->setStyleSheet(solidButtonStyle("#16A085", "#117864"));
    ui.btnBypassHighFlow->setStyleSheet(solidButtonStyle("#16A085", "#117864"));
    ui.btnOpcFanStop->setStyleSheet(solidButtonStyle("#7F8C8D", "#626F70"));
    ui.btnBypassLowFlow->setStyleSheet(solidButtonStyle("#7F8C8D", "#626F70"));
    ui.lblAuxState = new QLabel("OPC风扇: 关 | 旁路模式: 小流量 0.3 L/min");
    ui.lblAuxState->setAlignment(Qt::AlignCenter);
    ui.lblAuxState->setStyleSheet("font-size: 13px; color: #3C596B; background: #F7FAFC; border: 1px solid #DCE4EA; border-radius: 6px; padding: 6px;");
    auxLayout->addWidget(ui.btnOpcFanStart, 0, 0);
    auxLayout->addWidget(ui.btnOpcFanStop, 0, 1);
    auxLayout->addWidget(ui.btnBypassHighFlow, 1, 0);
    auxLayout->addWidget(ui.btnBypassLowFlow, 1, 1);
    auxLayout->addWidget(ui.lblAuxState, 3, 0, 1, 2);
    tempLayout->addWidget(auxGroup, 1, 0, 1, 3);
    tempLayout->addWidget(tempHint, 2, 0, 1, 3);
    ui.tabs->addTab(tempTab, "温控");

    QWidget *gasTab = new QWidget();
    QVBoxLayout *gasTabLayout = new QVBoxLayout(gasTab);
    gasTabLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *gasContent = new QWidget();
    QVBoxLayout *gasMainLayout = new QVBoxLayout(gasContent);
    gasMainLayout->setContentsMargins(2, 8, 2, 2);
    gasMainLayout->setSpacing(6);
    QGroupBox *pumpGroup = new QGroupBox("气泵");
    pumpGroup->setStyleSheet(cardStyle("#27AE60"));
    QGridLayout *pumpLayout = new QGridLayout(pumpGroup);
    pumpLayout->setContentsMargins(10, 24, 10, 8);
    pumpLayout->setSpacing(8);
    ui.btnPumpStart = new QPushButton("启动气泵");
    ui.btnPumpStop = new QPushButton("停止气泵");
    ui.btnPumpStart->setStyleSheet(solidButtonStyle("#27AE60", "#1E8449"));
    ui.btnPumpStop->setStyleSheet(solidButtonStyle("#7F8C8D", "#626F70"));
    ui.sliderPump = new QSlider(Qt::Horizontal);
    ui.sliderPump->setRange(0, 100);
    ui.sliderPump->setValue(30);
    ui.lblPumpValue = new QLabel("30 %");
    ui.lblPumpValue->setStyleSheet("font-size: 18px; color: #27AE60; font-weight: bold;");
    QLabel *pumpPowerLabel = new QLabel("抽气功率");
    pumpPowerLabel->setStyleSheet("font-size: 15px; color: #566573;");
    pumpLayout->addWidget(pumpPowerLabel, 0, 0);
    pumpLayout->addWidget(ui.sliderPump, 0, 1);
    pumpLayout->addWidget(ui.lblPumpValue, 0, 2);
    pumpLayout->addWidget(ui.btnPumpStart, 1, 0, 1, 2);
    pumpLayout->addWidget(ui.btnPumpStop, 1, 2);
    pumpLayout->setColumnStretch(1, 1);

    QGroupBox *valveGroup = new QGroupBox("比例阀");
    valveGroup->setStyleSheet(cardStyle("#2980B9"));
    QGridLayout *valveLayout = new QGridLayout(valveGroup);
    valveLayout->setContentsMargins(10, 24, 10, 8);
    valveLayout->setSpacing(8);
    QLabel *valveOpeningLabel = new QLabel("开度");
    valveOpeningLabel->setStyleSheet("font-size: 15px; color: #566573;");
    ui.sbValveOpening = new QDoubleSpinBox();
    ui.sbValveOpening->setRange(0.0, 100.0);
    ui.sbValveOpening->setDecimals(1);
    ui.sbValveOpening->setSingleStep(1.0);
    ui.sbValveOpening->setSuffix(" %");
    ui.sbValveOpening->setValue(0.0);
    ui.sbValveOpening->setMinimumHeight(36);
    ui.sbValveOpening->setStyleSheet("QDoubleSpinBox { padding: 5px; border: 1px solid #bdc3c7; border-radius: 5px; font-size: 16px; }");
    ui.lblValveCurrent = new QLabel("4.00 mA");
    ui.lblValveCurrent->setStyleSheet("font-size: 17px; color: #2980B9; font-weight: bold;");
    ui.btnValveApply = new QPushButton("设置");
    ui.btnValveRead = new QPushButton("读取");
    ui.btnValveClose = new QPushButton("安全关闭");
    ui.btnValveApply->setStyleSheet(solidButtonStyle("#2980B9", "#21618C"));
    ui.btnValveRead->setStyleSheet(solidButtonStyle("#566573", "#3F4D55"));
    ui.btnValveClose->setStyleSheet(solidButtonStyle("#C0392B", "#922B21"));
    ui.lblValveStatus = new QLabel("N4IOA01 · /dev/ttyAMA0 · 尚未通信");
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

    QGroupBox *pressureControlGroup = new QGroupBox("目标压差闭环控制");
    pressureControlGroup->setStyleSheet(cardStyle("#117A65"));
    QGridLayout *pressureControlLayout = new QGridLayout(pressureControlGroup);
    pressureControlLayout->setContentsMargins(12, 14, 12, 10);
    pressureControlLayout->setHorizontalSpacing(8);
    pressureControlLayout->setVerticalSpacing(6);
    ui.cmbPressureControlChannel = new QComboBox();
    ui.cmbPressureControlChannel->addItem("A0  0~40 kPa", 0);
    ui.cmbPressureControlChannel->addItem("A1  0~500 Pa", 1);
    ui.cmbPressureControlChannel->addItem("A2  0~300 Pa", 2);
    ui.cmbPressureControlChannel->setCurrentIndex(1);
    ui.sbPressureTarget = new QDoubleSpinBox();
    ui.sbPressureTarget->setRange(0.0, 500.0);
    ui.sbPressureTarget->setDecimals(1);
    ui.sbPressureTarget->setSingleStep(1.0);
    ui.sbPressureTarget->setSuffix(" Pa");
    ui.cmbPressureControlDirection = new QComboBox();
    ui.cmbPressureControlDirection->addItem("开度↑ 压差↑", true);
    ui.cmbPressureControlDirection->addItem("开度↑ 压差↓", false);
    ui.sbPressureKp = new QDoubleSpinBox();
    ui.sbPressureKp->setRange(0.0, 10.0);
    ui.sbPressureKp->setDecimals(3);
    ui.sbPressureKp->setSingleStep(0.05);
    ui.sbPressureKp->setValue(0.40);
    ui.sbPressureKi = new QDoubleSpinBox();
    ui.sbPressureKi->setRange(0.0, 10.0);
    ui.sbPressureKi->setDecimals(3);
    ui.sbPressureKi->setSingleStep(0.01);
    ui.sbPressureKi->setValue(0.08);
    ui.btnPressureControlStart = new QPushButton("启动闭环");
    ui.btnPressureControlStop = new QPushButton("停止并关闭");
    ui.btnPressureControlStart->setEnabled(false);
    ui.btnPressureControlStop->setEnabled(false);
    ui.btnPressureControlStart->setStyleSheet(solidButtonStyle("#117A65", "#0E6251"));
    ui.btnPressureControlStop->setStyleSheet(solidButtonStyle("#C0392B", "#922B21"));
    ui.lblPressureControlStatus = new QLabel(
        "未启动。请先启动气泵并确认所选压差通道正常。");
    ui.lblPressureControlStatus->setWordWrap(true);
    ui.lblPressureControlStatus->setStyleSheet(
        "font-size: 12px; color: #526471; background: #F4F7F9; border: 1px solid #DCE4EA; "
        "border-radius: 6px; padding: 6px 9px;");

    auto createPressureControlLabel = [](const QString& text) {
        QLabel *label = new QLabel(text);
        label->setStyleSheet("font-size: 12px; color: #607481; font-weight: bold;");
        return label;
    };
    pressureControlLayout->addWidget(createPressureControlLabel("反馈通道"), 0, 0, 1, 2);
    pressureControlLayout->addWidget(createPressureControlLabel("目标压差"), 0, 2, 1, 2);
    pressureControlLayout->addWidget(createPressureControlLabel("控制方向"), 0, 4, 1, 2);
    pressureControlLayout->addWidget(createPressureControlLabel("比例系数 Kp"), 0, 6);
    pressureControlLayout->addWidget(createPressureControlLabel("积分系数 Ki"), 0, 7);
    pressureControlLayout->addWidget(ui.cmbPressureControlChannel, 1, 0, 1, 2);
    pressureControlLayout->addWidget(ui.sbPressureTarget, 1, 2, 1, 2);
    pressureControlLayout->addWidget(ui.cmbPressureControlDirection, 1, 4, 1, 2);
    pressureControlLayout->addWidget(ui.sbPressureKp, 1, 6);
    pressureControlLayout->addWidget(ui.sbPressureKi, 1, 7);
    pressureControlLayout->addWidget(ui.btnPressureControlStart, 2, 0, 1, 4);
    pressureControlLayout->addWidget(ui.btnPressureControlStop, 2, 4, 1, 4);
    pressureControlLayout->addWidget(ui.lblPressureControlStatus, 3, 0, 1, 8);
    for (int column = 0; column < 8; ++column) pressureControlLayout->setColumnStretch(column, 1);

    QGroupBox *pressureGroup = new QGroupBox("压差监测");
    pressureGroup->setStyleSheet(cardStyle("#8E44AD"));
    QVBoxLayout *pressureLayout = new QVBoxLayout(pressureGroup);
    pressureLayout->setContentsMargins(12, 12, 12, 12);
    pressureLayout->setSpacing(8);
    QHBoxLayout *pressureToolbar = new QHBoxLayout();
    QLabel *pressureHardware = new QLabel("ADS1115 · I²C 0x48 · 三通道轮询");
    pressureHardware->setStyleSheet("font-size: 12px; color: #71828E;");
    ui.btnPressureZero = new QPushButton("三路重新校零");
    ui.btnPressureZero->setFixedSize(132, 34);
    ui.btnPressureZero->setStyleSheet(solidButtonStyle("#8E44AD", "#6C3483"));
    pressureToolbar->addWidget(pressureHardware);
    pressureToolbar->addStretch();
    pressureToolbar->addWidget(ui.btnPressureZero);
    pressureLayout->addLayout(pressureToolbar);

    QHBoxLayout *pressureCards = new QHBoxLayout();
    pressureCards->setSpacing(8);
    auto addPressureCard = [&](const QString& name, int channel, const QString& initialValue) {
        QFrame *card = new QFrame();
        card->setObjectName("pressureChannelCard");
        card->setStyleSheet(
            "QFrame#pressureChannelCard { background: #F8FAFB; border: 1px solid #DCE4EA; border-radius: 8px; }");
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(11, 9, 11, 9);
        cardLayout->setSpacing(4);
        QHBoxLayout *cardHeader = new QHBoxLayout();
        QLabel *nameLabel = new QLabel(name);
        nameLabel->setStyleSheet("font-size: 14px; color: #5B2C6F; font-weight: bold;");
        ui.lblPressureStatus[channel] = new QLabel("初始化中");
        ui.lblPressureStatus[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureStatus[channel]->setMinimumWidth(62);
        ui.lblPressureStatus[channel]->setStyleSheet(
            "font-size: 11px; color: #607481; font-weight: bold; background: #EDF1F3; "
            "border: 1px solid #D5DDE2; border-radius: 10px; padding: 3px 8px;");
        cardHeader->addWidget(nameLabel);
        cardHeader->addStretch();
        cardHeader->addWidget(ui.lblPressureStatus[channel]);
        ui.lblPressureValue[channel] = new QLabel(initialValue);
        ui.lblPressureValue[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureValue[channel]->setStyleSheet(
            "font-size: 25px; color: #8E44AD; font-weight: bold; font-family: 'WenQuanYi Micro Hei';");
        ui.lblPressureDetails[channel] = new QLabel("-- V · -- %FS");
        ui.lblPressureDetails[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureDetails[channel]->setStyleSheet("font-size: 11px; color: #607481;");
        cardLayout->addLayout(cardHeader);
        cardLayout->addWidget(ui.lblPressureValue[channel], 1);
        cardLayout->addWidget(ui.lblPressureDetails[channel]);
        pressureCards->addWidget(card, 1);
    };

    addPressureCard("A0 · 40 kPa", 0, "--.--- kPa");
    addPressureCard("A1 · 500 Pa", 1, "---.- Pa");
    addPressureCard("A2 · 300 Pa", 2, "---.- Pa");
    pressureLayout->addLayout(pressureCards);

    QHBoxLayout *actuatorLayout = new QHBoxLayout();
    actuatorLayout->setSpacing(6);
    actuatorLayout->addWidget(pumpGroup, 1);
    actuatorLayout->addWidget(valveGroup, 1);
    gasMainLayout->addLayout(actuatorLayout);
    gasMainLayout->addWidget(pressureControlGroup);
    gasMainLayout->addWidget(pressureGroup);
    gasMainLayout->addStretch();
    QScrollArea *gasScrollArea = new QScrollArea();
    gasScrollArea->setWidgetResizable(true);
    gasScrollArea->setFrameShape(QFrame::NoFrame);
    gasScrollArea->setWidget(gasContent);
    gasTabLayout->addWidget(gasScrollArea);
    ui.tabs->addTab(gasTab, "气路");

    QWidget *liquidTab = new QWidget();
    QVBoxLayout *liquidTabLayout = new QVBoxLayout(liquidTab);
    liquidTabLayout->setContentsMargins(4, 8, 4, 4);
    liquidTabLayout->setSpacing(8);
    QGroupBox *liquidGroup = new QGroupBox("液位与补液监控");
    liquidGroup->setStyleSheet(cardStyle("#F39C12"));
    QVBoxLayout *liquidLayout = new QVBoxLayout(liquidGroup);
    liquidLayout->setContentsMargins(12, 28, 12, 12);
    liquidLayout->setSpacing(8);
    QWidget *liquidStatusBar = new QWidget();
    liquidStatusBar->setObjectName("liquidStatusBar");
    liquidStatusBar->setStyleSheet(
        "QWidget#liquidStatusBar { background: #FFF9EC; border: 1px solid #F6DDA5; border-radius: 8px; }");
    QHBoxLayout *liquidButtonLayout = new QHBoxLayout(liquidStatusBar);
    liquidButtonLayout->setContentsMargins(10, 7, 8, 7);
    liquidButtonLayout->setSpacing(8);
    ui.btnLiquidStart = new QPushButton("开始监控液位");
    ui.btnLiquidStop = new QPushButton("停止液位监控");
    ui.btnDrain = new QPushButton("按住排液");
    ui.btnLiquidStart->setVisible(false);
    ui.btnLiquidStop->setVisible(false);
    ui.btnDrain->setFixedWidth(150);
    ui.btnDrain->setStyleSheet(solidButtonStyle("#C0392B", "#922B21"));
    QLabel *liquidStateTitle = new QLabel("当前液位状态");
    liquidStateTitle->setStyleSheet("font-size: 14px; color: #6E5A2F; font-weight: bold;");
    ui.lblLiquidState = new QLabel("异常");
    ui.lblLiquidState->setAlignment(Qt::AlignCenter);
    ui.lblLiquidState->setMinimumWidth(82);
    ui.lblLiquidState->setStyleSheet("font-size: 14px; color: #A93226; font-weight: bold; background: #FDEDEC; border: 1px solid #F5B7B1; border-radius: 12px; padding: 4px 12px;");
    ui.liquidLog = new QTextBrowser();
    ui.liquidLog->setMinimumHeight(330);
    ui.liquidLog->setStyleSheet("font-size: 12px; color: #405462; background-color: #F8FAFB; border: 1px solid #D7E0E6; border-radius: 8px; padding: 8px;");
    liquidButtonLayout->addWidget(ui.btnLiquidStart);
    liquidButtonLayout->addWidget(ui.btnLiquidStop);
    liquidButtonLayout->addWidget(liquidStateTitle);
    liquidButtonLayout->addWidget(ui.lblLiquidState);
    liquidButtonLayout->addStretch();
    liquidButtonLayout->addWidget(ui.btnDrain);
    QLabel *liquidLogTitle = new QLabel("运行记录");
    liquidLogTitle->setStyleSheet("font-size: 13px; color: #526471; font-weight: bold; padding: 2px 2px 0 2px;");
    liquidLayout->addWidget(liquidStatusBar);
    liquidLayout->addWidget(liquidLogTitle);
    liquidLayout->addWidget(ui.liquidLog);
    liquidTabLayout->addWidget(liquidGroup);
    ui.tabs->addTab(liquidTab, "液位");

    QWidget *algorithmTab = new QWidget();
    QVBoxLayout *algorithmLayout = new QVBoxLayout(algorithmTab);
    algorithmLayout->setContentsMargins(4, 8, 4, 4);
    algorithmLayout->setSpacing(8);
    QLabel *algorithmIntro = new QLabel("调整 OPC 脉冲识别阈值。修改后立即生效，请结合原始信号页面观察效果。");
    algorithmIntro->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    algorithmIntro->setWordWrap(true);
    algorithmIntro->setStyleSheet("font-size: 13px; color: #526471; padding: 2px 0;");
    QGroupBox *algorithmGroup = new QGroupBox("OPC 算法设置");
    algorithmGroup->setStyleSheet(cardStyle("#2E86C1"));
    QGridLayout *algorithmGrid = new QGridLayout(algorithmGroup);
    algorithmGrid->setContentsMargins(14, 16, 14, 14);
    algorithmGrid->setHorizontalSpacing(10);
    algorithmGrid->setVerticalSpacing(12);

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
    lblAlgorithmSummary->setStyleSheet("font-size: 14px; color: #2C5D7C; font-weight: bold; padding: 10px 12px; background: #F0F7FB; border: 1px solid #D4E6F1; border-radius: 7px;");
    lblAlgorithmSummary->setWordWrap(true);

    auto createAlgorithmControl = [&](const QString& title,
                                      const QString& description,
                                      QDoubleSpinBox *spinBox) {
        QFrame *card = new QFrame();
        card->setObjectName("algorithmParameterCard");
        card->setStyleSheet(
            "QFrame#algorithmParameterCard { background: #F8FAFB; border: 1px solid #DCE4EA; border-radius: 8px; }");
        QVBoxLayout *layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 10, 12, 12);
        layout->setSpacing(5);
        QLabel *label = new QLabel(title);
        label->setStyleSheet("font-size: 15px; color: #2C5D7C; font-weight: bold;");
        QLabel *hint = new QLabel(description);
        hint->setWordWrap(true);
        hint->setStyleSheet("font-size: 12px; color: #71828E;");
        spinBox->setMinimumHeight(40);
        spinBox->setStyleSheet("QDoubleSpinBox { padding: 4px 8px; background: #F9FBFC; border: 1px solid #C7D1D9; border-radius: 6px; font-size: 15px; } QDoubleSpinBox:focus { border: 2px solid #2E86C1; background: #FFFFFF; }");
        layout->addWidget(label);
        layout->addWidget(hint);
        layout->addSpacing(3);
        layout->addWidget(spinBox);
        return card;
    };

    algorithmGrid->addWidget(algorithmIntro, 0, 0, 1, 3);
    algorithmGrid->addWidget(
        createAlgorithmControl("阈值下限 · cutoff", "限制噪声宽度采用的最小值", sbCutoff), 1, 0);
    algorithmGrid->addWidget(
        createAlgorithmControl("阈值偏置 · offset", "在动态阈值基础上叠加修正", sbCutoffOffset), 1, 1);
    algorithmGrid->addWidget(
        createAlgorithmControl("计算间隔", "重新估计本底与噪声的周期", sbCutoffInterval), 1, 2);
    algorithmGrid->addWidget(lblAlgorithmSummary, 2, 0, 1, 3);
    for (int column = 0; column < 3; ++column) algorithmGrid->setColumnStretch(column, 1);

    auto refreshAlgorithmSummary = [lblAlgorithmSummary, &opcParams]() {
        lblAlgorithmSummary->setText(QString("当前规则：阈值 = 本底 + 限幅后的噪声宽度 + 偏置    |    噪声范围 %1 ~ %2 V    |    偏置 %3 V    |    每 %4 ms 更新")
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
    opcLayout->setContentsMargins(4, 8, 4, 4);
    opcLayout->setSpacing(8);
    QGroupBox *opcControlGroup = new QGroupBox("OPC 原始信号");
    opcControlGroup->setStyleSheet(cardStyle("#8E44AD"));
    QVBoxLayout *opcControlLayout = new QVBoxLayout(opcControlGroup);
    opcControlLayout->setContentsMargins(12, 30, 12, 12);
    opcControlLayout->setSpacing(8);
    QLabel *lblProcess = new QLabel("空气入口  →  饱和段  →  冷凝段  →  OPC 光腔");
    lblProcess->setAlignment(Qt::AlignCenter);
    lblProcess->setStyleSheet("color: #68457A; font-size: 14px; font-weight: bold; background: #F7F0FA; border: 1px solid #E4D3EB; border-radius: 7px; padding: 8px;");
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
