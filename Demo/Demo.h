#ifndef DEMO_MAINWINDOW_H
#define DEMO_MAINWINDOW_H

#include "SerialPort.h"

#include <QImage>
#include <QMainWindow>
#include <QResizeEvent>
#include <QString>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui { class DemoClass; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 用法：创建主窗口并初始化图像、串口和控制状态。
    MainWindow(QWidget* parent = nullptr);
    // 用法：销毁主窗口并释放串口资源。
    ~MainWindow();

private slots:
    // 用法：定时采集图像并推进自动对焦。
    void updateFrame();
    // 用法：处理自动/手动模式切换。
    void updateManualControlState();
    // 用法：刷新串口列表。
    void refreshSerialControls();
    // 用法：处理串口连接或断开。
    void toggleSerialConnection();
    // 用法：手动前进一步。
    void handleManualForward();
    // 用法：手动后退一步。
    void handleManualBackward();
    // 用法：处理急停和恢复。
    void toggleEmergencyStop(bool active);

private:
    // 用法：窗口缩放时刷新图像显示。
    void resizeEvent(QResizeEvent* event) override;
    // 用法：显示当前相机帧。
    void showFrame(const cv::Mat& frame);
    // 用法：按控件大小刷新图像。
    void refreshDisplayedImage();
    // 用法：刷新界面状态文字。
    void updateStatusDisplay();
    // 用法：同步驱动器状态到界面。
    bool synchronizeMotorStatus(bool logOnFailure = true);
    // 用法：追加日志文本。
    void appendLog(const QString& message);
    // 用法：发送手动相对移动。
    void handleManualMove(double delta);
    // 用法：推进自动对焦状态机。
    void processAutoFocus();
    // 用法：重置自动对焦内部状态。
    void resetAutoFocusState(bool logReset = false);
    // 用法：追加自动对焦采样点。
    bool appendAutoFocusSample();
    // 用法：发送自动对焦移动。
    bool sendAutoFocusMove(double deltaPulses, const QString& reason);
    // 用法：记录自动对焦阻塞原因。
    void reportAutoFocusBlocked(const QString& reason);
    // 用法：清除自动对焦阻塞原因。
    void clearAutoFocusBlockReason();

    Ui::DemoClass* ui;
    QTimer* timer;
    MotorSerialPort motorSerial;

    cv::Mat frame;
    QImage currentImage;
    qint32 currentPosition = 0;
    double currentSharpness = 0.0;

    //手动模式电机参数
    quint32 manualStep = 3200;
    quint16 manualSpeedRpm = 800;
    quint8 manualAcceleration = 20;

    // 自动模式电机参数
    quint32 autoFocusScanStep = 3200;
    quint32 autoFocusFineStep = 800;
    quint16 autoFocusSpeedRpm = 500;
    quint8 autoFocusAcceleration = 15;

    bool cameraOnline = false;
    bool frameFormatValid = false;
    int consecutiveEmptyFrames = 0;
    bool serialConnected = false;
    bool emergencyStopActive = false;
    bool manualModeSelected = false;
    bool motorEnabled = false;
    bool motorInPosition = false;
    bool autoFocusMovePending = false;
    bool autoFocusFinished = false;
    bool autoFocusFinalMoveSent = false;
    bool autoFocusEnableRetryDone = false;
    bool autoFocusHasEstimatedPosition = false;
    bool autoFocusDirectionReversed = false;
    bool autoFocusPeakConfirmed = false;
    bool autoFocusFineScanActive = false;
    bool autoFocusHasFinalTarget = false;
    int autoFocusScanDirection = 1;
    int autoFocusFineProbeDirection = -1;
    double autoFocusEstimatedPosition = 0.0;
    double autoFocusFinalTargetPosition = 0.0;
    std::vector<double> autoFocusPositions;
    std::vector<double> autoFocusSharpnessValues;
    QString autoFocusBlockReason;
    QString motorRunState = "未连接";
};

#endif

