#include "SerialPort.h"

#include <QElapsedTimer>
#include <QSerialPortInfo>

namespace SerialPortFrame
{
constexpr char kFrameHeader = static_cast<char>(0xC5);
constexpr char kFrameTail = static_cast<char>(0x5C);
}

// 用法：刷新并返回当前系统可用串口名称列表。
QStringList MotorSerialPort::availablePorts() const
{
    QStringList ports;
    const auto serialPorts = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : serialPorts)
    {
        ports.append(info.portName());
    }

    ports.sort(Qt::CaseInsensitive);
    return ports;
}

// 用法：按端口名和波特率打开串口，失败时通过 errorMessage 返回错误信息。
bool MotorSerialPort::open(const QString& portName, qint32 baudRate, QString& errorMessage)
{
    if (serial_.isOpen())
    {
        serial_.close();
    }

    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite))
    {
        errorMessage = serial_.errorString();
        return false;
    }

    clearInputBuffer();
    return true;
}

// 用法：关闭当前串口连接，重复调用是安全的。
void MotorSerialPort::close()
{
    if (serial_.isOpen())
    {
        serial_.close();
    }
}

// 用法：查询当前串口是否处于打开状态。
bool MotorSerialPort::isOpen() const
{
    return serial_.isOpen();
}

// 用法：设置驱动器从机地址，后续命令都会使用该地址。
void MotorSerialPort::setDeviceAddress(quint8 address)
{
    deviceAddress_ = address;
}

// 用法：读取当前驱动器从机地址。
quint8 MotorSerialPort::deviceAddress() const
{
    return deviceAddress_;
}

// 用法：将驱动器切换到通信位置模式，自动/手动相对移动前调用。
MotorSerialPort::Result MotorSerialPort::setCommunicationPositionMode()
{
    QByteArray responseData;
    return transact(0x62, QByteArray(1, static_cast<char>(0x00)), 2, responseData);
}

// 用法：清除驱动器报警或急停后的状态。
MotorSerialPort::Result MotorSerialPort::clearState()
{
    QByteArray responseData;
    return transact(0xFB, QByteArray(), 1, responseData);
}

// 用法：使能或失能电机，自动对焦发送移动指令前会确保已使能。
MotorSerialPort::Result MotorSerialPort::enableMotor(bool enable)
{
    QByteArray responseData;
    const Result result = transact(0xFA,
                                   QByteArray(1, static_cast<char>(enable ? 0x00 : 0x01)),
                                   2,
                                   responseData);
    if (!result.success)
    {
        return result;
    }

    return { true, enable ? "电机已使能。" : "电机已失能。" };
}

// 用法：向驱动器发送立即停止命令。
MotorSerialPort::Result MotorSerialPort::stopMotor()
{
    QByteArray responseData;
    return transact(0xFC, QByteArray(), 1, responseData);
}

// 用法：按方向、速度、加速度和脉冲数发送相对位置移动命令。
MotorSerialPort::Result MotorSerialPort::moveRelative(bool forward, quint16 speedRpm, quint8 acceleration, quint32 position)
{
    if (speedRpm == 0 || speedRpm > 6000)
    {
        return { false, "相对位置模式的速度超出允许范围。" };
    }

    if (acceleration > 200)
    {
        return { false, "相对位置模式的加减速度超出允许范围。" };
    }

    if (position == 0)
    {
        return { false, "相对位置模式的位移量不能为 0。" };
    }

    QByteArray requestData;
    requestData.reserve(8);
    requestData.append(static_cast<char>(forward ? 0x00 : 0x01));
    requestData.append(static_cast<char>(acceleration));
    requestData.append(static_cast<char>((speedRpm >> 8) & 0xFF));
    requestData.append(static_cast<char>(speedRpm & 0xFF));
    requestData.append(static_cast<char>((position >> 24) & 0xFF));
    requestData.append(static_cast<char>((position >> 16) & 0xFF));
    requestData.append(static_cast<char>((position >> 8) & 0xFF));
    requestData.append(static_cast<char>(position & 0xFF));

    QByteArray responseData;
    return transact(0xF3, requestData, 1, responseData);
}

// 用法：读取驱动器当前位置脉冲数。
MotorSerialPort::Result MotorSerialPort::readPosition(qint32& position)
{
    QByteArray responseData;
    const Result result = transact(0x2A, QByteArray(), 5, responseData);
    if (!result.success)
    {
        return result;
    }

    position = readInt32(responseData, 1);
    return { true, "读取位置成功。" };
}

// 用法：读取驱动器运行状态码。
MotorSerialPort::Result MotorSerialPort::readRunState(quint8& runState)
{
    QByteArray responseData;
    const Result result = transact(0x2C, QByteArray(), 2, responseData);
    if (!result.success)
    {
        return result;
    }

    runState = static_cast<quint8>(responseData.at(1));
    return { true, "读取运行状态成功。" };
}

// 用法：读取驱动器使能状态，状态协议不可靠时上层不应仅凭该值阻塞移动。
MotorSerialPort::Result MotorSerialPort::readEnableState(bool& enabled)
{
    QByteArray responseData;
    const Result result = transact(0x2F, QByteArray(), 2, responseData);
    if (!result.success)
    {
        return result;
    }

    enabled = responseData.at(1) == 0x01;
    return { true, "读取使能状态成功。" };
}

// 用法：读取驱动器到位状态。
MotorSerialPort::Result MotorSerialPort::readInPosition(bool& inPosition)
{
    QByteArray responseData;
    const Result result = transact(0x30, QByteArray(), 2, responseData);
    if (!result.success)
    {
        return result;
    }

    inPosition = responseData.at(1) == 0x01;
    return { true, "读取到位状态成功。" };
}

// 用法：一次性读取位置、运行状态、使能状态和到位状态。
MotorSerialPort::Result MotorSerialPort::readSnapshot(MotorSnapshot& snapshot)
{
    Result result = readPosition(snapshot.position);
    if (!result.success)
    {
        return result;
    }

    result = readRunState(snapshot.runState);
    if (!result.success)
    {
        return result;
    }

    result = readEnableState(snapshot.enabled);
    if (!result.success)
    {
        return result;
    }

    result = readInPosition(snapshot.inPosition);
    if (!result.success)
    {
        return result;
    }

    return { true, "读取电机状态成功。" };
}

// 用法：把驱动器运行状态码转换为界面显示文本。
QString MotorSerialPort::runStateText(quint8 runState)
{
    switch (runState)
    {
    case 0:
        return "停止状态";
    case 1:
        return "任务完成";
    case 2:
        return "正在运行";
    case 3:
        return "过载状态";
    case 4:
        return "堵转状态";
    case 5:
        return "欠压状态";
    default:
        return "未知状态";
    }
}

// 用法：发送一帧命令并读取校验应答，是所有驱动器命令的底层通讯入口。
MotorSerialPort::Result MotorSerialPort::transact(quint8 commandCode,
                                                  const QByteArray& requestData,
                                                  int expectedDataBytes,
                                                  QByteArray& responseData)
{
    if (!serial_.isOpen())
    {
        return { false, "串口尚未打开。" };
    }

    clearInputBuffer();

    const QByteArray requestFrame = buildFrame(commandCode, requestData);
    if (serial_.write(requestFrame) != requestFrame.size())
    {
        return { false, QString("串口写入失败：%1").arg(serial_.errorString()) };
    }

    if (!serial_.waitForBytesWritten(timeoutMs_))
    {
        return { false, QString("等待串口发送完成超时：%1").arg(serial_.errorString()) };
    }

    const int expectedFrameBytes = expectedDataBytes + 5;
    QByteArray responseFrame;
    QElapsedTimer timer;
    timer.start();

    while (responseFrame.size() < expectedFrameBytes)
    {
        const int remainingTime = timeoutMs_ - static_cast<int>(timer.elapsed());
        if (remainingTime <= 0)
        {
            return { false, "等待驱动器应答超时。" };
        }

        if (!serial_.waitForReadyRead(remainingTime))
        {
            return { false, QString("等待驱动器应答失败：%1").arg(serial_.errorString()) };
        }

        responseFrame.append(serial_.readAll());
        const int headerIndex = responseFrame.indexOf(SerialPortFrame::kFrameHeader);
        if (headerIndex > 0)
        {
            responseFrame.remove(0, headerIndex);
        }
    }

    responseFrame = responseFrame.left(expectedFrameBytes);
    if (responseFrame.size() != expectedFrameBytes)
    {
        return { false, "驱动器应答长度异常。" };
    }

    if (responseFrame.at(0) != SerialPortFrame::kFrameHeader)
    {
        return { false, "驱动器应答帧头错误。" };
    }

    if (responseFrame.at(expectedFrameBytes - 1) != SerialPortFrame::kFrameTail)
    {
        return { false, "驱动器应答帧尾错误。" };
    }

    if (static_cast<quint8>(responseFrame.at(1)) != deviceAddress_)
    {
        return { false, "驱动器地址与请求地址不匹配。" };
    }

    if (static_cast<quint8>(responseFrame.at(2)) != commandCode)
    {
        return { false, "驱动器应答功能码与请求不匹配。" };
    }

    const quint8 expectedChecksum = calculateChecksum(responseFrame.left(expectedFrameBytes - 2));
    const quint8 actualChecksum = static_cast<quint8>(responseFrame.at(expectedFrameBytes - 2));
    if (expectedChecksum != actualChecksum)
    {
        return { false, "驱动器应答校验和错误。" };
    }

    responseData = responseFrame.mid(3, expectedDataBytes);
    if (responseData.isEmpty())
    {
        return { false, "驱动器应答中缺少数据域。" };
    }

    const quint8 errorCode = static_cast<quint8>(responseData.at(0));
    if (errorCode != 0x01)
    {
        return { false, errorCodeText(errorCode) };
    }

    return { true, "通讯成功。" };
}

// 用法：按自定义协议封装命令帧。
QByteArray MotorSerialPort::buildFrame(quint8 commandCode, const QByteArray& requestData) const
{
    QByteArray frame;
    frame.reserve(requestData.size() + 5);
    frame.append(SerialPortFrame::kFrameHeader);
    frame.append(static_cast<char>(deviceAddress_));
    frame.append(static_cast<char>(commandCode));
    frame.append(requestData);
    frame.append(static_cast<char>(calculateChecksum(frame)));
    frame.append(SerialPortFrame::kFrameTail);
    return frame;
}

// 用法：计算协议帧校验和。
quint8 MotorSerialPort::calculateChecksum(const QByteArray& bytes) const
{
    quint32 checksum = 0;
    for (char byteValue : bytes)
    {
        checksum += static_cast<quint8>(byteValue);
    }

    return static_cast<quint8>(checksum & 0xFF);
}

// 用法：发送新命令前清空串口输入缓冲，避免读到上一帧残留数据。
void MotorSerialPort::clearInputBuffer()
{
    serial_.clear(QSerialPort::Input);
    serial_.readAll();
}

// 用法：把驱动器错误码转换为中文错误说明。
QString MotorSerialPort::errorCodeText(quint8 errorCode)
{
    switch (errorCode)
    {
    case 0x01:
        return "驱动器应答成功。";
    case 0xE1:
        return "驱动器提示帧长度不足。";
    case 0xE2:
        return "驱动器提示帧头错误。";
    case 0xE3:
        return "驱动器提示帧尾错误。";
    case 0xE4:
        return "驱动器提示校验和错误。";
    case 0xE5:
        return "驱动器提示功能码不支持。";
    case 0xE6:
        return "驱动器提示数据不合法。";
    default:
        return QString("驱动器返回未知错误码：0x%1。").arg(errorCode, 2, 16, QLatin1Char('0')).toUpper();
    }
}

// 用法：从大端字节数组中读取 16 位无符号整数。
quint16 MotorSerialPort::readUInt16(const QByteArray& data, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8) |
                                static_cast<quint8>(data.at(offset + 1)));
}

// 用法：从大端字节数组中读取 32 位有符号整数。
qint32 MotorSerialPort::readInt32(const QByteArray& data, int offset)
{
    return (static_cast<qint32>(static_cast<quint8>(data.at(offset))) << 24) |
           (static_cast<qint32>(static_cast<quint8>(data.at(offset + 1))) << 16) |
           (static_cast<qint32>(static_cast<quint8>(data.at(offset + 2))) << 8) |
           static_cast<qint32>(static_cast<quint8>(data.at(offset + 3)));
}

