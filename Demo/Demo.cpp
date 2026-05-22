#include "Demo.h"
#include "ui_Demo.h"

#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QTextDocument>
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

// 用法：调整右侧功能区内的控件顺序，让急停始终位于低频设置区域上方。
void ConfigureControlPanelOrder(Ui::DemoClass* ui)
{
    ui->verticalLayoutControlPanel->removeWidget(ui->btn_emergency_stop);

    const int paramsIndex = ui->verticalLayoutControlPanel->indexOf(ui->group_motor_params);
    if (paramsIndex >= 0)
    {
        ui->verticalLayoutControlPanel->insertWidget(paramsIndex, ui->btn_emergency_stop);
    }
    else
    {
        ui->verticalLayoutControlPanel->addWidget(ui->btn_emergency_stop);
    }
}

// 用法：把右侧功能区放进独立滚动区，避免展开低频设置时撑大主窗口或挤压日志。
void ConfigureControlPanelScrollArea(Ui::DemoClass* ui)
{
    ConfigureControlPanelOrder(ui);

    auto* controlPanelWidget = new QWidget(ui->centralWidget);
    controlPanelWidget->setObjectName("widget_control_panel");
    controlPanelWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    ui->gridLayoutRoot->removeItem(ui->verticalLayoutControlPanel);
    ui->verticalLayoutControlPanel->setParent(nullptr);
    controlPanelWidget->setLayout(ui->verticalLayoutControlPanel);

    auto* scrollArea = new QScrollArea(ui->centralWidget);
    scrollArea->setObjectName("scroll_control_panel");
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(kControlPanelMinimumWidth);
    scrollArea->setMaximumWidth(kControlPanelMaximumWidth);
    scrollArea->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    scrollArea->setWidget(controlPanelWidget);

    ui->gridLayoutRoot->addWidget(scrollArea, 0, 1, 1, 1);
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
    ui->gridLayoutRoot->setRowStretch(0, 2);
    ui->gridLayoutRoot->setRowStretch(1, 1);
}
}

// 用法：初始化界面控件、串口控件和相机刷新定时器。
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::DemoClass)
    , timer(new QTimer(this))
{
    ui->setupUi(this);
    DemoWindowDetail::ConfigureControlPanelScrollArea(ui);
    DemoWindowDetail::ConfigureMainLayout(ui);

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
}

// 用法：向日志框追加带时间戳的中文日志。
void MainWindow::appendLog(const QString& message)
{
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->text_log->appendPlainText(QString("[%1] %2").arg(timestamp).arg(message));
}
