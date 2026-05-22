#include "Demo.h"
#include "ui_Demo.h"

#include <QTimer>

// 用法：刷新串口下拉框中的可用端口。
void MainWindow::refreshSerialControls()
{
    const QString currentPort = ui->combo_serial->currentText();
    const QStringList ports = motorSerial.availablePorts();

    ui->combo_serial->clear();
    ui->combo_serial->addItems(ports);

    const int previousIndex = ui->combo_serial->findText(currentPort);
    if (previousIndex >= 0)
    {
        ui->combo_serial->setCurrentIndex(previousIndex);
    }
    else if (!ports.isEmpty())
    {
        ui->combo_serial->setCurrentIndex(0);
    }

    if (ports.isEmpty())
    {
        appendLog("未检测到可用串口。");
    }
    else
    {
        appendLog(QString("串口列表已刷新，检测到 %1 个串口。").arg(ports.size()));
    }
}

// 用法：处理连接/断开按钮，完成驱动器初始化和电机使能。
void MainWindow::toggleSerialConnection()
{
    if (!ui->btn_connect->isChecked())
    {
        if (motorSerial.isOpen())
        {
            motorSerial.close();
        }

        serialConnected = false;
        motorEnabled = false;
        motorInPosition = false;
        resetAutoFocusState();
        motorRunState = "未连接";
        currentPosition = 0;
        ui->btn_connect->setText("连接");
        appendLog("串口已断开。");
        updateStatusDisplay();
        return;
    }

    const QString portName = ui->combo_serial->currentText();
    const qint32 baudRate = ui->combo_baud->currentText().toInt();
    if (portName.isEmpty() || baudRate <= 0)
    {
        ui->btn_connect->setChecked(false);
        appendLog("串口连接失败：端口号或波特率无效。");
        return;
    }

    QString errorMessage;
    if (!motorSerial.open(portName, baudRate, errorMessage))
    {
        ui->btn_connect->setChecked(false);
        ui->btn_connect->setText("连接");
        serialConnected = false;
        appendLog(QString("串口打开失败：%1").arg(errorMessage));
        updateStatusDisplay();
        return;
    }

    appendLog(QString("串口已打开：%1，波特率 %2，从机地址默认使用 1。")
                  .arg(portName)
                  .arg(baudRate));

    MotorSerialPort::Result result = motorSerial.setCommunicationPositionMode();
    if (!result.success)
    {
        appendLog(QString("切换通信位置模式失败：%1").arg(result.message));
        motorSerial.close();
        ui->btn_connect->setChecked(false);
        ui->btn_connect->setText("连接");
        serialConnected = false;
        updateStatusDisplay();
        return;
    }
    appendLog("已切换到通信位置模式。");

    result = motorSerial.clearState();
    if (!result.success)
    {
        appendLog(QString("清除驱动器状态失败：%1").arg(result.message));
        motorSerial.close();
        ui->btn_connect->setChecked(false);
        ui->btn_connect->setText("连接");
        serialConnected = false;
        updateStatusDisplay();
        return;
    }
    appendLog("已清除驱动器状态。");

    result = motorSerial.enableMotor(true);
    if (!result.success)
    {
        appendLog(QString("电机使能失败：%1").arg(result.message));
        motorSerial.close();
        ui->btn_connect->setChecked(false);
        ui->btn_connect->setText("连接");
        serialConnected = false;
        updateStatusDisplay();
        return;
    }

    serialConnected = true;
    motorEnabled = true;
    ui->btn_connect->setText("断开");
    appendLog("电机已使能，可以进行手动移动。");
    resetAutoFocusState();
    synchronizeMotorStatus();
}

// 用法：手动模式下按默认步数向前移动。
void MainWindow::handleManualForward()
{
    handleManualMove(static_cast<double>(manualStep));
}

// 用法：手动模式下按默认步数向后移动。
void MainWindow::handleManualBackward()
{
    handleManualMove(-static_cast<double>(manualStep));
}

// 用法：处理急停按钮，发送停止、清状态和重新使能命令。
void MainWindow::toggleEmergencyStop(bool active)
{
    emergencyStopActive = active;
    ui->btn_emergency_stop->setText(active ? "恢复" : "急停");

    if (active)
    {
        appendLog("已触发急停。");
        resetAutoFocusState();
        if (serialConnected)
        {
            const MotorSerialPort::Result result = motorSerial.stopMotor();
            if (result.success)
            {
                appendLog("已向驱动器发送立即停止命令。");
            }
            else
            {
                appendLog(QString("发送急停命令失败：%1").arg(result.message));
            }

            synchronizeMotorStatus(false);
        }
    }
    else
    {
        appendLog("急停已解除。");
        if (serialConnected)
        {
            MotorSerialPort::Result result = motorSerial.clearState();
            if (!result.success)
            {
                appendLog(QString("解除急停后清状态失败：%1").arg(result.message));
            }
            else
            {
                appendLog("已清除刹车/失能状态。");
            }

            result = motorSerial.enableMotor(true);
            if (!result.success)
            {
                appendLog(QString("解除急停后重新使能失败：%1").arg(result.message));
            }
            else
            {
                appendLog("已重新使能电机。");
                motorEnabled = true;
            }

            synchronizeMotorStatus(false);
        }
    }

    updateManualControlState();
}

// 用法：读取驱动器快照并同步界面状态。
void MainWindow::synchronizeMotorStatus(bool logOnFailure)
{
    if (!serialConnected)
    {
        return;
    }

    MotorSerialPort::MotorSnapshot snapshot;
    const MotorSerialPort::Result result = motorSerial.readSnapshot(snapshot);
    if (!result.success)
    {
        if (logOnFailure)
        {
            appendLog(QString("读取电机状态失败：%1").arg(result.message));
        }
        return;
    }

    currentPosition = snapshot.position;
    motorRunState = MotorSerialPort::runStateText(snapshot.runState);
    motorEnabled = snapshot.enabled;
    motorInPosition = snapshot.inPosition;
    updateStatusDisplay();
}

// 用法：手动模式下发送相对移动指令。
void MainWindow::handleManualMove(double delta)
{
    if (emergencyStopActive)
    {
        appendLog("当前处于急停状态，已忽略手动移动指令。");
        return;
    }

    if (!ui->radio_manual->isChecked())
    {
        appendLog("当前为自动模式，已忽略手动移动指令。");
        return;
    }

    if (!serialConnected)
    {
        appendLog("当前串口未连接，无法发送手动移动指令。");
        return;
    }

    const bool forward = delta > 0.0;
    const quint32 pulseCount = static_cast<quint32>(qAbs(delta));
    MotorSerialPort::Result result = motorSerial.moveRelative(forward,
                                                              manualSpeedRpm,
                                                              manualAcceleration,
                                                              pulseCount);
    if (!result.success)
    {
        appendLog(QString("发送手动移动指令失败：%1").arg(result.message));
        return;
    }

    motorRunState = "正在运行";
    motorInPosition = false;
    updateStatusDisplay();

    appendLog(QString("已发送手动%1指令：步数=%2，速度=%3RPM，加减速度=%4。")
                  .arg(forward ? "前进" : "后退")
                  .arg(pulseCount)
                  .arg(manualSpeedRpm)
                  .arg(manualAcceleration));

    QTimer::singleShot(150, this, [this]() { synchronizeMotorStatus(false); });
    QTimer::singleShot(600, this, [this]() { synchronizeMotorStatus(false); });
}
