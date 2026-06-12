#include "Demo.h"
#include "ui_Demo.h"

#include <algorithm>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>
#include <QStyle>
#include <QTabWidget>
#include <QTextDocument>
#include <QUrl>
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
constexpr const char* kSettingsOrganization = "AutoFocusSystem";
constexpr const char* kSettingsApplication = "Demo";
constexpr const char* kSettingsDataDirectoryKey = "data/saveDirectory";
constexpr const char* kSettingsAutoSaveImageKey = "data/autoSaveImage";
constexpr const char* kSettingsAutoExportLogKey = "data/autoExportLog";

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

// 用法：返回默认图像和日志保存目录。
QString DefaultDataSaveDirectory()
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documentsPath.isEmpty())
    {
        documentsPath = QDir::homePath();
    }

    return QDir(documentsPath).filePath(QStringLiteral("AutoFocusData"));
}

// 用法：把原始 OpenCV 图像转换为可保存的 QImage。
QImage MatToSaveImage(const cv::Mat& mat)
{
    if (mat.empty())
    {
        return {};
    }

    switch (mat.type())
    {
    case CV_8UC1:
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      static_cast<qsizetype>(mat.step),
                      QImage::Format_Grayscale8).copy();

    case CV_16UC1:
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      static_cast<qsizetype>(mat.step),
                      QImage::Format_Grayscale16).copy();

    case CV_8UC3:
    {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data,
                      rgb.cols,
                      rgb.rows,
                      static_cast<qsizetype>(rgb.step),
                      QImage::Format_RGB888).copy();
    }

    case CV_8UC4:
    {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data,
                      rgba.cols,
                      rgba.rows,
                      static_cast<qsizetype>(rgba.step),
                      QImage::Format_RGBA8888).copy();
    }

    default:
        return {};
    }
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
    ui->group_status->setTitle(QStringLiteral("对焦结果"));
    ui->group_mode->setTitle(QStringLiteral("模式"));
    ui->group_manual->setTitle(QStringLiteral("手动控制"));
    ui->group_serial->setTitle(QStringLiteral("串口连接"));
    ui->group_motor_params->setTitle(QStringLiteral("电机参数"));

    DemoWindowDetail::ClearLayoutItems(ui->formLayoutStatus);
    ui->label_sharpnessTitle->hide();
    ui->label_sharpness->hide();
    ui->label_positionTitle->hide();
    ui->label_position->hide();
    ui->label_statusTitle->hide();
    ui->label_status->hide();

    labelFocusResultTitle = new QLabel(QStringLiteral("手动待命"), ui->group_status);
    labelFocusResultTitle->setAlignment(Qt::AlignCenter);
    labelFocusResultTitle->setWordWrap(true);
    labelFocusResultTitle->setProperty("role", "focusResultTitle");
    labelFocusResultTitle->setProperty("resultState", "neutral");

    labelFocusResultDetail = new QLabel(QStringLiteral("当前为手动模式，自动对焦未运行。"), ui->group_status);
    labelFocusResultDetail->setAlignment(Qt::AlignCenter);
    labelFocusResultDetail->setWordWrap(true);
    labelFocusResultDetail->setProperty("role", "focusResultDetail");

    progressFocusStage = new QProgressBar(ui->group_status);
    progressFocusStage->setRange(0, 100);
    progressFocusStage->setValue(0);
    progressFocusStage->setTextVisible(true);
    progressFocusStage->setFormat(QStringLiteral("手动待命"));

    const auto createFocusSummary = [this](const QString& text) {
        auto* label = new QLabel(text, ui->group_status);
        label->setWordWrap(true);
        label->setProperty("role", "focusSummary");
        return label;
    };

    labelFocusPositionSummary = createFocusSummary(QStringLiteral("当前位置：--"));
    labelFocusTargetSummary = createFocusSummary(QStringLiteral("目标位置：--"));
    labelFocusSampleSummary = createFocusSummary(QStringLiteral("采样数：0"));
    labelFocusSharpnessSummary = createFocusSummary(QStringLiteral("清晰度：完成后显示"));

    ui->formLayoutStatus->setContentsMargins(8, 8, 8, 8);
    ui->formLayoutStatus->setSpacing(8);
    ui->formLayoutStatus->addRow(labelFocusResultTitle);
    ui->formLayoutStatus->addRow(labelFocusResultDetail);
    ui->formLayoutStatus->addRow(progressFocusStage);
    ui->formLayoutStatus->addRow(labelFocusPositionSummary);
    ui->formLayoutStatus->addRow(labelFocusTargetSummary);
    ui->formLayoutStatus->addRow(labelFocusSampleSummary);
    ui->formLayoutStatus->addRow(labelFocusSharpnessSummary);

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
    initializeDataControls();

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
    auto* subtitleLabel = new QLabel(QStringLiteral("实时图像采集 / 电机控制 / 对焦结果反馈"), titleBlock);
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
    workspaceLayout->addWidget(ui->group_image, 1);

    ui->gridLayoutRoot->addWidget(topStatusBar, 0, 0, 1, 2);
    ui->gridLayoutRoot->addWidget(workspace, 1, 0, 1, 1);
    ui->gridLayoutRoot->addWidget(ui->group_log, 2, 0, 1, 2);

    DemoWindowDetail::ConfigureMainLayout(ui);
}

// 用法：创建数据保存页签和控件，不修改 Designer 生成的 .ui 文件。
void MainWindow::initializeDataControls()
{
    auto* tabs = ui->centralWidget->findChild<QTabWidget*>(QStringLiteral("tabs_control_panel"));
    if (tabs == nullptr)
    {
        return;
    }

    auto* dataPage = DemoWindowDetail::CreateTabPage(tabs);
    auto* dataGroup = new QGroupBox(QStringLiteral("数据管理"), dataPage);
    auto* groupLayout = new QVBoxLayout(dataGroup);
    groupLayout->setContentsMargins(10, 10, 10, 10);
    groupLayout->setSpacing(8);

    auto* directoryLayout = new QGridLayout();
    directoryLayout->setColumnStretch(1, 1);
    auto* directoryTitle = new QLabel(QStringLiteral("保存目录"), dataGroup);
    labelDataDirectory = new QLabel(dataGroup);
    labelDataDirectory->setWordWrap(true);
    labelDataDirectory->setProperty("role", "focusSummary");
    auto* buttonSelectDirectory = new QPushButton(QStringLiteral("选择目录"), dataGroup);
    directoryLayout->addWidget(directoryTitle, 0, 0);
    directoryLayout->addWidget(labelDataDirectory, 0, 1);
    directoryLayout->addWidget(buttonSelectDirectory, 1, 1);
    groupLayout->addLayout(directoryLayout);

    auto* actionLayout = new QHBoxLayout();
    auto* buttonSaveImage = new QPushButton(QStringLiteral("保存当前图像"), dataGroup);
    auto* buttonExportLog = new QPushButton(QStringLiteral("导出日志"), dataGroup);
    actionLayout->addWidget(buttonSaveImage);
    actionLayout->addWidget(buttonExportLog);
    groupLayout->addLayout(actionLayout);

    auto* buttonOpenDirectory = new QPushButton(QStringLiteral("打开保存目录"), dataGroup);
    groupLayout->addWidget(buttonOpenDirectory);

    checkAutoSaveImage = new QCheckBox(QStringLiteral("对焦完成后保存图像"), dataGroup);
    checkAutoExportLog = new QCheckBox(QStringLiteral("对焦完成后导出日志"), dataGroup);
    groupLayout->addWidget(checkAutoSaveImage);
    groupLayout->addWidget(checkAutoExportLog);

    connect(buttonSelectDirectory, &QPushButton::clicked, this, &MainWindow::selectDataSaveDirectory);
    connect(buttonSaveImage, &QPushButton::clicked, this, [this]() { saveCurrentImage(); });
    connect(buttonExportLog, &QPushButton::clicked, this, [this]() { exportLog(); });
    connect(buttonOpenDirectory, &QPushButton::clicked, this, &MainWindow::openDataSaveDirectory);
    connect(checkAutoSaveImage, &QCheckBox::toggled, this, [this](bool) { saveDataSettings(); });
    connect(checkAutoExportLog, &QCheckBox::toggled, this, [this](bool) { saveDataSettings(); });

    DemoWindowDetail::AddWidgetBeforeStretch(dataPage, dataGroup);
    tabs->addTab(dataPage, QStringLiteral("数据"));
    loadDataSettings();
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

// 用法：同步顶部状态条和右侧对焦结果卡。
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

    QString resultTitle = QStringLiteral("等待设备");
    QString resultDetail;
    QString resultState = QStringLiteral("warn");

    if (emergencyStopActive)
    {
        resultTitle = QStringLiteral("急停中");
        resultDetail = QStringLiteral("已停止移动，解除急停后可继续操作。");
        resultState = QStringLiteral("error");
    }
    else if (isManualMode)
    {
        resultTitle = QStringLiteral("手动待命");
        resultDetail = QStringLiteral("当前为手动模式，自动对焦未运行。");
        resultState = QStringLiteral("neutral");
    }
    else if (!cameraOnline || !serialConnected)
    {
        QStringList missingParts;
        if (!cameraOnline)
        {
            missingParts << QStringLiteral("相机未连接");
        }
        if (!serialConnected)
        {
            missingParts << QStringLiteral("串口未连接");
        }

        resultTitle = QStringLiteral("等待设备");
        resultDetail = missingParts.join(QStringLiteral("，"));
        resultState = QStringLiteral("warn");
    }
    else if (!frameFormatValid)
    {
        resultTitle = QStringLiteral("图像异常");
        resultDetail = QStringLiteral("当前图像帧格式无法用于对焦。");
        resultState = QStringLiteral("error");
    }
    else if (!autoFocusBlockReason.isEmpty())
    {
        resultTitle = QStringLiteral("对焦阻塞");
        resultDetail = autoFocusBlockReason;
        resultState = QStringLiteral("warn");
    }
    else if (autoFocusFinished)
    {
        resultTitle = QStringLiteral("对焦成功");
        resultDetail = QStringLiteral("已完成最终回焦和结果复核。");
        resultState = QStringLiteral("ok");
    }
    else if (autoFocusMovePending || !autoFocusPositions.empty() || autoFocusHasFinalTarget)
    {
        resultTitle = QStringLiteral("对焦中");
        resultDetail = focusStageText();
        resultState = QStringLiteral("active");
    }
    else
    {
        resultTitle = QStringLiteral("等待采样");
        resultDetail = QStringLiteral("设备已就绪，等待自动对焦采样。");
        resultState = QStringLiteral("neutral");
    }

    if (labelFocusResultTitle != nullptr)
    {
        labelFocusResultTitle->setText(resultTitle);
        labelFocusResultTitle->setProperty("resultState", resultState);
        labelFocusResultTitle->style()->unpolish(labelFocusResultTitle);
        labelFocusResultTitle->style()->polish(labelFocusResultTitle);
        labelFocusResultTitle->update();
    }

    if (labelFocusResultDetail != nullptr)
    {
        labelFocusResultDetail->setText(resultDetail);
    }

    if (labelFocusPositionSummary != nullptr)
    {
        labelFocusPositionSummary->setText(QStringLiteral("当前位置：%1 脉冲（%2 圈）")
                                               .arg(currentPosition)
                                               .arg(static_cast<double>(currentPosition) /
                                                        DemoWindowDetail::kPulsesPerTurn,
                                                    0,
                                                    'f',
                                                    3));
    }

    if (labelFocusTargetSummary != nullptr)
    {
        if (autoFocusHasFinalTarget)
        {
            labelFocusTargetSummary->setText(QStringLiteral("目标位置：%1 脉冲")
                                                 .arg(autoFocusFinalTargetPosition, 0, 'f', 0));
        }
        else if (autoFocusHasEstimatedPosition && !isManualMode)
        {
            labelFocusTargetSummary->setText(QStringLiteral("估计位置：%1 脉冲")
                                                 .arg(autoFocusEstimatedPosition, 0, 'f', 0));
        }
        else
        {
            labelFocusTargetSummary->setText(QStringLiteral("目标位置：--"));
        }
    }

    if (labelFocusSampleSummary != nullptr)
    {
        labelFocusSampleSummary->setText(QStringLiteral("采样数：%1")
                                             .arg(autoFocusPositions.size()));
    }

    if (labelFocusSharpnessSummary != nullptr)
    {
        if (isManualMode)
        {
            labelFocusSharpnessSummary->setText(cameraOnline && frameFormatValid
                                                    ? QStringLiteral("清晰度：当前 %1")
                                                          .arg(currentSharpness, 0, 'f', 2)
                                                    : QStringLiteral("清晰度：等待图像"));
        }
        else if (autoFocusFinished && !autoFocusSharpnessValues.empty())
        {
            const auto bestIt = std::max_element(autoFocusSharpnessValues.cbegin(),
                                                 autoFocusSharpnessValues.cend());
            labelFocusSharpnessSummary->setText(QStringLiteral("清晰度：最终 %1 / 峰值 %2")
                                                    .arg(currentSharpness, 0, 'f', 2)
                                                    .arg(*bestIt, 0, 'f', 2));
        }
        else if (autoFocusFinished)
        {
            labelFocusSharpnessSummary->setText(QStringLiteral("清晰度：最终 %1")
                                                    .arg(currentSharpness, 0, 'f', 2));
        }
        else
        {
            labelFocusSharpnessSummary->setText(QStringLiteral("清晰度：完成后显示"));
        }
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

// 用法：选择图像和日志保存目录。
void MainWindow::selectDataSaveDirectory()
{
    const QString startDirectory = dataSaveDirectory.isEmpty()
                                       ? DemoWindowDetail::DefaultDataSaveDirectory()
                                       : dataSaveDirectory;
    const QString selectedDirectory =
        QFileDialog::getExistingDirectory(this,
                                          QStringLiteral("选择保存目录"),
                                          startDirectory,
                                          QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selectedDirectory.isEmpty())
    {
        return;
    }

    dataSaveDirectory = selectedDirectory;
    if (labelDataDirectory != nullptr)
    {
        labelDataDirectory->setText(QDir::toNativeSeparators(dataSaveDirectory));
    }

    saveDataSettings();
    appendLog(QStringLiteral("数据保存目录已切换为：%1。")
                  .arg(QDir::toNativeSeparators(dataSaveDirectory)));
}

// 用法：保存当前原始相机帧为 PNG。
bool MainWindow::saveCurrentImage(const QString& filePrefix, bool logSuccess)
{
    if (frame.empty())
    {
        appendLog(QStringLiteral("当前无有效图像，无法保存。"), LogLevel::Warning);
        return false;
    }

    QImage saveImage = DemoWindowDetail::MatToSaveImage(frame);
    if (saveImage.isNull())
    {
        appendLog(QStringLiteral("当前图像格式不支持保存。"), LogLevel::Error);
        return false;
    }

    QDir directory(dataSaveDirectory.isEmpty()
                       ? DemoWindowDetail::DefaultDataSaveDirectory()
                       : dataSaveDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        appendLog(QStringLiteral("创建图像保存目录失败：%1。")
                      .arg(QDir::toNativeSeparators(directory.absolutePath())),
                  LogLevel::Error);
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString fileName = filePrefix == QStringLiteral("autofocus")
                                 ? QStringLiteral("autofocus_%1.png").arg(timestamp)
                                 : QStringLiteral("%1_%2.png").arg(filePrefix, timestamp);
    const QString filePath = directory.filePath(fileName);
    if (!saveImage.save(filePath, "PNG"))
    {
        appendLog(QStringLiteral("保存图像失败：%1。").arg(QDir::toNativeSeparators(filePath)),
                  LogLevel::Error);
        return false;
    }

    if (logSuccess)
    {
        appendLog(QStringLiteral("图像已保存：%1。").arg(QDir::toNativeSeparators(filePath)),
                  LogLevel::Success);
    }
    return true;
}

// 用法：导出当前事件日志为 UTF-8 文本。
bool MainWindow::exportLog(const QString& filePrefix, bool logSuccess)
{
    QDir directory(dataSaveDirectory.isEmpty()
                       ? DemoWindowDetail::DefaultDataSaveDirectory()
                       : dataSaveDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        appendLog(QStringLiteral("创建日志保存目录失败：%1。")
                      .arg(QDir::toNativeSeparators(directory.absolutePath())),
                  LogLevel::Error);
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString fileName = filePrefix == QStringLiteral("autofocus")
                                 ? QStringLiteral("autofocus_%1_log.txt").arg(timestamp)
                                 : QStringLiteral("%1_%2.txt").arg(filePrefix, timestamp);
    const QString filePath = directory.filePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        appendLog(QStringLiteral("导出日志失败：%1。").arg(file.errorString()), LogLevel::Error);
        return false;
    }

    const QByteArray content = ui->text_log->toPlainText().toUtf8();
    if (file.write(content) != content.size())
    {
        appendLog(QStringLiteral("写入日志文件失败：%1。").arg(file.errorString()), LogLevel::Error);
        return false;
    }

    if (logSuccess)
    {
        appendLog(QStringLiteral("日志已导出：%1。").arg(QDir::toNativeSeparators(filePath)),
                  LogLevel::Success);
    }
    return true;
}

// 用法：打开当前保存目录。
void MainWindow::openDataSaveDirectory()
{
    QDir directory(dataSaveDirectory.isEmpty()
                       ? DemoWindowDetail::DefaultDataSaveDirectory()
                       : dataSaveDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        appendLog(QStringLiteral("创建保存目录失败：%1。")
                      .arg(QDir::toNativeSeparators(directory.absolutePath())),
                  LogLevel::Error);
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath())))
    {
        appendLog(QStringLiteral("打开保存目录失败：%1。")
                      .arg(QDir::toNativeSeparators(directory.absolutePath())),
                  LogLevel::Error);
    }
}

// 用法：加载数据保存配置。
void MainWindow::loadDataSettings()
{
    QSettings settings(QString::fromLatin1(DemoWindowDetail::kSettingsOrganization),
                       QString::fromLatin1(DemoWindowDetail::kSettingsApplication));
    dataSaveDirectory = settings.value(QString::fromLatin1(DemoWindowDetail::kSettingsDataDirectoryKey),
                                       DemoWindowDetail::DefaultDataSaveDirectory())
                            .toString();

    if (labelDataDirectory != nullptr)
    {
        labelDataDirectory->setText(QDir::toNativeSeparators(dataSaveDirectory));
    }

    if (checkAutoSaveImage != nullptr)
    {
        checkAutoSaveImage->setChecked(settings.value(
                                             QString::fromLatin1(DemoWindowDetail::kSettingsAutoSaveImageKey),
                                             false)
                                             .toBool());
    }

    if (checkAutoExportLog != nullptr)
    {
        checkAutoExportLog->setChecked(settings.value(
                                             QString::fromLatin1(DemoWindowDetail::kSettingsAutoExportLogKey),
                                             false)
                                             .toBool());
    }
}

// 用法：保存数据保存配置。
void MainWindow::saveDataSettings()
{
    QSettings settings(QString::fromLatin1(DemoWindowDetail::kSettingsOrganization),
                       QString::fromLatin1(DemoWindowDetail::kSettingsApplication));
    settings.setValue(QString::fromLatin1(DemoWindowDetail::kSettingsDataDirectoryKey),
                      dataSaveDirectory.isEmpty()
                          ? DemoWindowDetail::DefaultDataSaveDirectory()
                          : dataSaveDirectory);

    if (checkAutoSaveImage != nullptr)
    {
        settings.setValue(QString::fromLatin1(DemoWindowDetail::kSettingsAutoSaveImageKey),
                          checkAutoSaveImage->isChecked());
    }

    if (checkAutoExportLog != nullptr)
    {
        settings.setValue(QString::fromLatin1(DemoWindowDetail::kSettingsAutoExportLogKey),
                          checkAutoExportLog->isChecked());
    }
}

// 用法：自动对焦成功后按设置保存图像和日志。
void MainWindow::handleAutoFocusArtifactsIfNeeded()
{
    if (autoFocusArtifactsSaved)
    {
        return;
    }

    const bool shouldSaveImage = checkAutoSaveImage != nullptr && checkAutoSaveImage->isChecked();
    const bool shouldExportLog = checkAutoExportLog != nullptr && checkAutoExportLog->isChecked();
    if (!shouldSaveImage && !shouldExportLog)
    {
        autoFocusArtifactsSaved = true;
        return;
    }

    if (shouldSaveImage)
    {
        saveCurrentImage(QStringLiteral("autofocus"));
    }

    if (shouldExportLog)
    {
        exportLog(QStringLiteral("autofocus"));
    }

    autoFocusArtifactsSaved = true;
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
