#include "Demo.h"
#include "GaussianAutoFocus.h"
#include "ui_Demo.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <QTimer>

namespace DemoAutoFocusDetail
{
constexpr quint32 kAutoFocusMaxCorrectionPulses = 100000;
constexpr quint32 kAutoFocusFinishTolerancePulses = 20;
constexpr int kAutoFocusMoveSettleMs = 700;
constexpr size_t kAutoFocusMinFitSamples = 6;
constexpr size_t kAutoFocusMaxSamples = 100;
constexpr size_t kAutoFocusDirectionProbeSamples = 5;
constexpr size_t kAutoFocusInitialProbeSamples = 32;
constexpr size_t kAutoFocusPostPeakSamples = 3;
constexpr double kAutoFocusDropTolerance = 0.6;
constexpr double kAutoFocusDropRatio = 0.01;
constexpr double kAutoFocusVerifySharpnessRatio = 0.95;

// 用法：判断清晰度是否出现超过噪声阈值的有效下降。
bool IsSignificantSharpnessDrop(double previous, double current)
{
    const double threshold = std::max(kAutoFocusDropTolerance, std::abs(previous) * kAutoFocusDropRatio);
    return current + threshold < previous;
}

// 用法：判断清晰度是否出现超过噪声阈值的有效上升。
bool IsSignificantSharpnessRise(double previous, double current)
{
    const double threshold = std::max(kAutoFocusDropTolerance, std::abs(previous) * kAutoFocusDropRatio);
    return current > previous + threshold;
}

// 用法：统计清晰度序列尾部连续有效下降的次数。
int CountTailSharpnessDrops(const std::vector<double>& values)
{
    int drops = 0;
    for (size_t i = values.size(); i > 1; --i)
    {
        if (!IsSignificantSharpnessDrop(values[i - 2], values[i - 1]))
        {
            break;
        }

        ++drops;
    }

    return drops;
}

// 用法：返回清晰度最高采样点的下标。
size_t BestSharpnessIndex(const std::vector<double>& values)
{
    size_t bestIndex = 0;
    for (size_t i = 1; i < values.size(); ++i)
    {
        if (values[i] > values[bestIndex])
        {
            bestIndex = i;
        }
    }

    return bestIndex;
}

// 用法：判断整段扫描历史中是否已经出现过累计有效上升。
bool HasAnySharpnessRise(const std::vector<double>& values)
{
    if (values.empty())
    {
        return false;
    }

    double minValue = values.front();
    for (size_t i = 1; i < values.size(); ++i)
    {
        if (IsSignificantSharpnessRise(minValue, values[i]))
        {
            return true;
        }

        minValue = std::min(minValue, values[i]);
    }

    return false;
}

// 用法：判断指定候选峰值前是否已经出现过累计有效上升。
bool HasRiseBeforeIndex(const std::vector<double>& values, size_t candidateIndex)
{
    if (values.empty())
    {
        return false;
    }

    double minValue = values.front();
    for (size_t i = 1; i <= candidateIndex && i < values.size(); ++i)
    {
        if (IsSignificantSharpnessRise(minValue, values[i]))
        {
            return true;
        }

        minValue = std::min(minValue, values[i]);
    }

    return false;
}

// 用法：统计最高点之后已经有效低于峰值的过峰采样次数。
int CountPostPeakSamples(const std::vector<double>& values, size_t peakIndex)
{
    if (peakIndex >= values.size())
    {
        return 0;
    }

    int overPeakSamples = 0;
    const double peakValue = values[peakIndex];
    for (size_t i = peakIndex + 1; i < values.size(); ++i)
    {
        if (IsSignificantSharpnessDrop(peakValue, values[i]))
        {
            ++overPeakSamples;
        }
    }

    return overPeakSamples;
}

// 用法：按论文逻辑确认峰值，要求最高点前已上升且最高点后已过峰采样 3 次。
bool TryFindConfirmedPeakIndex(const std::vector<double>& values, size_t& peakIndex)
{
    if (values.size() <= kAutoFocusPostPeakSamples + 1)
    {
        return false;
    }

    const size_t bestIndex = BestSharpnessIndex(values);
    if (!HasRiseBeforeIndex(values, bestIndex))
    {
        return false;
    }

    if (CountPostPeakSamples(values, bestIndex) < static_cast<int>(kAutoFocusPostPeakSamples))
    {
        return false;
    }

    peakIndex = bestIndex;
    return true;
}

// 用法：判断高斯拟合中心是否落在采样范围内且没有偏离采样峰值过远。
bool IsUsableGaussianCenter(double center,
                            double minPosition,
                            double maxPosition,
                            double bestPosition,
                            double scanStep)
{
    if (!std::isfinite(center))
    {
        return false;
    }

    const double margin = scanStep;
    if (center < minPosition - margin || center > maxPosition + margin)
    {
        return false;
    }

    return std::abs(center - bestPosition) <= scanStep * 1.5;
}

// 用法：判断当前扫描是否已经接近焦面，需要切换为小步长采样。
bool ShouldUseFineAutoFocusStep(const std::vector<double>& values,
                                size_t bestIndex,
                                int postPeakSamples)
{
    const bool hasRiseBeforeBest = HasRiseBeforeIndex(values, bestIndex);
    if (!hasRiseBeforeBest)
    {
        return false;
    }

    if (postPeakSamples > 0)
    {
        return true;
    }

    if (values.size() < kAutoFocusMinFitSamples || bestIndex + 1 != values.size())
    {
        return false;
    }

    const double previous = values[values.size() - 2];
    const double current = values.back();
    const double plateauThreshold = std::max(kAutoFocusDropTolerance * 0.5,
                                             std::abs(current) * kAutoFocusDropRatio * 0.5);
    return std::abs(current - previous) <= plateauThreshold;
}

// 用法：计算采样位置和清晰度的线性斜率，用于初始扫描方向判断。
double LinearSharpnessSlope(const std::vector<double>& positions,
                            const std::vector<double>& values,
                            size_t sampleCount)
{
    if (positions.size() < sampleCount || values.size() < sampleCount || sampleCount < 2)
    {
        return 0.0;
    }

    double meanPosition = 0.0;
    double meanSharpness = 0.0;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        meanPosition += positions[i];
        meanSharpness += values[i];
    }
    meanPosition /= static_cast<double>(sampleCount);
    meanSharpness /= static_cast<double>(sampleCount);

    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = 0; i < sampleCount; ++i)
    {
        const double dx = positions[i] - meanPosition;
        numerator += dx * (values[i] - meanSharpness);
        denominator += dx * dx;
    }

    if (std::abs(denominator) < 1e-12)
    {
        return 0.0;
    }

    return numerator / denominator;
}

// 用法：判断启动方向是否没有形成有效上升，需要丢弃单侧探测并反向扫描。
bool ShouldReverseInitialScan(const std::vector<double>& positions,
                              const std::vector<double>& values,
                              size_t bestIndex,
                              int tailDrops,
                              int postPeakSamples,
                              bool hasSeenRise)
{
    const size_t sampleCount = values.size();
    if (sampleCount < 2 || hasSeenRise)
    {
        return false;
    }

    if (sampleCount == kAutoFocusDirectionProbeSamples)
    {
        const double positionRange = positions[kAutoFocusDirectionProbeSamples - 1] - positions.front();
        const double expectedChange =
            LinearSharpnessSlope(positions, values, kAutoFocusDirectionProbeSamples) * positionRange;
        const double threshold = std::max(kAutoFocusDropTolerance,
                                          std::abs(values.front()) * kAutoFocusDropRatio);
        return expectedChange < -threshold;
    }

    if (tailDrops >= 2)
    {
        return true;
    }

    if (bestIndex == 0 &&
        postPeakSamples >= static_cast<int>(kAutoFocusPostPeakSamples))
    {
        return true;
    }

    return sampleCount >= kAutoFocusInitialProbeSamples;
}

}

// 用法：自动模式下完成采样、扫描、拟合和最终回焦调度。
void MainWindow::processAutoFocus()
{
    if (!ui->radio_auto->isChecked())
    {
        return;
    }

    if (emergencyStopActive)
    {
        reportAutoFocusBlocked("自动对焦等待急停解除");
        return;
    }

    if (!serialConnected)
    {
        reportAutoFocusBlocked("自动对焦等待串口连接");
        return;
    }

    if (!cameraOnline)
    {
        reportAutoFocusBlocked("自动对焦等待相机图像");
        return;
    }

    if (!frameFormatValid)
    {
        reportAutoFocusBlocked("自动对焦等待有效图像格式");
        return;
    }

    if (autoFocusMovePending)
    {
        return;
    }

    const bool verifyFinalPosition = autoFocusFinalMoveSent;
    if (verifyFinalPosition)
    {
        autoFocusFinalMoveSent = false;
        appendLog("自动对焦最终位置复核中。");
    }

    if (autoFocusFinished)
    {
        return;
    }

    if (currentSharpness <= 0.0 || !std::isfinite(currentSharpness))
    {
        reportAutoFocusBlocked("自动对焦等待有效清晰度");
        return;
    }

    clearAutoFocusBlockReason();

    if (!appendAutoFocusSample())
    {
        return;
    }

    const size_t sampleCount = autoFocusPositions.size();
    size_t bestIndex = DemoAutoFocusDetail::BestSharpnessIndex(autoFocusSharpnessValues);
    size_t confirmedPeakIndex = bestIndex;
    const bool foundConfirmedPeak =
        DemoAutoFocusDetail::TryFindConfirmedPeakIndex(autoFocusSharpnessValues, confirmedPeakIndex);
    if (foundConfirmedPeak)
    {
        bestIndex = confirmedPeakIndex;
        autoFocusPeakConfirmed = true;
    }

    const double bestPosition = autoFocusPositions[bestIndex];
    const double bestSharpness = autoFocusSharpnessValues[bestIndex];
    const int postPeakSamples =
        DemoAutoFocusDetail::CountPostPeakSamples(autoFocusSharpnessValues, bestIndex);

    if (verifyFinalPosition)
    {
        const bool closeToFinalTarget =
            autoFocusHasFinalTarget &&
            std::abs(autoFocusEstimatedPosition - autoFocusFinalTargetPosition) <=
                static_cast<double>(DemoAutoFocusDetail::kAutoFocusFinishTolerancePulses);
        const bool closeToBestSharpness =
            autoFocusPeakConfirmed &&
            currentSharpness >= bestSharpness * DemoAutoFocusDetail::kAutoFocusVerifySharpnessRatio;
        if (closeToFinalTarget && closeToBestSharpness)
        {
            autoFocusFinished = true;
            appendLog(QString("自动对焦完成：目标位置=%1，当前位置=%2，复核清晰度=%3，最佳清晰度=%4。")
                          .arg(autoFocusFinalTargetPosition, 0, 'f', 0)
                          .arg(autoFocusEstimatedPosition, 0, 'f', 0)
                          .arg(currentSharpness, 0, 'f', 2)
                          .arg(bestSharpness, 0, 'f', 2));
            updateStatusDisplay();
            return;
        }

        if (!closeToFinalTarget)
        {
            appendLog(QString("最终位置未到目标，继续修正：目标位置=%1，当前位置=%2。")
                          .arg(autoFocusFinalTargetPosition, 0, 'f', 0)
                          .arg(autoFocusEstimatedPosition, 0, 'f', 0));
        }
        else
        {
            appendLog(QString("最终位置清晰度低于采样峰值，继续搜索：当前=%1，峰值=%2。")
                          .arg(currentSharpness, 0, 'f', 2)
                          .arg(bestSharpness, 0, 'f', 2));
        }
        autoFocusHasFinalTarget = false;
    }

    const int tailDrops = DemoAutoFocusDetail::CountTailSharpnessDrops(autoFocusSharpnessValues);
    const bool hasSeenRise = DemoAutoFocusDetail::HasAnySharpnessRise(autoFocusSharpnessValues);
    if (!autoFocusPeakConfirmed && !autoFocusDirectionReversed &&
        DemoAutoFocusDetail::ShouldReverseInitialScan(autoFocusPositions,
                                                      autoFocusSharpnessValues,
                                                      bestIndex,
                                                      tailDrops,
                                                      postPeakSamples,
                                                      hasSeenRise))
    {
        autoFocusScanDirection = -autoFocusScanDirection;
        autoFocusFineProbeDirection = -autoFocusScanDirection;
        autoFocusDirectionReversed = true;
        autoFocusFineScanActive = false;
        autoFocusPositions.clear();
        autoFocusSharpnessValues.clear();
        appendLog("初始扫描方向未检测到有效上升，已丢弃单侧探测并切换为反方向扫描。");

        if (!sendAutoFocusMove(static_cast<double>(autoFocusScanDirection) *
                                   static_cast<double>(autoFocusScanStep),
                               "反向扫描"))
        {
            autoFocusFinished = true;
            appendLog("自动对焦已终止。");
        }
        return;
    }

    if (!autoFocusPeakConfirmed)
    {
        if (sampleCount >= DemoAutoFocusDetail::kAutoFocusMaxSamples)
        {
            autoFocusFinished = true;
            reportAutoFocusBlocked("自动对焦未检测到过峰 3 次，请增大扫描范围或减小步长");
            return;
        }

        if (tailDrops == 0 && sampleCount >= 2 &&
            DemoAutoFocusDetail::IsSignificantSharpnessRise(autoFocusSharpnessValues[sampleCount - 2],
                                                            autoFocusSharpnessValues[sampleCount - 1]))
        {
            appendLog("检测到清晰度上升，继续同方向扫描以确认下降侧。");
        }
        else if (hasSeenRise && postPeakSamples > 0)
        {
            appendLog(QString("已过峰 %1 次，继续同方向扫描至过峰 3 次。").arg(postPeakSamples));
        }

        const bool useFineStep =
            autoFocusFineScanActive ||
            DemoAutoFocusDetail::ShouldUseFineAutoFocusStep(autoFocusSharpnessValues,
                                                            bestIndex,
                                                            postPeakSamples);
        if (useFineStep && !autoFocusFineScanActive)
        {
            autoFocusFineScanActive = true;
            appendLog("已进入焦面附近小步扫描，将保持小步长直到峰值确认。");
        }

        const quint32 scanStep = useFineStep ? autoFocusFineStep : autoFocusScanStep;
        if (!sendAutoFocusMove(static_cast<double>(autoFocusScanDirection) *
                                   static_cast<double>(scanStep),
                               useFineStep ? "焦面小步扫描采样" : "扫描采样"))
        {
            autoFocusFinished = true;
            appendLog("自动对焦已终止。");
        }
        return;
    }

    double targetPosition = bestPosition;
    QString targetSource = "采样峰值";
    try
    {
        if (sampleCount >= DemoAutoFocusDetail::kAutoFocusMinFitSamples)
        {
            const GaussianAutoFocus::FitResult fit =
                GaussianAutoFocus::FitGaussNewton(autoFocusPositions, autoFocusSharpnessValues);
            const auto [minIt, maxIt] =
                std::minmax_element(autoFocusPositions.begin(), autoFocusPositions.end());
            if (DemoAutoFocusDetail::IsUsableGaussianCenter(fit.center,
                                                            *minIt,
                                                            *maxIt,
                                                            bestPosition,
                                                            static_cast<double>(autoFocusScanStep)))
            {
                targetPosition = fit.center;
                targetSource = "高斯拟合";
            }

            appendLog(QString("自动对焦拟合完成：峰值位置=%1，采样峰值=%2，当前位置=%3，目标来源=%4，残差=%5。")
                          .arg(fit.center, 0, 'f', 1)
                          .arg(bestPosition, 0, 'f', 1)
                          .arg(autoFocusEstimatedPosition, 0, 'f', 1)
                          .arg(targetSource)
                          .arg(fit.residualNorm, 0, 'f', 3));
        }

        const double correctionPulses = targetPosition - autoFocusEstimatedPosition;
        const double safetyBoundedCorrection = std::clamp(
            correctionPulses,
            -static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses),
            static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses));

        if (std::abs(correctionPulses - safetyBoundedCorrection) > 0.5)
        {
            appendLog(QString("自动对焦修正量超过安全上限，已限制为 %1 脉冲。")
                          .arg(safetyBoundedCorrection, 0, 'f', 0));
        }

        if (std::abs(safetyBoundedCorrection) <=
            static_cast<double>(DemoAutoFocusDetail::kAutoFocusFinishTolerancePulses))
        {
            if (autoFocusPeakConfirmed &&
                currentSharpness >= bestSharpness * DemoAutoFocusDetail::kAutoFocusVerifySharpnessRatio)
            {
                autoFocusFinished = true;
                appendLog(QString("自动对焦完成：当前位置接近%1，清晰度=%2。")
                              .arg(targetSource)
                              .arg(currentSharpness, 0, 'f', 2));
                updateStatusDisplay();
                return;
            }

            const double fallbackDelta = bestPosition - autoFocusEstimatedPosition;
            if (std::abs(fallbackDelta) >
                static_cast<double>(DemoAutoFocusDetail::kAutoFocusFinishTolerancePulses))
            {
                const double safetyBoundedFallbackDelta = std::clamp(
                    fallbackDelta,
                    -static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses),
                    static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses));
                if (sendAutoFocusMove(safetyBoundedFallbackDelta, "回到采样峰值"))
                {
                    autoFocusFinalTargetPosition = bestPosition;
                    autoFocusHasFinalTarget = true;
                    autoFocusFinalMoveSent = true;
                }
                else
                {
                    autoFocusFinished = true;
                    appendLog("自动对焦回到采样峰值失败，流程已终止。");
                }
                return;
            }

            const double probeDelta =
                static_cast<double>(autoFocusFineProbeDirection) * static_cast<double>(autoFocusFineStep);
            autoFocusFineProbeDirection = -autoFocusFineProbeDirection;
            if (!sendAutoFocusMove(probeDelta, "焦面小步复核"))
            {
                autoFocusFinished = true;
                appendLog("自动对焦复核扫描失败，流程已终止。");
            }
            return;
        }

        if (sendAutoFocusMove(safetyBoundedCorrection, "最终回焦"))
        {
            autoFocusFinalTargetPosition = targetPosition;
            autoFocusHasFinalTarget = true;
            autoFocusFinalMoveSent = true;
        }
        else
        {
            autoFocusFinished = true;
            appendLog("自动对焦最终回焦失败，流程已终止。");
        }
    }
    catch (const std::exception& error)
    {
        appendLog(QString("自动对焦拟合失败，改用采样峰值：%1").arg(QString::fromUtf8(error.what())));
        if (!autoFocusPeakConfirmed)
        {
            autoFocusFinished = true;
            reportAutoFocusBlocked("自动对焦拟合失败且尚未检测到过峰 3 次");
            return;
        }

        const double correctionPulses = bestPosition - autoFocusEstimatedPosition;
        if (std::abs(correctionPulses) <=
            static_cast<double>(DemoAutoFocusDetail::kAutoFocusFinishTolerancePulses))
        {
            if (currentSharpness >= bestSharpness * DemoAutoFocusDetail::kAutoFocusVerifySharpnessRatio)
            {
                autoFocusFinished = true;
                appendLog("自动对焦完成：当前位置接近已确认的采样峰值。");
                updateStatusDisplay();
            }
            else
            {
                const double probeDelta =
                    static_cast<double>(autoFocusFineProbeDirection) * static_cast<double>(autoFocusFineStep);
                autoFocusFineProbeDirection = -autoFocusFineProbeDirection;
                if (!sendAutoFocusMove(probeDelta, "峰值小步复核"))
                {
                    autoFocusFinished = true;
                    appendLog("自动对焦峰值复核失败，流程已终止。");
                }
            }
            return;
        }

        const double safetyBoundedCorrection = std::clamp(
            correctionPulses,
            -static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses),
            static_cast<double>(DemoAutoFocusDetail::kAutoFocusMaxCorrectionPulses));
        if (sendAutoFocusMove(safetyBoundedCorrection, "回到采样峰值"))
        {
            autoFocusFinalTargetPosition = bestPosition;
            autoFocusHasFinalTarget = true;
            autoFocusFinalMoveSent = true;
        }
        else
        {
            autoFocusFinished = true;
            appendLog("自动对焦回到采样峰值失败，流程已终止。");
        }
    }
}

// 用法：清空自动对焦采样、估算位置和阻塞提示，准备重新开始。
void MainWindow::resetAutoFocusState(bool logReset)
{
    const bool hadState = !autoFocusPositions.empty() ||
                          !autoFocusSharpnessValues.empty() ||
                          autoFocusMovePending ||
                          autoFocusFinished ||
                          autoFocusFinalMoveSent ||
                          autoFocusFineScanActive ||
                          autoFocusHasFinalTarget ||
                          !autoFocusBlockReason.isEmpty();

    autoFocusPositions.clear();
    autoFocusSharpnessValues.clear();
    autoFocusMovePending = false;
    autoFocusFinished = false;
    autoFocusFinalMoveSent = false;
    autoFocusEnableRetryDone = false;
    autoFocusHasEstimatedPosition = false;
    autoFocusDirectionReversed = false;
    autoFocusPeakConfirmed = false;
    autoFocusFineScanActive = false;
    autoFocusHasFinalTarget = false;
    autoFocusScanDirection = 1;
    autoFocusFineProbeDirection = -autoFocusScanDirection;
    autoFocusEstimatedPosition = 0.0;
    autoFocusFinalTargetPosition = 0.0;
    autoFocusBlockReason.clear();

    if (logReset && hadState)
    {
        appendLog("自动对焦状态已重置。");
    }
}

// 用法：记录当前位置对应的清晰度采样，驱动器回读失败时当前位置保留内部估算值。
bool MainWindow::appendAutoFocusSample()
{
    const double position = static_cast<double>(currentPosition);
    autoFocusEstimatedPosition = position;
    autoFocusHasEstimatedPosition = true;

    if (!autoFocusPositions.empty() && std::abs(autoFocusPositions.back() - position) < 0.5)
    {
        autoFocusSharpnessValues.back() = currentSharpness;
        return false;
    }

    autoFocusPositions.push_back(position);
    autoFocusSharpnessValues.push_back(currentSharpness);

    appendLog(QString("自动对焦采样：当前位置=%1，清晰度=%2。")
                  .arg(position, 0, 'f', 0)
                  .arg(currentSharpness, 0, 'f', 2));
    updateStatusDisplay();
    return true;
}

// 用法：发送自动对焦相对移动指令，并更新内部估算位置。
bool MainWindow::sendAutoFocusMove(double deltaPulses, const QString& reason)
{
    const double absoluteDelta = std::abs(deltaPulses);
    const quint32 pulseCount = static_cast<quint32>(std::llround(absoluteDelta));
    if (pulseCount == 0)
    {
        return false;
    }

    const bool forward = deltaPulses > 0.0;
    if (!motorEnabled && !autoFocusEnableRetryDone)
    {
        const MotorSerialPort::Result enableResult = motorSerial.enableMotor(true);
        autoFocusEnableRetryDone = true;
        if (!enableResult.success)
        {
            appendLog(QString("自动对焦发送移动前使能电机失败：%1").arg(enableResult.message));
            return false;
        }

        motorEnabled = true;
        appendLog("自动对焦发送移动前已重新使能电机。");
    }

    const MotorSerialPort::Result result = motorSerial.moveRelative(forward,
                                                                    autoFocusSpeedRpm,
                                                                    autoFocusAcceleration,
                                                                    pulseCount);
    if (!result.success)
    {
        appendLog(QString("发送自动对焦%1指令失败：%2").arg(reason).arg(result.message));
        return false;
    }

    autoFocusMovePending = true;
    motorRunState = "正在运行";
    motorInPosition = false;
    if (!autoFocusHasEstimatedPosition)
    {
        autoFocusEstimatedPosition = static_cast<double>(currentPosition);
        autoFocusHasEstimatedPosition = true;
    }

    autoFocusEstimatedPosition += forward ? static_cast<double>(pulseCount) : -static_cast<double>(pulseCount);
    currentPosition = static_cast<qint32>(std::llround(autoFocusEstimatedPosition));
    updateStatusDisplay();

    appendLog(QString("已发送自动对焦%1指令：方向=%2，步数=%3，速度=%4RPM，加减速度=%5。")
                  .arg(reason)
                  .arg(forward ? "前进" : "后退")
                  .arg(pulseCount)
                  .arg(autoFocusSpeedRpm)
                  .arg(autoFocusAcceleration));

    QTimer::singleShot(150, this, [this]() { synchronizeMotorStatus(false); });
    QTimer::singleShot(DemoAutoFocusDetail::kAutoFocusMoveSettleMs, this, [this]() {
        if (synchronizeMotorStatus(false))
        {
            autoFocusEstimatedPosition = static_cast<double>(currentPosition);
            autoFocusHasEstimatedPosition = true;
        }
        else
        {
            currentPosition = static_cast<qint32>(std::llround(autoFocusEstimatedPosition));
        }

        autoFocusMovePending = false;
        updateStatusDisplay();
    });
    return true;
}

// 用法：记录自动对焦当前阻塞原因，同一原因不会重复刷屏。
void MainWindow::reportAutoFocusBlocked(const QString& reason)
{
    if (autoFocusBlockReason == reason)
    {
        return;
    }

    autoFocusBlockReason = reason;
    appendLog(reason + "。");
    updateStatusDisplay();
}

// 用法：清除自动对焦阻塞原因并刷新状态栏。
void MainWindow::clearAutoFocusBlockReason()
{
    if (autoFocusBlockReason.isEmpty())
    {
        return;
    }

    autoFocusBlockReason.clear();
    updateStatusDisplay();
}
