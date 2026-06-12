/*
测试用途：在当前焦面附近按均匀位移采集并导出图片数据。

注意：
1. 这个文件不要加入 AutoFocusSystem.vcxproj，否则会和主程序 main.cpp 产生重复入口。
2. 运行前先让系统处在焦面附近；程序会把启动时读取到的当前位置当作焦面中心。
3. 单独编译时需要同时编译/链接 MotorSerialPort.cpp、Sharpness.cpp、SpinnakerCamera.cpp，
   并链接 Qt Core、Qt SerialPort、OpenCV 和 Spinnaker。

命令行：
FocusImageExportTest.exe COM3 [output_dir] [count] [spacing_pulses] [baud] [speed_rpm] [acceleration] [settle_ms]

示例：
FocusImageExportTest.exe COM3 focus_export 7 400 115200 500 15 700
*/

#include "../AutoFocusSystem/MotorSerialPort.h"
#include "../AutoFocusSystem/Sharpness.h"

#include <QCoreApplication>
#include <QStringList>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// 用法：由 SpinnakerCamera.cpp 提供，从 FLIR 相机获取一帧图像。
cv::Mat GetFrameFromCamera();

namespace FocusImageExportTest
{
struct ExportConfig
{
    QString portName;
    std::filesystem::path outputDir = "focus_export";
    int sampleCount = 7;
    qint32 spacingPulses = 400;
    qint32 baudRate = 115200;
    quint16 speedRpm = 500;
    quint8 acceleration = 15;
    int settleMs = 700;
    int frameRetries = 8;
    int frameRetryIntervalMs = 80;
    bool returnToCenter = true;
};

void PrintUsage(const QStringList& availablePorts)
{
    std::cerr
        << "Usage: FocusImageExportTest.exe COM3 [output_dir] [count] [spacing_pulses] "
           "[baud] [speed_rpm] [acceleration] [settle_ms]\n"
        << "Example: FocusImageExportTest.exe COM3 focus_export 7 400 115200 500 15 700\n";

    if (!availablePorts.isEmpty())
    {
        std::cerr << "Available ports:";
        for (const QString& port : availablePorts)
        {
            std::cerr << " " << port.toLocal8Bit().constData();
        }
        std::cerr << "\n";
    }
}

bool ParseIntArgument(const QStringList& args, int index, int& value)
{
    if (args.size() <= index)
    {
        return true;
    }

    bool ok = false;
    const int parsed = args.at(index).toInt(&ok);
    if (!ok)
    {
        return false;
    }

    value = parsed;
    return true;
}

bool ParseConfig(const QStringList& args, const QStringList& availablePorts, ExportConfig& config)
{
    if (args.size() < 2)
    {
        PrintUsage(availablePorts);
        return false;
    }

    config.portName = args.at(1);
    if (args.size() > 2)
    {
        config.outputDir = std::filesystem::path(args.at(2).toStdString());
    }

    int sampleCount = config.sampleCount;
    int spacingPulses = config.spacingPulses;
    int baudRate = config.baudRate;
    int speedRpm = config.speedRpm;
    int acceleration = config.acceleration;
    int settleMs = config.settleMs;

    if (!ParseIntArgument(args, 3, sampleCount) ||
        !ParseIntArgument(args, 4, spacingPulses) ||
        !ParseIntArgument(args, 5, baudRate) ||
        !ParseIntArgument(args, 6, speedRpm) ||
        !ParseIntArgument(args, 7, acceleration) ||
        !ParseIntArgument(args, 8, settleMs))
    {
        std::cerr << "Invalid numeric argument.\n";
        PrintUsage(availablePorts);
        return false;
    }

    if (sampleCount < 1 || sampleCount % 2 == 0)
    {
        std::cerr << "count must be a positive odd number, so the center focus frame is included.\n";
        return false;
    }

    if (spacingPulses <= 0 || baudRate <= 0 || speedRpm <= 0 ||
        speedRpm > 6000 || acceleration < 0 || acceleration > 200 || settleMs < 0)
    {
        std::cerr << "Numeric argument is out of allowed range.\n";
        return false;
    }

    config.sampleCount = sampleCount;
    config.spacingPulses = static_cast<qint32>(spacingPulses);
    config.baudRate = static_cast<qint32>(baudRate);
    config.speedRpm = static_cast<quint16>(speedRpm);
    config.acceleration = static_cast<quint8>(acceleration);
    config.settleMs = settleMs;
    return true;
}

std::vector<qint32> BuildUniformOffsets(int sampleCount, qint32 spacingPulses)
{
    std::vector<qint32> offsets;
    offsets.reserve(static_cast<size_t>(sampleCount));

    const int halfCount = sampleCount / 2;
    for (int index = -halfCount; index <= halfCount; ++index)
    {
        offsets.push_back(static_cast<qint32>(index * spacingPulses));
    }

    return offsets;
}

std::string FileNameForSample(int index, qint32 offsetPulses, qint32 actualPosition)
{
    std::ostringstream name;
    name << "focus_"
         << std::setw(3) << std::setfill('0') << index
         << "_offset_" << offsetPulses
         << "_pos_" << actualPosition
         << ".png";
    return name.str();
}

bool CheckResult(const MotorSerialPort::Result& result, const char* action)
{
    if (result.success)
    {
        return true;
    }

    std::cerr << action << " failed: " << result.message.toLocal8Bit().constData() << "\n";
    return false;
}

bool MoveToPosition(MotorSerialPort& motor,
                    qint32& currentPosition,
                    qint32 targetPosition,
                    const ExportConfig& config)
{
    const qint64 delta = static_cast<qint64>(targetPosition) - static_cast<qint64>(currentPosition);
    if (delta == 0)
    {
        return true;
    }

    const bool forward = delta > 0;
    const quint32 pulseCount = static_cast<quint32>(std::llabs(delta));
    if (!CheckResult(motor.moveRelative(forward, config.speedRpm, config.acceleration, pulseCount),
                     "moveRelative"))
    {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(config.settleMs));

    MotorSerialPort::MotorSnapshot snapshot;
    if (!CheckResult(motor.readSnapshot(snapshot), "readSnapshot"))
    {
        return false;
    }

    currentPosition = snapshot.position;
    return true;
}

cv::Mat CaptureFrameWithRetry(const ExportConfig& config)
{
    for (int attempt = 0; attempt < config.frameRetries; ++attempt)
// 用法：由 SpinnakerCamera.cpp 提供，从 FLIR 相机获取一帧图像。
        cv::Mat frame = GetFrameFromCamera();
        if (!frame.empty())
        {
            return frame;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config.frameRetryIntervalMs));
    }

    return {};
}

int RunExport(const ExportConfig& config)
{
    std::filesystem::create_directories(config.outputDir);

    const std::filesystem::path manifestPath = config.outputDir / "manifest.csv";
    std::ofstream manifest(manifestPath, std::ios::binary);
    if (!manifest.is_open())
    {
        std::cerr << "Failed to create manifest: " << manifestPath.string() << "\n";
        return 1;
    }

    manifest << "index,offset_pulses,target_position,actual_position,sharpness,file\n";

    MotorSerialPort motor;
    QString errorMessage;
    if (!motor.open(config.portName, config.baudRate, errorMessage))
    {
        std::cerr << "Open serial failed: " << errorMessage.toLocal8Bit().constData() << "\n";
        return 1;
    }

    if (!CheckResult(motor.setCommunicationPositionMode(), "setCommunicationPositionMode") ||
        !CheckResult(motor.clearState(), "clearState") ||
        !CheckResult(motor.enableMotor(true), "enableMotor"))
    {
        motor.close();
        return 1;
    }

    MotorSerialPort::MotorSnapshot snapshot;
    if (!CheckResult(motor.readSnapshot(snapshot), "readSnapshot"))
    {
        motor.close();
        return 1;
    }

    const qint32 centerPosition = snapshot.position;
    qint32 currentPosition = centerPosition;
    const std::vector<qint32> offsets = BuildUniformOffsets(config.sampleCount, config.spacingPulses);

    std::cout << "Focus center position: " << centerPosition << " pulses\n";
    bool success = true;

    for (int index = 0; index < static_cast<int>(offsets.size()); ++index)
    {
        const qint32 offset = offsets[static_cast<size_t>(index)];
        const qint32 targetPosition = centerPosition + offset;

        if (!MoveToPosition(motor, currentPosition, targetPosition, config))
        {
            success = false;
            break;
        }

        cv::Mat frame = CaptureFrameWithRetry(config);
        if (frame.empty())
        {
            std::cerr << "Capture frame failed at offset " << offset << ".\n";
            manifest << index << "," << offset << "," << targetPosition << ","
                     << currentPosition << ",,capture_failed\n";
            success = false;
            break;
        }

        const double sharpness = Sharpness::Calculate(frame);
        const std::string fileName = FileNameForSample(index, offset, currentPosition);
        const std::filesystem::path imagePath = config.outputDir / fileName;

        if (!cv::imwrite(imagePath.string(), frame))
        {
            std::cerr << "Failed to write image: " << imagePath.string() << "\n";
            success = false;
            break;
        }

        manifest << index << ","
                 << offset << ","
                 << targetPosition << ","
                 << currentPosition << ","
                 << std::fixed << std::setprecision(6) << sharpness << ","
                 << fileName << "\n";

        std::cout << "Saved " << fileName
                  << " offset=" << offset
                  << " actual_position=" << currentPosition
                  << " sharpness=" << sharpness << "\n";
    }

    if (config.returnToCenter && currentPosition != centerPosition)
    {
        std::cout << "Returning to center position: " << centerPosition << " pulses\n";
        if (!MoveToPosition(motor, currentPosition, centerPosition, config))
        {
            success = false;
        }
    }

    motor.close();
    std::cout << "Manifest: " << manifestPath.string() << "\n";
    return success ? 0 : 1;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    MotorSerialPort portProbe;
    const QStringList availablePorts = portProbe.availablePorts();

    FocusImageExportTest::ExportConfig config;
    if (!FocusImageExportTest::ParseConfig(application.arguments(), availablePorts, config))
    {
        return 1;
    }

    return FocusImageExportTest::RunExport(config);
}
