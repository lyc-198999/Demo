#ifndef DEMO_SERIAL_PORT_H
#define DEMO_SERIAL_PORT_H

#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QStringList>

class MotorSerialPort
{
public:
    struct Result
    {
        bool success = false;
        QString message;
    };

    struct MotorSnapshot
    {
        qint32 position = 0;
        quint8 runState = 0;
        bool enabled = false;
        bool inPosition = false;
    };

    // 用法：获取当前系统可用串口列表。
    QStringList availablePorts() const;
    // 用法：打开指定串口，失败时返回中文错误信息。
    bool open(const QString& portName, qint32 baudRate, QString& errorMessage);
    // 用法：关闭串口连接。
    void close();
    // 用法：检查串口是否已打开。
    bool isOpen() const;

    // 用法：设置驱动器从机地址。
    void setDeviceAddress(quint8 address);
    // 用法：读取当前驱动器从机地址。
    quint8 deviceAddress() const;

    // 用法：切换到通信位置模式。
    Result setCommunicationPositionMode();
    // 用法：清除驱动器状态。
    Result clearState();
    // 用法：使能或失能电机。
    Result enableMotor(bool enable);
    // 用法：立即停止电机。
    Result stopMotor();
    // 用法：发送相对位置移动指令。
    Result moveRelative(bool forward, quint16 speedRpm, quint8 acceleration, quint32 position);

    // 用法：读取当前位置。
    Result readPosition(qint32& position);
    // 用法：读取运行状态。
    Result readRunState(quint8& runState);
    // 用法：读取使能状态。
    Result readEnableState(bool& enabled);
    // 用法：读取到位状态。
    Result readInPosition(bool& inPosition);
    // 用法：读取电机状态快照。
    Result readSnapshot(MotorSnapshot& snapshot);

    // 用法：将运行状态码转换为中文文本。
    static QString runStateText(quint8 runState);

private:
    // 用法：发送命令帧并读取应答帧。
    Result transact(quint8 commandCode, const QByteArray& requestData, int expectedDataBytes, QByteArray& responseData);
    // 用法：构造一帧自定义串口协议数据。
    QByteArray buildFrame(quint8 commandCode, const QByteArray& requestData) const;
    // 用法：计算协议校验和。
    quint8 calculateChecksum(const QByteArray& bytes) const;
    // 用法：清空串口输入缓冲区。
    void clearInputBuffer();

    // 用法：将驱动器错误码转换为中文文本。
    static QString errorCodeText(quint8 errorCode);
    // 用法：按大端格式读取 16 位整数。
    static quint16 readUInt16(const QByteArray& data, int offset);
    // 用法：按大端格式读取 32 位整数。
    static qint32 readInt32(const QByteArray& data, int offset);

    QSerialPort serial_;
    quint8 deviceAddress_ = 0x01;
    int timeoutMs_ = 300;
};

#endif

