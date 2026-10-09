#include "MainWindowUi.h"

#include "ControlWidgets.h"
#include "PlotSetup.h"
#include "TouchDoubleSpinBox.h"
#include "../algorithms/OpcCounter.h"
#include "../hardware/PinMap.h"
#include "../qcustomplot.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
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
#include <QSizePolicy>
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
    if (pixmap.isNull()) pixmap.load(":/ui/research_group_header.png");
    if (pixmap.isNull()) pixmap.load(appDir + "/research_group_header.png");
    if (pixmap.isNull()) pixmap.load(appDir + "/assets/research_group_header.png");
    if (pixmap.isNull()) pixmap.load("/home/pi/Desktop/HTCPC/research_group_header.png");
    return pixmap;
}

QPixmap cropWhiteLogoMargins(const QPixmap& source) {
    if (source.isNull()) return source;

    const QImage image = source.toImage();
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const bool visible = color.alpha() >= 16 &&
                                 (color.red() < 245 || color.green() < 245 || color.blue() < 245);
            if (!visible) continue;
            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
        }
    }

    if (right < left || bottom < top) return source;
    const QRect contentBounds(left, top, right - left + 1, bottom - top + 1);
    const QRect paddedBounds = contentBounds.adjusted(-12, -12, 12, 12).intersected(image.rect());
    return source.copy(paddedBounds);
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

MainWindowUi buildMainWindow(QApplication& app,
                             QMainWindow& window,
                             OpcParams& opcParams,
                             const ParticleCalibrationParams& particleCalibration) {
    MainWindowUi ui;

    app.installEventFilter(new DialogUiFilter(&app));

    window.setWindowTitle("高温凝结核粒子计数器总控面板");
    window.resize(1280, 720);
    app.setFont(QFont("WenQuanYi Micro Hei", 14));
    app.setStyleSheet(
        "QMainWindow, QWidget { background-color: #EEF2F6; color: #263746; font-family: 'WenQuanYi Micro Hei'; }"
        "QFocusFrame { background: transparent; border: none; }"
        "QLabel { background: transparent; }"
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab { background: #E5E8EC; color: #2c3e50; min-width: 108px; min-height: 42px; padding: 6px 16px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; font-size: 20px; font-weight: bold; }"
        "QTabBar::tab:selected { background: #005bac; color: white; }"
        "QPushButton { min-height: 42px; border: 1px solid #CBD5DC; border-radius: 7px; "
        "background-color: #FFFFFF; color: #34495E; font-size: 18px; font-weight: bold; "
        "padding: 0 12px; outline: none; }"
        "QPushButton:focus, QToolButton:focus { outline: none; }"
        "QPushButton:hover { background-color: #F4F8FB; border-color: #91A5B4; }"
        "QPushButton:pressed { background-color: #E6EDF2; }"
        "QPushButton:disabled { background-color: #E8ECEF; color: #98A3AB; border-color: #D8DEE3; }"
        "QDoubleSpinBox, QComboBox { min-height: 36px; background-color: #FFFFFF; color: #2C3E50; "
        "border: 1px solid #C7D1D9; border-radius: 6px; padding: 2px 8px; font-size: 18px; }"
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
        "QMessageBox QLabel#qt_msgbox_label { font-size: 21px; min-width: 440px; max-width: 620px; padding: 4px; }"
        "QDialogButtonBox { background: transparent; }"
        "QDialogButtonBox QPushButton { min-width: 96px; min-height: 38px; padding: 0 18px; background-color: #F2F5F7; color: #34495E; border: 1px solid #B8C4CE; border-radius: 6px; font-size: 19px; font-weight: bold; outline: none; }"
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
    QPixmap logoPixmap = cropWhiteLogoMargins(loadHeaderLogo());
    if (!logoPixmap.isNull()) {
        lblLogo->setPixmap(logoPixmap.scaled(QSize(260, 46), Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
        lblLogo->setStyleSheet("background: transparent; border: none;");
    } else {
        lblLogo->setText("北京航空航天大学");
        lblLogo->setStyleSheet("color: #005bac; font-size: 24px; font-weight: bold;");
    }
    lblLogo->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    headerLayout->addWidget(lblLogo);

    QLabel *lblPlatformTitle = new QLabel(" | 高温凝结核粒子计数器");
    lblPlatformTitle->setStyleSheet("color: #005bac; font-size: 24px; font-weight: bold; background: transparent; border: none;");
    headerLayout->addWidget(lblPlatformTitle);
    headerLayout->addStretch();

    QWidget *headerStatusBar = new QWidget();
    headerStatusBar->setStyleSheet("background: transparent;");
    QHBoxLayout *headerStatusLayout = new QHBoxLayout(headerStatusBar);
    headerStatusLayout->setContentsMargins(0, 0, 8, 0);
    headerStatusLayout->setSpacing(8);
    QWidget *warmupIndicator = new QWidget();
    warmupIndicator->setStyleSheet("background: transparent;");
    QHBoxLayout *warmupIndicatorLayout = new QHBoxLayout(warmupIndicator);
    warmupIndicatorLayout->setContentsMargins(0, 0, 0, 0);
    warmupIndicatorLayout->setSpacing(4);
    QLabel *warmupTitle = new QLabel("热机");
    warmupTitle->setStyleSheet(
        "font-size: 20px; color: #2C3E50; font-weight: 800; background: transparent;");
    ui.lblStartupState = new QLabel("自检");
    ui.lblStartupState->setAlignment(Qt::AlignCenter);
    ui.lblStartupState->setMinimumWidth(66);
    ui.lblStartupState->setStyleSheet(
        "font-size: 22px; font-weight: 800; color: #C45F00; background: transparent;");
    warmupIndicatorLayout->addWidget(warmupTitle);
    warmupIndicatorLayout->addWidget(ui.lblStartupState);
    headerStatusLayout->addWidget(warmupIndicator);
    auto addHeaderStatusIndicator = [&](const QString& title, QLabel *&lamp) {
        QWidget *indicator = new QWidget();
        indicator->setStyleSheet("background: transparent;");
        QHBoxLayout *indicatorLayout = new QHBoxLayout(indicator);
        indicatorLayout->setContentsMargins(0, 0, 0, 0);
        indicatorLayout->setSpacing(4);
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setStyleSheet("font-size: 18px; color: #34495E; font-weight: bold; background: transparent;");
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
    pageSelector->setFixedSize(140, 42);
    pageSelector->setFocusPolicy(Qt::NoFocus);
    pageSelector->setMaxVisibleItems(7);
    pageSelector->setToolTip("切换功能页面");
    pageSelector->setStyleSheet(
        "QComboBox { background-color: #FFFFFF; color: #2C3E50; border: 1px solid #AEBBC6; border-radius: 7px; padding: 5px 38px 5px 13px; font-size: 19px; font-weight: bold; }"
        "QComboBox:hover { border-color: #7F96A8; }"
        "QComboBox:on { border-color: #005BAC; }"
        "QComboBox:disabled { background-color: #EFF1F3; color: #9AA6AF; border-color: #CDD4DA; }"
        "QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: 34px; background-color: #F5F7F9; border-left: 1px solid #D5DDE4; border-top-right-radius: 7px; border-bottom-right-radius: 7px; }"
        "QComboBox::drop-down:hover { background-color: #E9EEF2; }"
        "QComboBox::down-arrow { image: url(:/ui/ui/dropdown_arrow.svg); width: 14px; height: 9px; }"
        "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #2C3E50; border: 1px solid #AEBBC6; border-radius: 7px; padding: 4px; outline: none; selection-background-color: #005BAC; selection-color: #FFFFFF; font-size: 19px; font-weight: bold; }"
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

    QWidget *overviewTab = new QWidget();
    QGridLayout *overviewLayout = new QGridLayout(overviewTab);
    overviewLayout->setContentsMargins(4, 8, 4, 4);
    overviewLayout->setSpacing(8);
    ui.lblParticleConcentration = new QLabel("-- 个/ml");
    ui.lblParticleConcentration->setStyleSheet("font-size: 46px; color: #0E8F78; font-weight: bold; font-family: 'WenQuanYi Micro Hei';");
    ui.lblParticleConcentration->setAlignment(Qt::AlignCenter);
    ui.lblStatus = new QLabel("状态: 待机（执行器关闭）");
    ui.lblStatus->setAlignment(Qt::AlignCenter);
    ui.lblStatus->setWordWrap(true);
    ui.lblStatus->setStyleSheet("color: #E67E22; font-weight:bold; font-size: 18px;");
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
    ui.lblCaptureState->setStyleSheet("font-size: 17px; font-weight: bold; color: #566573;");
    systemLayout->addWidget(ui.lblStatus, 0, 0, 1, 2);
    systemLayout->addWidget(ui.lblCaptureState, 0, 2);
    systemLayout->addWidget(ui.btnAcqStart, 1, 0);
    systemLayout->addWidget(ui.btnAcqStop, 1, 1);
    systemLayout->addWidget(ui.btnSaveRaw, 1, 2);

    ui.lblOverviewPump = createOverviewValue("关", "#27AE60");

    ui.lblCompactDeviceState = new QLabel("气泵: 关    流量: 0.3 L/min    压差 A0:初始化  A1:初始化  A2:初始化");
    ui.lblCompactDeviceState->setAlignment(Qt::AlignCenter);
    ui.lblCompactDeviceState->setStyleSheet("font-size: 16px; color: #526471; background: #FFFFFF; border: 1px solid #D8E1E7; border-radius: 7px; padding: 5px 10px;");
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
    particlePlotTitle->setStyleSheet("font-size: 18px; color: #3C596B; font-weight: bold;");
    ui.btnResetParticlePlot = new QPushButton("还原视图");
    ui.btnResetParticlePlot->setFixedSize(88, 28);
    ui.btnResetParticlePlot->setStyleSheet("QPushButton { background-color: #F7FAFC; color: #3C596B; border: 1px solid #C8D4DC; border-radius: 5px; min-height: 24px; font-size: 16px; font-weight: bold; } QPushButton:hover { background-color: #EAF2F7; border-color: #8FA7B7; } QPushButton:pressed { background-color: #DDE8EF; }");
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
    ui.tabs->addTab(overviewTab, "总览");

    QWidget *tempTab = new QWidget();
    QGridLayout *tempLayout = new QGridLayout(tempTab);
    tempLayout->setContentsMargins(4, 8, 4, 4);
    tempLayout->setSpacing(8);

    QGroupBox *condGroup = createTempGroup("冷凝段", "#3498DB", ui.sbCond, ui.btnCondStart, ui.btnCondStop, ui.lblCondTemp, ui.lblCondPwm);
    QGroupBox *satGroup = createTempGroup("饱和段", "#E74C3C", ui.sbSat, ui.btnSatStart, ui.btnSatStop, ui.lblSatTemp, ui.lblSatPwm);
    QGroupBox *opcGroup = createTempGroup("OPC段", "#F39C12", ui.sbOpc, ui.btnOpcStart, ui.btnOpcStop, ui.lblOpcTemp, ui.lblOpcPwm);

    ui.sbCond->setValue(200.0);
    ui.sbSat->setValue(250.0);
    ui.sbOpc->setValue(250.0);
    ui.btnOpcStart->setEnabled(false);
    ui.btnOpcStop->setEnabled(false);
    ui.lblOpcPwm->setText("功率: 0.0 %");

    QGroupBox *pidGroup = new QGroupBox("PID 参数调节");
    pidGroup->setStyleSheet(
        cardStyle("#8E44AD") +
        "QGroupBox::title { font-size: 19px; font-weight: 700; letter-spacing: 1px; }");
    QGridLayout *pidLayout = new QGridLayout(pidGroup);
    pidLayout->setContentsMargins(14, 28, 14, 11);
    pidLayout->setHorizontalSpacing(8);
    pidLayout->setVerticalSpacing(7);
    ui.cmbTempPidSegment = new QComboBox();
    ui.cmbTempPidSegment->addItem("冷凝段", 0);
    ui.cmbTempPidSegment->addItem("饱和段", 1);
    ui.cmbTempPidSegment->addItem("OPC段", 2);
    ui.cmbTempPidSegment->setCurrentIndex(1);
    ui.cmbTempPidSegment->setMinimumHeight(38);
    ui.cmbTempPidSegment->setStyleSheet(
        "QComboBox { padding: 5px 9px; background: #FFFFFF; border: 1px solid #C7D1D9; "
        "border-radius: 7px; color: #243B53; font-size: 18px; font-weight: 600; }"
        "QComboBox:hover { border-color: #9B59B6; background: #FCFAFD; }"
        "QComboBox:focus { border: 2px solid #8E44AD; }");
    ui.sbTempKp = new TouchDoubleSpinBox("设置比例系数 Kp");
    ui.sbTempKi = new TouchDoubleSpinBox("设置积分系数 Ki");
    ui.sbTempKd = new TouchDoubleSpinBox("设置微分系数 Kd");
    const QString pidSpinStyle =
        "QDoubleSpinBox { min-height: 36px; padding: 3px 7px; background: #F9FBFC; "
        "border: 1px solid #C7D1D9; border-radius: 7px; color: #243B53; "
        "font-size: 19px; font-weight: 600; } "
        "QDoubleSpinBox:hover { border-color: #9B59B6; background: #FCFAFD; } "
        "QDoubleSpinBox:focus { border: 2px solid #8E44AD; background: #FFFFFF; }";
    ui.sbTempKp->setRange(0.0, 100.0);
    ui.sbTempKp->setDecimals(2);
    ui.sbTempKp->setSingleStep(0.5);
    ui.sbTempKi->setRange(0.0, 20.0);
    ui.sbTempKi->setDecimals(3);
    ui.sbTempKi->setSingleStep(0.05);
    ui.sbTempKd->setRange(0.0, 300.0);
    ui.sbTempKd->setDecimals(1);
    ui.sbTempKd->setSingleStep(5.0);
    ui.sbTempKp->setStyleSheet(pidSpinStyle);
    ui.sbTempKi->setStyleSheet(pidSpinStyle);
    ui.sbTempKd->setStyleSheet(pidSpinStyle);
    ui.btnTempPidApply = new QPushButton("应用参数");
    ui.btnTempPidReset = new QPushButton("恢复推荐值");
    ui.btnTempPidApply->setStyleSheet(
        solidButtonStyle("#8E44AD", "#6C3483") +
        "QPushButton { font-size: 18px; font-weight: 700; }");
    ui.btnTempPidReset->setStyleSheet(
        solidButtonStyle("#71858A", "#56696E") +
        "QPushButton { font-size: 17px; font-weight: 600; }");
    ui.lblTempPidStatus = new QLabel("选择控制段后可查看和修改参数");
    ui.lblTempPidStatus->setStyleSheet(
        "font-size: 17px; font-weight: 500; color: #634B6B; background: #F8F3FA; "
        "border: 1px solid #E4D3EA; border-radius: 6px; padding: 5px 9px;");
    auto createPidCaption = [](const QString& symbol, const QString& description) {
        QLabel *label = new QLabel(
            QString("<span style='font-size: 20px; font-weight:700; color:#7D3C98;'>%1</span>"
                    "&nbsp;&nbsp;<span style='font-size: 16px; font-weight:500; "
                    "color:#607481;'>%2</span>")
                .arg(symbol, description));
        label->setTextFormat(Qt::RichText);
        label->setStyleSheet("padding: 0 1px 1px 1px;");
        return label;
    };
    QLabel *pidSegmentCaption = new QLabel("控制回路");
    pidSegmentCaption->setStyleSheet(
        "font-size: 18px; font-weight: 700; color: #34495E; padding: 0 1px 1px 1px;");
    pidLayout->addWidget(pidSegmentCaption, 0, 0);
    pidLayout->addWidget(ui.cmbTempPidSegment, 1, 0);
    pidLayout->addWidget(createPidCaption("Kp", "比例系数"), 0, 1);
    pidLayout->addWidget(ui.sbTempKp, 1, 1);
    pidLayout->addWidget(createPidCaption("Ki", "积分系数"), 0, 2);
    pidLayout->addWidget(ui.sbTempKi, 1, 2);
    pidLayout->addWidget(createPidCaption("Kd", "微分系数"), 0, 3);
    pidLayout->addWidget(ui.sbTempKd, 1, 3);
    pidLayout->addWidget(ui.btnTempPidApply, 1, 4);
    pidLayout->addWidget(ui.btnTempPidReset, 1, 5);
    pidLayout->addWidget(ui.lblTempPidStatus, 2, 0, 1, 6);
    pidLayout->setColumnStretch(0, 2);
    pidLayout->setColumnStretch(1, 1);
    pidLayout->setColumnStretch(2, 1);
    pidLayout->setColumnStretch(3, 1);

    QLabel *tempHint = new QLabel(
        "<span style='font-weight:700; color:#315D76;'>调参提示</span>"
        "&nbsp;&nbsp;<span style='color:#526D7C;'>点击“应用参数”后立即生效并保存，同时清空积分状态。</span>");
    tempHint->setTextFormat(Qt::RichText);
    tempHint->setWordWrap(true);
    tempHint->setStyleSheet("font-size: 17px; padding: 8px 12px; background: #EAF2F8; border: 1px solid #D4E6F1; border-radius: 7px;");
    tempLayout->addWidget(condGroup, 0, 0);
    tempLayout->addWidget(satGroup, 0, 1);
    tempLayout->addWidget(opcGroup, 0, 2);
    tempLayout->addWidget(pidGroup, 1, 0, 1, 3);
    tempLayout->addWidget(tempHint, 2, 0, 1, 3);
    tempLayout->setRowStretch(3, 1);
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
    ui.sliderPump->setValue(100);
    ui.lblPumpValue = new QLabel("100 %");
    ui.lblPumpValue->setStyleSheet("font-size: 22px; color: #27AE60; font-weight: bold;");
    QLabel *pumpPowerLabel = new QLabel("抽气功率");
    pumpPowerLabel->setStyleSheet("font-size: 19px; color: #566573;");
    pumpLayout->addWidget(pumpPowerLabel, 0, 0);
    pumpLayout->addWidget(ui.sliderPump, 0, 1);
    pumpLayout->addWidget(ui.lblPumpValue, 0, 2);
    pumpLayout->addWidget(ui.btnPumpStart, 1, 0, 1, 2);
    pumpLayout->addWidget(ui.btnPumpStop, 1, 2);
    pumpLayout->setColumnStretch(1, 1);

    QGroupBox *flowModeGroup = new QGroupBox("流量模式");
    flowModeGroup->setStyleSheet(cardStyle("#16A085"));
    QVBoxLayout *flowModeLayout = new QVBoxLayout(flowModeGroup);
    flowModeLayout->setContentsMargins(10, 24, 10, 8);
    flowModeLayout->setSpacing(7);
    QHBoxLayout *flowModeButtons = new QHBoxLayout();
    flowModeButtons->setSpacing(7);
    ui.btnBypassLowFlow = new QPushButton("小流量\n0.3 L/min");
    ui.btnBypassHighFlow = new QPushButton("大流量\n1.5 L/min");
    ui.btnBypassLowFlow->setCheckable(true);
    ui.btnBypassHighFlow->setCheckable(true);
    ui.btnBypassLowFlow->setChecked(true);
    const QString flowModeButtonStyle =
        "QPushButton { min-height: 46px; padding: 5px 10px; font-size: 18px; "
        "font-weight: bold; color: #3C596B; background: #F4F7F9; "
        "border: 1px solid #C8D5DC; border-radius: 8px; }"
        "QPushButton:hover { background: #E8F4F1; border-color: #6BB8A8; }"
        "QPushButton:checked { color: #FFFFFF; background: #16A085; "
        "border: 2px solid #117864; }"
        "QPushButton:checked:disabled { color: #FFFFFF; background: #16A085; "
        "border: 2px solid #117864; }"
        "QPushButton:disabled:!checked { color: #A7B2B9; background: #EEF1F3; "
        "border-color: #D8DEE2; }";
    ui.btnBypassLowFlow->setStyleSheet(flowModeButtonStyle);
    ui.btnBypassHighFlow->setStyleSheet(flowModeButtonStyle);
    ui.lblFlowModeState = new QLabel("当前：小流量 0.3 L/min");
    ui.lblFlowModeState->setAlignment(Qt::AlignCenter);
    ui.lblFlowModeState->setStyleSheet(
        "font-size: 16px; color: #187A5A; font-weight: bold; background: #E8F8F5; "
        "border: 1px solid #A3E4D7; border-radius: 6px; padding: 5px;");
    flowModeButtons->addWidget(ui.btnBypassLowFlow);
    flowModeButtons->addWidget(ui.btnBypassHighFlow);
    flowModeLayout->addLayout(flowModeButtons);
    flowModeLayout->addWidget(ui.lblFlowModeState);

    QGroupBox *valveGroup = new QGroupBox("比例阀");
    valveGroup->setStyleSheet(cardStyle("#2980B9"));
    QGridLayout *valveLayout = new QGridLayout(valveGroup);
    valveLayout->setContentsMargins(10, 24, 10, 8);
    valveLayout->setSpacing(8);
    QLabel *valveOpeningLabel = new QLabel("开度");
    valveOpeningLabel->setStyleSheet("font-size: 19px; color: #566573;");
    ui.sbValveOpening = new TouchDoubleSpinBox("设置比例阀开度");
    ui.sbValveOpening->setRange(0.0, 100.0);
    ui.sbValveOpening->setDecimals(1);
    ui.sbValveOpening->setSingleStep(1.0);
    ui.sbValveOpening->setSuffix(" %");
    ui.sbValveOpening->setValue(80.0);
    ui.sbValveOpening->setMinimumHeight(36);
    ui.sbValveOpening->setStyleSheet("QDoubleSpinBox { padding: 5px; border: 1px solid #bdc3c7; border-radius: 5px; font-size: 20px; }");
    ui.lblValveCurrent = new QLabel("16.80 mA");
    ui.lblValveCurrent->setStyleSheet("font-size: 21px; color: #2980B9; font-weight: bold;");
    ui.btnValveApply = new QPushButton("设置");
    ui.btnValveRead = new QPushButton("读取");
    ui.btnValveClose = new QPushButton("安全关闭");
    ui.btnValveApply->setStyleSheet(solidButtonStyle("#2980B9", "#21618C"));
    ui.btnValveRead->setStyleSheet(solidButtonStyle("#566573", "#3F4D55"));
    ui.btnValveClose->setStyleSheet(solidButtonStyle("#C0392B", "#922B21"));
    ui.lblValveStatus = new QLabel("N4IOA01 · /dev/ttyAMA0 · 尚未通信");
    ui.lblValveStatus->setWordWrap(true);
    ui.lblValveStatus->setStyleSheet("font-size: 17px; color: #566573;");
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
    ui.sbPressureTarget = new TouchDoubleSpinBox("设置目标压差");
    ui.sbPressureTarget->setRange(0.0, 500.0);
    ui.sbPressureTarget->setDecimals(1);
    ui.sbPressureTarget->setSingleStep(1.0);
    ui.sbPressureTarget->setSuffix(" Pa");
    ui.cmbPressureControlDirection = new QComboBox();
    ui.cmbPressureControlDirection->addItem("开度↑ 压差↑", true);
    ui.cmbPressureControlDirection->addItem("开度↑ 压差↓", false);
    ui.sbPressureKp = new TouchDoubleSpinBox("设置比例系数 Kp");
    ui.sbPressureKp->setRange(0.0, 10.0);
    ui.sbPressureKp->setDecimals(3);
    ui.sbPressureKp->setSingleStep(0.05);
    ui.sbPressureKp->setValue(0.40);
    ui.sbPressureKi = new TouchDoubleSpinBox("设置积分系数 Ki");
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
        "font-size: 16px; color: #526471; background: #F4F7F9; border: 1px solid #DCE4EA; "
        "border-radius: 6px; padding: 6px 9px;");

    auto createPressureControlLabel = [](const QString& text) {
        QLabel *label = new QLabel(text);
        label->setStyleSheet("font-size: 16px; color: #607481; font-weight: bold;");
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
    pressureHardware->setStyleSheet("font-size: 16px; color: #71828E;");
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
        nameLabel->setStyleSheet("font-size: 18px; color: #5B2C6F; font-weight: bold;");
        ui.lblPressureStatus[channel] = new QLabel("初始化中");
        ui.lblPressureStatus[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureStatus[channel]->setMinimumWidth(62);
        ui.lblPressureStatus[channel]->setStyleSheet(
            "font-size: 15px; color: #607481; font-weight: bold; background: #EDF1F3; "
            "border: 1px solid #D5DDE2; border-radius: 10px; padding: 3px 8px;");
        cardHeader->addWidget(nameLabel);
        cardHeader->addStretch();
        cardHeader->addWidget(ui.lblPressureStatus[channel]);
        ui.lblPressureValue[channel] = new QLabel(initialValue);
        ui.lblPressureValue[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureValue[channel]->setStyleSheet(
            "font-size: 29px; color: #8E44AD; font-weight: bold; font-family: 'WenQuanYi Micro Hei';");
        ui.lblPressureDetails[channel] = new QLabel("-- V · -- %FS");
        ui.lblPressureDetails[channel]->setAlignment(Qt::AlignCenter);
        ui.lblPressureDetails[channel]->setStyleSheet("font-size: 15px; color: #607481;");
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
    actuatorLayout->addWidget(flowModeGroup, 1);
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
    liquidStateTitle->setStyleSheet("font-size: 18px; color: #6E5A2F; font-weight: bold;");
    ui.lblLiquidState = new QLabel("异常");
    ui.lblLiquidState->setAlignment(Qt::AlignCenter);
    ui.lblLiquidState->setMinimumWidth(82);
    ui.lblLiquidState->setStyleSheet("font-size: 18px; color: #A93226; font-weight: bold; background: #FDEDEC; border: 1px solid #F5B7B1; border-radius: 12px; padding: 4px 12px;");
    ui.liquidLog = new QTextBrowser();
    ui.liquidLog->setMinimumHeight(330);
    ui.liquidLog->setStyleSheet("font-size: 16px; color: #405462; background-color: #F8FAFB; border: 1px solid #D7E0E6; border-radius: 8px; padding: 8px;");
    liquidButtonLayout->addWidget(ui.btnLiquidStart);
    liquidButtonLayout->addWidget(ui.btnLiquidStop);
    liquidButtonLayout->addWidget(liquidStateTitle);
    liquidButtonLayout->addWidget(ui.lblLiquidState);
    liquidButtonLayout->addStretch();
    liquidButtonLayout->addWidget(ui.btnDrain);
    QLabel *liquidLogTitle = new QLabel("运行记录");
    liquidLogTitle->setStyleSheet("font-size: 17px; color: #526471; font-weight: bold; padding: 2px 2px 0 2px;");
    liquidLayout->addWidget(liquidStatusBar);
    liquidLayout->addWidget(liquidLogTitle);
    liquidLayout->addWidget(ui.liquidLog);
    liquidTabLayout->addWidget(liquidGroup);
    ui.tabs->addTab(liquidTab, "液位");

    QWidget *algorithmTab = new QWidget();
    QVBoxLayout *algorithmLayout = new QVBoxLayout(algorithmTab);
    algorithmLayout->setContentsMargins(4, 8, 4, 4);
    algorithmLayout->setSpacing(8);
    QLabel *algorithmIntro = new QLabel("调整 OPC 脉冲识别阈值和颗粒计数标定参数。阈值修改后立即生效并自动保存，标定系数需点击“应用并保存”。");
    algorithmIntro->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    algorithmIntro->setWordWrap(true);
    algorithmIntro->setStyleSheet("font-size: 17px; color: #526471; padding: 2px 0;");
    QGroupBox *algorithmGroup = new QGroupBox("OPC 算法设置");
    algorithmGroup->setStyleSheet(cardStyle("#2E86C1"));
    QGridLayout *algorithmGrid = new QGridLayout(algorithmGroup);
    algorithmGrid->setContentsMargins(14, 16, 14, 14);
    algorithmGrid->setHorizontalSpacing(10);
    algorithmGrid->setVerticalSpacing(12);

    ui.sbOpcMinRange = new TouchDoubleSpinBox("设置阈值下限");
    ui.sbOpcMinRange->setRange(0.001, 2.0);
    ui.sbOpcMinRange->setDecimals(4);
    ui.sbOpcMinRange->setSingleStep(0.005);
    ui.sbOpcMinRange->setSuffix(" V");
    ui.sbOpcMinRange->setValue(opcParams.minRange);

    ui.sbOpcThresholdOffset = new TouchDoubleSpinBox("设置阈值偏置");
    ui.sbOpcThresholdOffset->setRange(-1.0, 1.0);
    ui.sbOpcThresholdOffset->setDecimals(4);
    ui.sbOpcThresholdOffset->setSingleStep(0.001);
    ui.sbOpcThresholdOffset->setSuffix(" V");
    ui.sbOpcThresholdOffset->setValue(opcParams.thresholdOffset);

    ui.sbOpcWindowMs = new TouchDoubleSpinBox("设置计算间隔");
    ui.sbOpcWindowMs->setRange(1.0, 1000.0);
    ui.sbOpcWindowMs->setDecimals(1);
    ui.sbOpcWindowMs->setSingleStep(1.0);
    ui.sbOpcWindowMs->setSuffix(" ms");
    ui.sbOpcWindowMs->setValue(opcParams.windowMs);

    QLabel *lblAlgorithmSummary = new QLabel();
    lblAlgorithmSummary->setStyleSheet("font-size: 18px; color: #2C5D7C; font-weight: bold; padding: 10px 12px; background: #F0F7FB; border: 1px solid #D4E6F1; border-radius: 7px;");
    lblAlgorithmSummary->setWordWrap(true);
    ui.lblOpcAlgorithmRealtime = new QLabel(
        "实时算法结果：等待 OPC 采集数据（cutoff 为实际判定阈值，offset 为人工修正量）");
    ui.lblOpcAlgorithmRealtime->setStyleSheet(
        "font-size: 18px; color: #176B55; font-weight: bold; padding: 10px 12px; "
        "background: #EDF8F4; border: 1px solid #BFE3D7; border-radius: 7px;");
    ui.lblOpcAlgorithmRealtime->setWordWrap(true);

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
        label->setStyleSheet("font-size: 19px; color: #2C5D7C; font-weight: bold;");
        QLabel *hint = new QLabel(description);
        hint->setWordWrap(true);
        hint->setStyleSheet("font-size: 16px; color: #71828E;");
        spinBox->setMinimumHeight(40);
        spinBox->setStyleSheet("QDoubleSpinBox { padding: 4px 8px; background: #F9FBFC; border: 1px solid #C7D1D9; border-radius: 6px; font-size: 19px; } QDoubleSpinBox:focus { border: 2px solid #2E86C1; background: #FFFFFF; }");
        layout->addWidget(label);
        layout->addWidget(hint);
        layout->addSpacing(3);
        layout->addWidget(spinBox);
        return card;
    };

    algorithmGrid->addWidget(algorithmIntro, 0, 0, 1, 3);
    algorithmGrid->addWidget(
        createAlgorithmControl("阈值下限 · cutoff", "限制噪声宽度采用的最小值", ui.sbOpcMinRange), 1, 0);
    algorithmGrid->addWidget(
        createAlgorithmControl("阈值偏置 · offset", "在动态阈值基础上叠加修正", ui.sbOpcThresholdOffset), 1, 1);
    algorithmGrid->addWidget(
        createAlgorithmControl("计算间隔", "重新估计本底与噪声的周期", ui.sbOpcWindowMs), 1, 2);
    algorithmGrid->addWidget(lblAlgorithmSummary, 2, 0, 1, 3);
    algorithmGrid->addWidget(ui.lblOpcAlgorithmRealtime, 3, 0, 1, 3);
    for (int column = 0; column < 3; ++column) algorithmGrid->setColumnStretch(column, 1);

    auto refreshAlgorithmSummary = [lblAlgorithmSummary, &opcParams]() {
        lblAlgorithmSummary->setText(QString("当前规则：阈值 = 本底 + 限幅后的噪声宽度 + 偏置    |    噪声范围 %1 ~ %2 V    |    偏置 %3 V    |    每 %4 ms 更新")
            .arg(opcParams.minRange, 0, 'f', 4)
            .arg(opcParams.maxRange, 0, 'f', 4)
            .arg(opcParams.thresholdOffset, 0, 'f', 4)
            .arg(opcParams.windowMs, 0, 'f', 1));
    };

    QObject::connect(ui.sbOpcMinRange, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.minRange = val;
        refreshAlgorithmSummary();
    });
    QObject::connect(ui.sbOpcThresholdOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.thresholdOffset = val;
        refreshAlgorithmSummary();
    });
    QObject::connect(ui.sbOpcWindowMs, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&opcParams, refreshAlgorithmSummary](double val) {
        opcParams.windowMs = val;
        refreshAlgorithmSummary();
    });
    refreshAlgorithmSummary();

    algorithmLayout->addWidget(algorithmGroup);

    QGroupBox *calibrationGroup = new QGroupBox("颗粒计数二次标定");
    calibrationGroup->setStyleSheet(cardStyle("#16A085"));
    QGridLayout *calibrationGrid = new QGridLayout(calibrationGroup);
    calibrationGrid->setContentsMargins(14, 18, 14, 12);
    calibrationGrid->setHorizontalSpacing(10);
    calibrationGrid->setVerticalSpacing(8);

    QLabel *calibrationFormula = new QLabel(
        "标定公式：y = a × x² + b × x + c　　x：原始计数速率（个/s）　　y：标定后计数速率（个/s）");
    calibrationFormula->setWordWrap(true);
    calibrationFormula->setStyleSheet(
        "font-size: 17px; color: #286B60; font-weight: bold; padding: 7px 10px; "
        "background: #ECF8F5; border: 1px solid #BFE5DC; border-radius: 7px;");

    ui.sbParticleCalibrationA = new TouchDoubleSpinBox("设置二次项系数 a");
    ui.sbParticleCalibrationB = new TouchDoubleSpinBox("设置一次项系数 b");
    ui.sbParticleCalibrationC = new TouchDoubleSpinBox("设置常数项系数 c");
    ui.sbParticleCalibrationA->setRange(-1000000.0, 1000000.0);
    ui.sbParticleCalibrationA->setDecimals(6);
    ui.sbParticleCalibrationA->setSingleStep(0.000001);
    ui.sbParticleCalibrationA->setValue(particleCalibration.a);
    ui.sbParticleCalibrationB->setRange(-1000000.0, 1000000.0);
    ui.sbParticleCalibrationB->setDecimals(4);
    ui.sbParticleCalibrationB->setSingleStep(0.001);
    ui.sbParticleCalibrationB->setValue(particleCalibration.b);
    ui.sbParticleCalibrationC->setRange(-1000000000.0, 1000000000.0);
    ui.sbParticleCalibrationC->setDecimals(2);
    ui.sbParticleCalibrationC->setSingleStep(1.0);
    ui.sbParticleCalibrationC->setValue(particleCalibration.c);

    const QString calibrationSpinStyle =
        "QDoubleSpinBox { min-height: 38px; padding: 3px 8px; background: #F9FBFC; "
        "border: 1px solid #B7D8D0; border-radius: 7px; color: #234D46; "
        "font-size: 19px; font-weight: 600; } "
        "QDoubleSpinBox:focus { border: 2px solid #16A085; background: #FFFFFF; }";
    ui.sbParticleCalibrationA->setStyleSheet(calibrationSpinStyle);
    ui.sbParticleCalibrationB->setStyleSheet(calibrationSpinStyle);
    ui.sbParticleCalibrationC->setStyleSheet(calibrationSpinStyle);

    auto createCalibrationControl = [](const QString& title, QDoubleSpinBox *spinBox) {
        QWidget *container = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        QLabel *label = new QLabel(title);
        label->setStyleSheet("font-size: 18px; color: #286B60; font-weight: bold;");
        layout->addWidget(label);
        layout->addWidget(spinBox);
        return container;
    };

    ui.btnParticleCalibrationApply = new QPushButton("应用并保存");
    ui.btnParticleCalibrationReset = new QPushButton("恢复默认");
    ui.btnParticleCalibrationApply->setStyleSheet(solidButtonStyle("#16A085", "#117A65"));
    ui.btnParticleCalibrationReset->setStyleSheet(solidButtonStyle("#71858A", "#56696E"));
    ui.lblParticleCalibrationStatus = new QLabel("默认系数 a=0、b=1、c=0，标定前后数值一致");
    ui.lblParticleCalibrationStatus->setWordWrap(true);
    ui.lblParticleCalibrationStatus->setStyleSheet(
        "font-size: 16px; color: #416A63; padding: 4px 7px;");

    calibrationGrid->addWidget(calibrationFormula, 0, 0, 1, 5);
    calibrationGrid->addWidget(createCalibrationControl("a · 二次项", ui.sbParticleCalibrationA), 1, 0);
    calibrationGrid->addWidget(createCalibrationControl("b · 一次项", ui.sbParticleCalibrationB), 1, 1);
    calibrationGrid->addWidget(createCalibrationControl("c · 常数项", ui.sbParticleCalibrationC), 1, 2);
    calibrationGrid->addWidget(ui.btnParticleCalibrationApply, 1, 3);
    calibrationGrid->addWidget(ui.btnParticleCalibrationReset, 1, 4);
    calibrationGrid->addWidget(ui.lblParticleCalibrationStatus, 2, 0, 1, 5);
    for (int column = 0; column < 3; ++column) calibrationGrid->setColumnStretch(column, 1);

    algorithmLayout->addWidget(calibrationGroup);
    algorithmLayout->addStretch();
    ui.tabs->addTab(algorithmTab, "算法");

    ui.communicationTab = new QWidget();
    QVBoxLayout *communicationLayout = new QVBoxLayout(ui.communicationTab);
    communicationLayout->setContentsMargins(4, 8, 4, 4);
    communicationLayout->setSpacing(8);

    QGroupBox *networkGroup = new QGroupBox("本机有线网络 · 仅管理 eth0 / Ethernet");
    networkGroup->setStyleSheet(cardStyle("#2980B9"));
    QGridLayout *networkLayout = new QGridLayout(networkGroup);
    networkLayout->setContentsMargins(14, 28, 14, 10);
    networkLayout->setHorizontalSpacing(8);
    networkLayout->setVerticalSpacing(6);
    const QString networkCaptionStyle =
        "font-size: 16px; color: #607481; font-weight: bold;";
    const QString networkValueStyle =
        "font-size: 18px; color: #2C5D7C; font-weight: bold; background: #F7FAFC; "
        "border: 1px solid #D7E2E9; border-radius: 6px; padding: 4px 8px;";
    auto makeCaption = [&](const QString &text) {
        QLabel *label = new QLabel(text);
        label->setStyleSheet(networkCaptionStyle);
        return label;
    };
    auto makeNetworkValue = [&]() {
        QLabel *label = new QLabel("--");
        label->setStyleSheet(networkValueStyle);
        label->setMinimumHeight(34);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    ui.lblNetworkInterface = makeNetworkValue();
    ui.lblNetworkIpv4 = makeNetworkValue();
    ui.lblNetworkLinkState = makeNetworkValue();
    ui.lblNetworkProfile = makeNetworkValue();
    networkLayout->addWidget(makeCaption("网卡"), 0, 0);
    networkLayout->addWidget(ui.lblNetworkInterface, 0, 1);
    networkLayout->addWidget(makeCaption("链路"), 0, 2);
    networkLayout->addWidget(ui.lblNetworkLinkState, 0, 3);
    networkLayout->addWidget(makeCaption("当前实际 IP"), 0, 4);
    networkLayout->addWidget(ui.lblNetworkIpv4, 0, 5, 1, 2);
    networkLayout->addWidget(makeCaption("连接配置"), 0, 7);
    networkLayout->addWidget(ui.lblNetworkProfile, 0, 8);

    ui.cmbNetworkIpv4Mode = new QComboBox();
    ui.cmbNetworkIpv4Mode->addItem("DHCP 自动获取");
    ui.cmbNetworkIpv4Mode->addItem("静态 IP");
    ui.cmbNetworkIpv4Mode->setMinimumWidth(176);
    ui.cmbNetworkIpv4Mode->setMinimumHeight(38);
    auto createIpv4Editor = [&](const QString &title, QDoubleSpinBox *segments[4]) {
        QWidget *editor = new QWidget();
        QHBoxLayout *layout = new QHBoxLayout(editor);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        for (int i = 0; i < 4; ++i) {
            TouchDoubleSpinBox *segment = new TouchDoubleSpinBox(
                QString("%1 · 第 %2 段").arg(title).arg(i + 1));
            segment->setRange(0, 255);
            segment->setDecimals(0);
            segment->setFixedSize(66, 38);
            segment->setStyleSheet(
                "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #B9C8D2; "
                "border-radius: 6px; color: #264C63; font-size: 19px; font-weight: bold; }"
                "QDoubleSpinBox:focus { border: 2px solid #2E86C1; }"
                "QDoubleSpinBox:disabled { background: #E8ECEF; color: #929DA5; }");
            segments[i] = segment;
            layout->addWidget(segment);
            if (i != 3) {
                QLabel *dot = new QLabel(".");
                dot->setStyleSheet("font-size: 22px; color: #526471; font-weight: bold;");
                layout->addWidget(dot);
            }
        }
        layout->addStretch();
        return editor;
    };
    QWidget *ipEditor = createIpv4Editor("本机 IPv4", ui.sbNetworkIp);
    ui.sbNetworkPrefix = new TouchDoubleSpinBox("设置 IPv4 前缀长度");
    ui.sbNetworkPrefix->setRange(1, 32);
    ui.sbNetworkPrefix->setDecimals(0);
    ui.sbNetworkPrefix->setValue(24);
    ui.sbNetworkPrefix->setPrefix("/");
    ui.sbNetworkPrefix->setFixedSize(76, 38);
    ui.chkNetworkGateway = new QCheckBox("网关");
    ui.chkNetworkGateway->setStyleSheet(networkCaptionStyle +
        " QCheckBox::indicator { width: 25px; height: 25px; }");
    QWidget *gatewayEditor = createIpv4Editor("网关", ui.sbNetworkGateway);
    ui.chkNetworkDns = new QCheckBox("DNS");
    ui.chkNetworkDns->setStyleSheet(networkCaptionStyle +
        " QCheckBox::indicator { width: 25px; height: 25px; }");
    QWidget *dnsEditor = createIpv4Editor("DNS", ui.sbNetworkDns);
    ui.lblNetworkNetmask = new QLabel("/24 = 255.255.255.0");
    ui.lblNetworkNetmask->setStyleSheet(
        "font-size: 16px; color: #526471; font-weight: bold;");
    ui.lblRecommendedIpc = new QLabel("推荐工控机：192.168.50.1/24");
    ui.lblRecommendedIpc->setStyleSheet(
        "font-size: 16px; color: #176B55; font-weight: bold;");
    ui.lblRecommendedIpc->setWordWrap(true);

    networkLayout->addWidget(makeCaption("IPv4 模式"), 1, 0);
    networkLayout->addWidget(ui.cmbNetworkIpv4Mode, 1, 1, 1, 2);
    networkLayout->addWidget(makeCaption("IP 地址"), 1, 3);
    networkLayout->addWidget(ipEditor, 1, 4, 1, 3);
    networkLayout->addWidget(makeCaption("前缀"), 1, 7);
    networkLayout->addWidget(ui.sbNetworkPrefix, 1, 8);
    networkLayout->addWidget(ui.chkNetworkGateway, 2, 0);
    networkLayout->addWidget(gatewayEditor, 2, 1, 1, 3);
    networkLayout->addWidget(ui.chkNetworkDns, 2, 4);
    networkLayout->addWidget(dnsEditor, 2, 5, 1, 4);
    networkLayout->addWidget(ui.lblNetworkNetmask, 3, 0, 1, 4);
    networkLayout->addWidget(ui.lblRecommendedIpc, 3, 4, 1, 5);

    ui.lblNetworkOperationStatus = new QLabel("正在读取 NetworkManager 配置...");
    ui.lblNetworkOperationStatus->setWordWrap(true);
    ui.lblNetworkOperationStatus->setStyleSheet(
        "font-size: 16px; color: #526471; font-weight: bold; padding: 3px 6px;");
    ui.btnNetworkRefresh = new QPushButton("刷新网络");
    ui.btnNetworkReset = new QPushButton("恢复推荐配置");
    ui.btnNetworkApply = new QPushButton("应用网络设置");
    ui.btnNetworkRefresh->setFixedSize(120, 42);
    ui.btnNetworkReset->setFixedSize(156, 42);
    ui.btnNetworkApply->setFixedSize(156, 42);
    ui.btnNetworkRefresh->setStyleSheet(solidButtonStyle("#2980B9", "#21618C"));
    ui.btnNetworkReset->setStyleSheet(solidButtonStyle("#71858A", "#56696E"));
    ui.btnNetworkApply->setStyleSheet(solidButtonStyle("#167D68", "#116454"));
    networkLayout->addWidget(ui.lblNetworkOperationStatus, 4, 0, 1, 4);
    networkLayout->addWidget(ui.btnNetworkRefresh, 4, 5);
    networkLayout->addWidget(ui.btnNetworkReset, 4, 6, 1, 2);
    networkLayout->addWidget(ui.btnNetworkApply, 4, 8);
    networkLayout->setColumnStretch(1, 1);
    networkLayout->setColumnStretch(4, 1);
    networkLayout->setColumnStretch(5, 1);
    networkLayout->setColumnStretch(8, 1);

    QHBoxLayout *serviceLayout = new QHBoxLayout();
    serviceLayout->setSpacing(8);
    const QString serviceValueStyle =
        "font-size: 18px; color: #405766; font-weight: bold; background: #F7FAFC; "
        "border: 1px solid #DCE4EA; border-radius: 7px; padding: 6px 10px;";
    const QString enableButtonStyle =
        "QPushButton { min-width: 104px; min-height: 44px; background: #7F8C8D; color: white; "
        "border: none; border-radius: 22px; font-size: 19px; font-weight: bold; }"
        "QPushButton:checked { background: #16A085; }"
        "QPushButton:pressed { padding-top: 2px; }";
    const QString portStyle =
        "QDoubleSpinBox { min-height: 44px; padding: 3px 10px; background: #FFFFFF; "
        "border: 1px solid #B9C8D2; border-radius: 7px; color: #264C63; "
        "font-size: 22px; font-weight: bold; }"
        "QDoubleSpinBox:focus { border: 2px solid #2E86C1; }";

    QGroupBox *webGroup = new QGroupBox("Web 远程看板 · HTTP");
    webGroup->setStyleSheet(cardStyle("#16A085"));
    QGridLayout *webLayout = new QGridLayout(webGroup);
    webLayout->setContentsMargins(14, 29, 14, 12);
    webLayout->setHorizontalSpacing(10);
    webLayout->setVerticalSpacing(8);
    ui.lblWebStatus = new QLabel("● 状态未知");
    ui.lblWebStatus->setAlignment(Qt::AlignCenter);
    ui.lblWebStatus->setMinimumHeight(38);
    ui.lblWebStatus->setWordWrap(true);
    ui.btnWebEnable = new QPushButton("开");
    ui.btnWebEnable->setCheckable(true);
    ui.btnWebEnable->setChecked(true);
    ui.btnWebEnable->setStyleSheet(enableButtonStyle);
    ui.sbWebPort = new TouchDoubleSpinBox("设置 Web 远程看板端口");
    ui.sbWebPort->setRange(1024.0, 65535.0);
    ui.sbWebPort->setDecimals(0);
    ui.sbWebPort->setSingleStep(1.0);
    ui.sbWebPort->setValue(8080.0);
    ui.sbWebPort->setStyleSheet(portStyle);
    ui.lblWebAddress = new QLabel("网络地址不可用");
    ui.lblWebAddress->setStyleSheet(serviceValueStyle);
    ui.lblWebAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QLabel *webEnableCaption = new QLabel("启用远程看板");
    QLabel *webPortCaption = new QLabel("HTTP 端口");
    QLabel *webAddressCaption = new QLabel("当前访问地址");
    webLayout->addWidget(ui.lblWebStatus, 0, 0, 1, 2);
    webLayout->addWidget(webEnableCaption, 1, 0);
    webLayout->addWidget(ui.btnWebEnable, 1, 1);
    webLayout->addWidget(webPortCaption, 2, 0);
    webLayout->addWidget(ui.sbWebPort, 2, 1);
    webLayout->addWidget(webAddressCaption, 3, 0, 1, 2);
    webLayout->addWidget(ui.lblWebAddress, 4, 0, 1, 2);
    webLayout->setColumnStretch(1, 1);

    QGroupBox *tcpGroup = new QGroupBox("工控机 TCP 通讯 · CPC Protocol V1.0");
    tcpGroup->setStyleSheet(cardStyle("#8E44AD"));
    QGridLayout *tcpLayout = new QGridLayout(tcpGroup);
    tcpLayout->setContentsMargins(14, 29, 14, 12);
    tcpLayout->setHorizontalSpacing(10);
    tcpLayout->setVerticalSpacing(7);
    ui.lblTcpStatus = new QLabel("● 状态未知");
    ui.lblTcpStatus->setAlignment(Qt::AlignCenter);
    ui.lblTcpStatus->setMinimumHeight(38);
    ui.lblTcpStatus->setWordWrap(true);
    ui.btnTcpEnable = new QPushButton("开");
    ui.btnTcpEnable->setCheckable(true);
    ui.btnTcpEnable->setChecked(true);
    ui.btnTcpEnable->setStyleSheet(enableButtonStyle);
    ui.sbTcpPort = new TouchDoubleSpinBox("设置工控机 TCP 端口");
    ui.sbTcpPort->setRange(1024.0, 65535.0);
    ui.sbTcpPort->setDecimals(0);
    ui.sbTcpPort->setSingleStep(1.0);
    ui.sbTcpPort->setValue(5000.0);
    ui.sbTcpPort->setStyleSheet(portStyle);
    ui.lblTcpAddress = new QLabel("网络地址不可用");
    ui.lblTcpClient = new QLabel("未连接");
    ui.lblTcpLastSequence = new QLabel("暂无数据");
    ui.lblTcpLastSendTime = new QLabel("暂无数据");
    for (QLabel *label : {ui.lblTcpAddress, ui.lblTcpClient,
                          ui.lblTcpLastSequence, ui.lblTcpLastSendTime}) {
        label->setStyleSheet(serviceValueStyle);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    QLabel *tcpEnableCaption = new QLabel("启用工控机 TCP");
    QLabel *tcpPortCaption = new QLabel("TCP 端口");
    QLabel *tcpAddressCaption = new QLabel("当前服务地址");
    QLabel *tcpClientCaption = new QLabel("客户端");
    QLabel *tcpLastCaption = new QLabel("最近发送");
    tcpLayout->addWidget(ui.lblTcpStatus, 0, 0, 1, 4);
    tcpLayout->addWidget(tcpEnableCaption, 1, 0);
    tcpLayout->addWidget(ui.btnTcpEnable, 1, 1);
    tcpLayout->addWidget(tcpPortCaption, 1, 2);
    tcpLayout->addWidget(ui.sbTcpPort, 1, 3);
    tcpLayout->addWidget(tcpAddressCaption, 2, 0);
    tcpLayout->addWidget(ui.lblTcpAddress, 2, 1, 1, 3);
    tcpLayout->addWidget(tcpClientCaption, 3, 0);
    tcpLayout->addWidget(ui.lblTcpClient, 3, 1, 1, 3);
    tcpLayout->addWidget(tcpLastCaption, 4, 0);
    tcpLayout->addWidget(ui.lblTcpLastSequence, 4, 1);
    tcpLayout->addWidget(ui.lblTcpLastSendTime, 4, 2, 1, 2);
    tcpLayout->setColumnStretch(1, 1);
    tcpLayout->setColumnStretch(3, 1);

    serviceLayout->addWidget(webGroup, 1);
    serviceLayout->addWidget(tcpGroup, 1);

    QWidget *communicationActions = new QWidget();
    QHBoxLayout *communicationActionLayout = new QHBoxLayout(communicationActions);
    communicationActionLayout->setContentsMargins(0, 0, 0, 0);
    communicationActionLayout->setSpacing(10);
    ui.lblCommunicationApplyStatus = new QLabel("当前配置已应用");
    ui.lblCommunicationApplyStatus->setWordWrap(true);
    ui.lblCommunicationApplyStatus->setStyleSheet(
        "font-size: 17px; color: #526471; font-weight: bold; padding: 6px 10px;");
    ui.btnCommunicationReset = new QPushButton("恢复通讯默认");
    ui.btnCommunicationApply = new QPushButton("应用通讯设置");
    ui.btnCommunicationReset->setFixedSize(170, 46);
    ui.btnCommunicationApply->setFixedSize(180, 46);
    ui.btnCommunicationReset->setStyleSheet(
        solidButtonStyle("#71858A", "#56696E") +
        "QPushButton { min-height: 48px; }");
    ui.btnCommunicationApply->setStyleSheet(
        solidButtonStyle("#167D68", "#116454") +
        "QPushButton { min-height: 48px; }");
    communicationActionLayout->addWidget(ui.lblCommunicationApplyStatus, 1);
    communicationActionLayout->addWidget(ui.btnCommunicationReset);
    communicationActionLayout->addWidget(ui.btnCommunicationApply);

    communicationLayout->addWidget(networkGroup, 0);
    communicationLayout->addLayout(serviceLayout, 1);
    communicationLayout->addWidget(communicationActions);
    ui.tabs->addTab(ui.communicationTab, "通讯");

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
    lblProcess->setStyleSheet("color: #68457A; font-size: 18px; font-weight: bold; background: #F7F0FA; border: 1px solid #E4D3EB; border-radius: 7px; padding: 8px;");
    ui.opcPlot = new QCustomPlot();
    ui.opcPlot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui.opcPlot->setMinimumHeight(260);
    setupOpcPlot(ui.opcPlot);
    QHBoxLayout *opcPlotToolbar = new QHBoxLayout();
    opcPlotToolbar->setContentsMargins(2, 0, 2, 0);
    QLabel *opcTouchHint = new QLabel("单指平移 · 双指缩放 · 双击还原（触摸后暂停实时跟随）");
    opcTouchHint->setStyleSheet("color: #607784; font-size: 16px;");
    ui.btnResetOpcPlot = new QPushButton("还原视图");
    ui.btnResetOpcPlot->setFixedSize(88, 28);
    ui.btnResetOpcPlot->setToolTip("恢复最近 50 ms 波形的实时跟随和自动电压量程");
    ui.btnResetOpcPlot->setStyleSheet("QPushButton { background-color: #F7FAFC; color: #3C596B; border: 1px solid #C8D4DC; border-radius: 5px; min-height: 24px; font-size: 16px; font-weight: bold; } QPushButton:hover { background-color: #EAF2F7; border-color: #8FA7B7; } QPushButton:pressed { background-color: #DDE8EF; }");
    opcPlotToolbar->addWidget(opcTouchHint);
    opcPlotToolbar->addStretch();
    opcPlotToolbar->addWidget(ui.btnResetOpcPlot);
    opcControlLayout->addWidget(lblProcess);
    opcControlLayout->addLayout(opcPlotToolbar);
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
