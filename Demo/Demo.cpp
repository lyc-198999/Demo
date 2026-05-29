#include "Demo.h"
#include "SharpnessTrendWidget.h"
#include "ui_Demo.h"

#include <algorithm>
#include <iterator>
#include <QDateTime>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QIODevice>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QTabWidget>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

namespace DemoWindowDetail
{
constexpr int kFrameIntervalMs = 80;
constexpr int kMaxLogBlocks = 200;
constexpr double kPulsesPerTurn = 51200.0;
constexpr int kImageMinimumWidth = 420;
constexpr int kImageMinimumHeight = 236;
constexpr int kControlPanelMinimumWidth = 320;
constexpr int kControlPanelMaximumWidth = 460;
constexpr int kLogPanelMinimumHeight = 180;
constexpr int kLogTextMinimumHeight = 140;
constexpr int kImageColumnStretch = 3;
constexpr int kControlColumnStretch = 2;

// 用法：清空布局项但保留其中的已有控件，便于把 Designer 控件重组成页签。
void ClearLayoutItems(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0))
    {
        delete item;
    }
}

// 用法：创建右侧页签中的单页容器。
QWidget* CreateTabPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    layout->addStretch(1);
    return page;
}

// 用法：把控件插入页签页，并保持页面尾部弹性空间在最后。
void AddWidgetBeforeStretch(QWidget* page, QWidget* widget)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    const int insertIndex = std::max(0, layout->count() - 1);
    layout->insertWidget(insertIndex, widget);
}

// 用法：将右侧功能区拆成运行、连接、参数三个页签，降低长表单带来的扫描负担。
void ConfigureControlPanelTabs(Ui::DemoClass* ui, QWidget* controlPanelWidget)
{
    ClearLayoutItems(ui->verticalLayoutControlPanel);
    ui->verticalLayoutControlPanel->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayoutControlPanel->setSpacing(10);

    auto* tabs = new QTabWidget(controlPanelWidget);
    tabs->setObjectName("tabs_control_panel");

    QWidget* runPage = CreateTabPage(tabs);
    QWidget* connectionPage = CreateTabPage(tabs);
    QWidget* paramsPage = CreateTabPage(tabs);

    AddWidgetBeforeStretch(runPage, ui->group_mode);
    AddWidgetBeforeStretch(runPage, ui->group_manual);
    AddWidgetBeforeStretch(runPage, ui->btn_emergency_stop);

    AddWidgetBeforeStretch(connectionPage, ui->group_serial);
    AddWidgetBeforeStretch(paramsPage, ui->group_motor_params);

    tabs->addTab(runPage, QStringLiteral("运行"));
    tabs->addTab(connectionPage, QStringLiteral("连接"));
    tabs->addTab(paramsPage, QStringLiteral("参数"));

    ui->verticalLayoutControlPanel->addWidget(ui->group_status);
    ui->verticalLayoutControlPanel->addWidget(tabs, 1);
}

// 用法：把右侧功能区放进独立滚动区，避免展开低频设置时撑大主窗口或挤压日志。
void ConfigureControlPanelScrollArea(Ui::DemoClass* ui)
{
    auto* controlPanelWidget = new QWidget(ui->centralWidget);
    controlPanelWidget->setObjectName("widget_control_panel");
    controlPanelWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    ui->gridLayoutRoot->removeItem(ui->verticalLayoutControlPanel);
    ui->verticalLayoutControlPanel->setParent(nullptr);
    controlPanelWidget->setLayout(ui->verticalLayoutControlPanel);
    ConfigureControlPanelTabs(ui, controlPanelWidget);

    auto* scrollArea = new QScrollArea(ui->centralWidget);
    scrollArea->setObjectName("scroll_control_panel");
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(kControlPanelMinimumWidth);
    scrollArea->setMaximumWidth(kControlPanelMaximumWidth);
    scrollArea->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    scrollArea->setWidget(controlPanelWidget);

    ui->gridLayoutRoot->addWidget(scrollArea, 1, 1, 1, 1);
}

// 用法：让右侧状态文本在窄面板内换行显示，避免撑宽或被裁切。
void ConfigureStatusTextLayout(Ui::DemoClass* ui)
{
    ui->formLayoutStatus->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    ui->formLayoutMotorParams->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    ui->gridLayoutSerial->setColumnStretch(1, 1);

    ui->label_position->setWordWrap(true);
    ui->label_position->setMinimumWidth(0);
    ui->label_position->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    ui->label_status->setWordWrap(true);
    ui->label_status->setMinimumWidth(0);
    ui->label_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

// 用法：调整主布局比例，保留右侧功能区和日志区的最低可读空间。
void ConfigureMainLayout(Ui::DemoClass* ui)
{
    ConfigureStatusTextLayout(ui);

    ui->label_image->setMinimumSize(kImageMinimumWidth, kImageMinimumHeight);
    ui->gridLayoutRoot->setColumnMinimumWidth(1, kControlPanelMinimumWidth);
    ui->gridLayoutRoot->setColumnStretch(0, kImageColumnStretch);
    ui->gridLayoutRoot->setColumnStretch(1, kControlColumnStretch);
    ui->group_log->setMinimumHeight(kLogPanelMinimumHeight);
    ui->text_log->setMinimumHeight(kLogTextMinimumHeight);
    ui->gridLayoutRoot->setRowStretch(0, 0);
    ui->gridLayoutRoot->setRowStretch(1, 4);
    ui->gridLayoutRoot->setRowStretch(2, 1);
}
}

// 用法：创建现代化仪器布局、监控看板和全局样式。
void MainWindow::initializeModernUi()
{
    setWindowTitle(QStringLiteral("自动对焦系统"));
    setMinimumSize(885, 680);
    resize(1180, 840);

    QFile themeFile(QStringLiteral(":/Demo/ModernTheme.qss"));
    if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStyleSheet(QString::fromUtf8(themeFile.readAll()));
    }

    ui->menuBar->hide();
    ui->mainToolBar->hide();
    ui->btn_emergency_stop->setStyleSheet(QString());
    ui->label_image->setStyleSheet(QString());
    ui->label_image->setFrameShape(QFrame::NoFrame);

    ui->group_image->setTitle(QStringLiteral("实时图像"));
    ui->group_log->setTitle(QStringLiteral("事件日志"));
    ui->group_status->setTitle(QStringLiteral("状态摘要"));
    ui->group_mode->setTitle(QStringLiteral("模式"));
    ui->group_manual->setTitle(QStringLiteral("手动控制"));
    ui->group_serial->setTitle(QStringLiteral("串口连接"));
    ui->group_motor_params->setTitle(QStringLiteral("电机参数"));

    ui->group_log->setCheckable(true);
    ui->group_log->setChecked(true);
    connect(ui->group_log, &QGroupBox::toggled, this, [this](bool checked) {
        ui->text_log->setVisible(checked);
        ui->group_log->setMinimumHeight(checked ? DemoWindowDetail::kLogPanelMinimumHeight : 44);
        ui->group_log->setMaximumHeight(checked ? QWIDGETSIZE_MAX : 44);
    });

    ui->gridLayoutRoot->removeWidget(ui->group_image);
    ui->gridLayoutRoot->removeWidget(ui->group_log);
    DemoWindowDetail::ConfigureControlPanelScrollArea(ui);

    auto* topStatusBar = new QFrame(ui->centralWidget);
    topStatusBar->setObjectName("topStatusBar");
    auto* topLayout = new QHBoxLayout(topStatusBar);
    topLayout->setContentsMargins(14, 10, 14, 10);
    topLayout->setSpacing(8);

    auto* titleBlock = new QWidget(topStatusBar);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 10, 0);
    titleLayout->setSpacing(1);

    auto* titleLabel = new QLabel(QStringLiteral("自动对焦系统"), titleBlock);
    titleLabel->setObjectName("appTitle");
    auto* subtitleLabel = new QLabel(QStringLiteral("实时图像采集 / 电机控制 / 自动对焦监控"), titleBlock);
    subtitleLabel->setProperty("role", "subtitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subtitleLabel);
    topLayout->addWidget(titleBlock, 1);

    const auto createBadge = [topStatusBar](const QString& text) {
        auto* label = new QLabel(text, topStatusBar);
        label->setProperty("role", "badge");
        label->setProperty("badgeState", "neutral");
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    labelModeBadge = createBadge(QStringLiteral("手动模式"));
    labelCameraBadge = createBadge(QStringLiteral("相机未连接"));
    labelSerialBadge = createBadge(QStringLiteral("串口未连接"));
    labelMotorBadge = createBadge(QStringLiteral("电机未连接"));
    labelEmergencyBadge = createBadge(QStringLiteral("急停正常"));
    labelFocusStageBadge = createBadge(QStringLiteral("待机"));

    topLayout->addWidget(labelModeBadge);
    topLayout->addWidget(labelCameraBadge);
    topLayout->addWidget(labelSerialBadge);
    topLayout->addWidget(labelMotorBadge);
    topLayout->addWidget(labelEmergencyBadge);
    topLayout->addWidget(labelFocusStageBadge);

    auto* workspace = new QWidget(ui->centralWidget);
    workspace->setObjectName("workspace");
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(12);
    workspaceLayout->addWidget(ui->group_image, 3);

    auto* monitorGroup = new QGroupBox(QStringLiteral("对焦监控"), workspace);
    monitorGroup->setObjectName("group_monitor");
    auto* monitorLayout = new QGridLayout(monitorGroup);
    monitorLayout->setContentsMargins(12, 14, 12, 12);
    monitorLayout->setHorizontalSpacing(10);
    monitorLayout->setVerticalSpacing(10);

    const auto createMetric = [monitorGroup](const QString& title, QLabel*& valueLabel) {
        auto* frame = new QFrame(monitorGroup);
        frame->setProperty("role", "metric");
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(2);

        auto* titleLabel = new QLabel(title, frame);
        titleLabel->setProperty("role", "metricTitle");
        valueLabel = new QLabel(QStringLiteral("--"), frame);
        valueLabel->setProperty("role", "metricValue");
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        layout->addWidget(titleLabel);
        layout->addWidget(valueLabel);
        return frame;
    };

    monitorLayout->addWidget(createMetric(QStringLiteral("当前清晰度"), labelMetricSharpness), 0, 0);
    monitorLayout->addWidget(createMetric(QStringLiteral("当前位置"), labelMetricPosition), 0, 1);
    monitorLayout->addWidget(createMetric(QStringLiteral("采样峰值"), labelMetricBest), 0, 2);
    monitorLayout->addWidget(createMetric(QStringLiteral("目标位置"), labelMetricTarget), 0, 3);
    monitorLayout->addWidget(createMetric(QStringLiteral("采样数"), labelMetricSamples), 0, 4);

    progressFocusStage = new QProgressBar(monitorGroup);
    progressFocusStage->setRange(0, 100);
    progressFocusStage->setValue(0);
    progressFocusStage->setTextVisible(true);
    progressFocusStage->setFormat(QStringLiteral("待机"));
    monitorLayout->addWidget(progressFocusStage, 1, 0, 1, 5);

    sharpnessTrendWidget = new SharpnessTrendWidget(monitorGroup);
    monitorLayout->addWidget(sharpnessTrendWidget, 2, 0, 1, 5);

    workspaceLayout->addWidget(monitorGroup, 1);

    ui->gridLayoutRoot->addWidget(topStatusBar, 0, 0, 1, 2);
    ui->gridLayoutRoot->addWidget(workspace, 1, 0, 1, 1);
    ui->gridLayoutRoot->addWidget(ui->group_log, 2, 0, 1, 2);

    DemoWindowDetail::ConfigureMainLayout(ui);
}

// 用法：初始化界面控件、串口控件和相机刷新定时器。
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::DemoClass)
    , timer(new QTimer(this))
{
    ui->setupUi(this);
    initializeModernUi();

    ui->combo_baud->addItems(QStringList{ "9600", "19200", "38400", "57600", "115200" });
    ui->combo_baud->setCurrentText("115200");
    ui->text_log->document()->setMaximumBlockCount(DemoWindowDetail::kMaxLogBlocks);
    ui->btn_connect->setCheckable(true);
    ui->btn_emergency_stop->setCheckable(true);

    ui->spin_manual_step->setRange(1, 1000000);
    ui->spin_manual_speed->setRange(1, 3000);
    ui->spin_manual_acceleration->setRange(1, 255);
    ui->spin_auto_step->setRange(1, 1000000);
    ui->spin_auto_fine_step->setRange(1, 1000000);
    ui->spin_auto_speed->setRange(1, 3000);
    ui->spin_auto_acceleration->setRange(1, 255);

    ui->spin_manual_step->setValue(static_cast<int>(manualStep));
    ui->spin_manual_speed->setValue(static_cast<int>(manualSpeedRpm));
    ui->spin_manual_acceleration->setValue(static_cast<int>(manualAcceleration));
    ui->spin_auto_step->setValue(static_cast<int>(autoFocusScanStep));
    ui->spin_auto_fine_step->setMaximum(static_cast<int>(autoFocusScanStep));
    ui->spin_auto_fine_step->setValue(static_cast<int>(autoFocusFineStep));
    ui->spin_auto_speed->setValue(static_cast<int>(autoFocusSpeedRpm));
    ui->spin_auto_acceleration->setValue(static_cast<int>(autoFocusAcceleration));

    connect(timer, &QTimer::timeout, this, &MainWindow::updateFrame);
    connect(ui->radio_auto, &QRadioButton::toggled, this, &MainWindow::updateManualControlState);
    connect(ui->radio_manual, &QRadioButton::toggled, this, &MainWindow::updateManualControlState);
    connect(ui->btn_refresh, &QPushButton::clicked, this, &MainWindow::refreshSerialControls);
    connect(ui->btn_connect, &QPushButton::clicked, this, &MainWindow::toggleSerialConnection);
    connect(ui->btn_forward, &QPushButton::clicked, this, &MainWindow::handleManualForward);
    connect(ui->btn_backward, &QPushButton::clicked, this, &MainWindow::handleManualBackward);
    connect(ui->btn_emergency_stop, &QPushButton::toggled, this, &MainWindow::toggleEmergencyStop);
    connect(ui->spin_manual_step, &QSpinBox::valueChanged, this, [this](int value) {
        manualStep = static_cast<quint32>(value);
    });
    connect(ui->spin_manual_speed, &QSpinBox::valueChanged, this, [this](int value) {
        manualSpeedRpm = static_cast<quint16>(value);
    });
    connect(ui->spin_manual_acceleration, &QSpinBox::valueChanged, this, [this](int value) {
        manualAcceleration = static_cast<quint8>(value);
    });
    connect(ui->spin_auto_step, &QSpinBox::valueChanged, this, [this](int value) {
        autoFocusScanStep = static_cast<quint32>(value);
        ui->spin_auto_fine_step->setMaximum(value);
        if (autoFocusFineStep > autoFocusScanStep)
        {
            ui->spin_auto_fine_step->setValue(value);
        }
    });
    connect(ui->spin_auto_fine_step, &QSpinBox::valueChanged, this, [this](int value) {
        autoFocusFineStep = static_cast<quint32>(value);
    });
    connect(ui->spin_auto_speed, &QSpinBox::valueChanged, this, [this](int value) {
        autoFocusSpeedRpm = static_cast<quint16>(value);
    });
    connect(ui->spin_auto_acceleration, &QSpinBox::valueChanged, this, [this](int value) {
        autoFocusAcceleration = static_cast<quint8>(value);
    });

    refreshSerialControls();
    updateManualControlState();
    updateStatusDisplay();
    appendLog("窗口初始化完成。");
    appendLog("串口控制已切换为正点原子自定义协议模式。");
    appendLog(QString("手动模式默认参数：步数=%1，速度=%2RPM，加减速度=%3。")
                  .arg(manualStep)
                  .arg(manualSpeedRpm)
                  .arg(manualAcceleration));
    appendLog(QString("自动模式默认参数：扫描步数=%1，小步长=%2，速度=%3RPM，加减速度=%4。")
                  .arg(autoFocusScanStep)
                  .arg(autoFocusFineStep)
                  .arg(autoFocusSpeedRpm)
                  .arg(autoFocusAcceleration));

    timer->start(DemoWindowDetail::kFrameIntervalMs);
}

// 用法：窗口销毁时关闭串口并释放 UI。
MainWindow::~MainWindow()
{
    if (motorSerial.isOpen())
    {
        motorSerial.close();
    }

    delete ui;
}

// 用法：根据自动/手动单选按钮切换手动控制可用状态。
void MainWindow::updateManualControlState()
{
    const bool isManualMode = ui->radio_manual->isChecked();
    if (manualModeSelected != isManualMode)
    {
        appendLog(isManualMode ? "已切换到手动模式。" : "已切换到自动模式。");
        resetAutoFocusState(isManualMode);
        manualModeSelected = isManualMode;
    }

    ui->group_manual->setEnabled(isManualMode && !emergencyStopActive);
    updateStatusDisplay();
}

// 用法：刷新位置、清晰度和运行状态标签。
void MainWindow::updateStatusDisplay()
{
    ui->label_position->setText(QString("%1 脉冲（%2 圈）")
                                    .arg(currentPosition)
                                    .arg(static_cast<double>(currentPosition) /
                                             DemoWindowDetail::kPulsesPerTurn,
                                         0,
                                         'f',
                                         3));
    ui->label_sharpness->setText(cameraOnline && frameFormatValid
                                     ? QString::number(currentSharpness, 'f', 2)
                                     : "--");

    QStringList statusParts;
    statusParts << (ui->radio_manual->isChecked() ? "手动模式" : "自动模式");

    if (emergencyStopActive)
    {
        statusParts << "已触发急停";
    }

    statusParts << (serialConnected ? "串口已连接" : "串口未连接");

    if (serialConnected)
    {
        statusParts << motorRunState;
        statusParts << (motorEnabled ? "电机已使能" : "电机未使能");
        statusParts << (motorInPosition ? "已到位" : "未到位");
    }

    if (ui->radio_auto->isChecked())
    {
        if (!autoFocusBlockReason.isEmpty())
        {
            statusParts << autoFocusBlockReason;
        }
        else if (autoFocusFinished)
        {
            statusParts << "自动对焦完成";
        }
        else if (autoFocusMovePending)
        {
            statusParts << "自动对焦移动中";
        }
        else if (!autoFocusPositions.empty())
        {
            statusParts << QString("自动对焦采样 %1").arg(autoFocusPositions.size());
        }
    }

    if (!cameraOnline)
    {
        statusParts << "相机未连接";
    }
    else if (!frameFormatValid)
    {
        statusParts << "图像帧格式错误";
    }

    const QString statusText = statusParts.join(" | ");
    ui->label_status->setText(statusText);
    ui->label_status->setToolTip(statusText);
    ui->statusBar->showMessage(statusText);
    updateModernDashboard();
}

// 用法：同步顶部状态条、监控摘要和清晰度曲线标记。
void MainWindow::updateModernDashboard()
{
    if (labelModeBadge == nullptr)
    {
        return;
    }

    const bool isManualMode = ui->radio_manual->isChecked();
    setStatusBadge(labelModeBadge,
                   isManualMode ? QStringLiteral("手动模式") : QStringLiteral("自动模式"),
                   isManualMode ? QStringLiteral("neutral") : QStringLiteral("active"));

    if (!cameraOnline)
    {
        setStatusBadge(labelCameraBadge, QStringLiteral("相机未连接"), QStringLiteral("warn"));
    }
    else if (!frameFormatValid)
    {
        setStatusBadge(labelCameraBadge, QStringLiteral("图像格式错误"), QStringLiteral("error"));
    }
    else
    {
        setStatusBadge(labelCameraBadge, QStringLiteral("相机在线"), QStringLiteral("ok"));
    }

    setStatusBadge(labelSerialBadge,
                   serialConnected ? QStringLiteral("串口已连接") : QStringLiteral("串口未连接"),
                   serialConnected ? QStringLiteral("ok") : QStringLiteral("warn"));

    if (!serialConnected)
    {
        setStatusBadge(labelMotorBadge, QStringLiteral("电机未连接"), QStringLiteral("neutral"));
    }
    else if (!motorEnabled)
    {
        setStatusBadge(labelMotorBadge, QStringLiteral("电机未使能"), QStringLiteral("warn"));
    }
    else if (motorRunState.contains(QStringLiteral("运行")))
    {
        setStatusBadge(labelMotorBadge, QStringLiteral("电机运行中"), QStringLiteral("active"));
    }
    else if (motorInPosition)
    {
        setStatusBadge(labelMotorBadge, QStringLiteral("电机已到位"), QStringLiteral("ok"));
    }
    else
    {
        setStatusBadge(labelMotorBadge, QStringLiteral("电机已使能"), QStringLiteral("active"));
    }

    setStatusBadge(labelEmergencyBadge,
                   emergencyStopActive ? QStringLiteral("急停触发") : QStringLiteral("急停正常"),
                   emergencyStopActive ? QStringLiteral("error") : QStringLiteral("ok"));

    setStatusBadge(labelFocusStageBadge, focusStageText(), focusStageBadgeState());

    if (labelMetricSharpness != nullptr)
    {
        labelMetricSharpness->setText(cameraOnline && frameFormatValid
                                          ? QString::number(currentSharpness, 'f', 2)
                                          : QStringLiteral("--"));
    }

    if (labelMetricPosition != nullptr)
    {
        labelMetricPosition->setText(QStringLiteral("%1 脉冲").arg(currentPosition));
        labelMetricPosition->setToolTip(QStringLiteral("%1 圈")
                                            .arg(static_cast<double>(currentPosition) /
                                                     DemoWindowDetail::kPulsesPerTurn,
                                                 0,
                                                 'f',
                                                 3));
    }

    if (labelMetricBest != nullptr)
    {
        if (autoFocusSharpnessValues.empty())
        {
            labelMetricBest->setText(QStringLiteral("--"));
        }
        else
        {
            const auto bestIt = std::max_element(autoFocusSharpnessValues.cbegin(),
                                                 autoFocusSharpnessValues.cend());
            const size_t bestIndex = static_cast<size_t>(
                std::distance(autoFocusSharpnessValues.cbegin(), bestIt));
            const double bestPosition = bestIndex < autoFocusPositions.size()
                                            ? autoFocusPositions[bestIndex]
                                            : 0.0;
            labelMetricBest->setText(QStringLiteral("%1 @ %2")
                                         .arg(*bestIt, 0, 'f', 2)
                                         .arg(bestPosition, 0, 'f', 0));
        }
    }

    if (labelMetricTarget != nullptr)
    {
        if (autoFocusHasFinalTarget)
        {
            labelMetricTarget->setText(QStringLiteral("%1 脉冲")
                                           .arg(autoFocusFinalTargetPosition, 0, 'f', 0));
        }
        else if (autoFocusHasEstimatedPosition)
        {
            labelMetricTarget->setText(QStringLiteral("估计 %1")
                                           .arg(autoFocusEstimatedPosition, 0, 'f', 0));
        }
        else
        {
            labelMetricTarget->setText(QStringLiteral("--"));
        }
    }

    if (labelMetricSamples != nullptr)
    {
        labelMetricSamples->setText(QString::number(autoFocusPositions.size()));
    }

    if (progressFocusStage != nullptr)
    {
        int progress = 0;
        if (ui->radio_auto->isChecked())
        {
            progress = 10;
            if (!autoFocusPositions.empty())
            {
                progress = std::min(50, 10 + static_cast<int>(autoFocusPositions.size()) * 5);
            }
            if (autoFocusFineScanActive)
            {
                progress = std::max(progress, 55);
            }
            if (autoFocusPeakConfirmed)
            {
                progress = std::max(progress, 70);
            }
            if (autoFocusFinalMoveSent || autoFocusHasFinalTarget)
            {
                progress = std::max(progress, 85);
            }
            if (autoFocusFinished)
            {
                progress = 100;
            }
            if (!autoFocusBlockReason.isEmpty())
            {
                progress = 0;
            }
        }

        progressFocusStage->setValue(progress);
        progressFocusStage->setFormat(focusStageText());
    }

    if (sharpnessTrendWidget != nullptr)
    {
        sharpnessTrendWidget->setTargetMarkers(autoFocusHasFinalTarget,
                                               autoFocusFinalTargetPosition,
                                               autoFocusHasEstimatedPosition,
                                               autoFocusEstimatedPosition);
    }
}

// 用法：返回当前自动对焦阶段描述。
QString MainWindow::focusStageText() const
{
    if (emergencyStopActive)
    {
        return QStringLiteral("急停中");
    }

    if (ui->radio_manual->isChecked())
    {
        return QStringLiteral("手动待命");
    }

    if (!cameraOnline)
    {
        return QStringLiteral("等待相机");
    }

    if (!frameFormatValid)
    {
        return QStringLiteral("图像异常");
    }

    if (!autoFocusBlockReason.isEmpty())
    {
        return QStringLiteral("对焦阻塞");
    }

    if (autoFocusFinished)
    {
        return QStringLiteral("对焦完成");
    }

    if (autoFocusFinalMoveSent || autoFocusHasFinalTarget)
    {
        return QStringLiteral("最终回焦");
    }

    if (autoFocusPeakConfirmed)
    {
        return QStringLiteral("峰值确认");
    }

    if (autoFocusFineScanActive)
    {
        return QStringLiteral("小步扫描");
    }

    if (autoFocusMovePending)
    {
        return QStringLiteral("移动中");
    }

    if (!autoFocusPositions.empty())
    {
        return QStringLiteral("采样中 %1").arg(autoFocusPositions.size());
    }

    return serialConnected ? QStringLiteral("等待采样") : QStringLiteral("等待串口");
}

// 用法：返回当前自动对焦阶段对应的 UI 状态色。
QString MainWindow::focusStageBadgeState() const
{
    if (emergencyStopActive)
    {
        return QStringLiteral("error");
    }

    if (ui->radio_manual->isChecked())
    {
        return QStringLiteral("neutral");
    }

    if (!frameFormatValid && cameraOnline)
    {
        return QStringLiteral("error");
    }

    if (!cameraOnline || !serialConnected || !autoFocusBlockReason.isEmpty())
    {
        return QStringLiteral("warn");
    }

    if (autoFocusFinished)
    {
        return QStringLiteral("ok");
    }

    if (ui->radio_auto->isChecked() &&
        (autoFocusMovePending || !autoFocusPositions.empty() || autoFocusHasFinalTarget))
    {
        return QStringLiteral("active");
    }

    return QStringLiteral("neutral");
}

// 用法：刷新状态徽标文字和颜色。
void MainWindow::setStatusBadge(QLabel* label, const QString& text, const QString& state)
{
    if (label == nullptr)
    {
        return;
    }

    label->setText(text);
    label->setProperty("badgeState", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}

// 用法：根据日志内容和显式等级确定最终显示等级。
MainWindow::LogLevel MainWindow::effectiveLogLevel(const QString& message, LogLevel level) const
{
    if (level != LogLevel::Info)
    {
        return level;
    }

    if (message.contains(QStringLiteral("失败")) ||
        message.contains(QStringLiteral("错误")) ||
        message.contains(QStringLiteral("终止")) ||
        message.contains(QStringLiteral("中断")) ||
        message.contains(QStringLiteral("不支持")))
    {
        return LogLevel::Error;
    }

    if (message.contains(QStringLiteral("急停")) ||
        message.contains(QStringLiteral("未检测到")) ||
        message.contains(QStringLiteral("未连接")) ||
        message.contains(QStringLiteral("无效")) ||
        message.contains(QStringLiteral("忽略")) ||
        message.contains(QStringLiteral("低于")) ||
        message.contains(QStringLiteral("限制")))
    {
        return LogLevel::Warning;
    }

    if (message.contains(QStringLiteral("完成")) ||
        message.contains(QStringLiteral("已打开")) ||
        message.contains(QStringLiteral("已切换")) ||
        message.contains(QStringLiteral("已清除")) ||
        message.contains(QStringLiteral("已重新")) ||
        message.contains(QStringLiteral("已恢复")) ||
        message.contains(QStringLiteral("已发送")) ||
        message.contains(QStringLiteral("已使能")) ||
        message.contains(QStringLiteral("可以进行")))
    {
        return LogLevel::Success;
    }

    return LogLevel::Info;
}

// 用法：向日志框追加带时间戳的中文日志。
void MainWindow::appendLog(const QString& message, LogLevel level)
{
    const LogLevel displayLevel = effectiveLogLevel(message, level);
    QString tag = QStringLiteral("信息");
    switch (displayLevel)
    {
    case LogLevel::Success:
        tag = QStringLiteral("成功");
        break;
    case LogLevel::Warning:
        tag = QStringLiteral("警告");
        break;
    case LogLevel::Error:
        tag = QStringLiteral("错误");
        break;
    case LogLevel::Info:
        break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->text_log->appendPlainText(QStringLiteral("[%1] [%2] %3")
                                      .arg(timestamp)
                                      .arg(tag)
                                      .arg(message));
}
