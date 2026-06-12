#include "GaussianAutoFocus.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <opencv2/core.hpp>
#include <stdexcept>

namespace GaussianAutoFocus
{
namespace Detail
{
constexpr double kTiny = 1e-12;

// 用法：检查拟合输入和中间参数是否为有限数。
bool IsFinite(double value)
{
    return std::isfinite(value);
}

// 用法：保证高斯宽度参数为正数，避免迭代时除零。
double ClampSigma(double sigma, double minSigma)
{
    const double lower = std::max(minSigma, kTiny);
    if (!IsFinite(sigma))
    {
        return lower;
    }

    sigma = std::abs(sigma);
    return std::max(sigma, lower);
}

// 用法：在高斯拟合开始前校验采样数量、阈值和数值有效性。
void ValidateInput(const std::vector<double>& zValues,
                   const std::vector<double>& sharpnessValues,
                   const FitOptions& options)
{
    if (zValues.size() != sharpnessValues.size())
    {
        throw std::invalid_argument("高斯拟合输入数据长度不匹配。");
    }

    if (zValues.size() < 4)
    {
        throw std::invalid_argument("高斯拟合至少需要四个采样点。");
    }

    if (options.maxIterations <= 0)
    {
        throw std::invalid_argument("高斯拟合最大迭代次数必须为正数。");
    }

    if (options.convergenceTolerance <= 0.0 || options.minSigma <= 0.0)
    {
        throw std::invalid_argument("高斯拟合阈值参数必须为正数。");
    }

    for (size_t i = 0; i < zValues.size(); ++i)
    {
        if (!IsFinite(zValues[i]) || !IsFinite(sharpnessValues[i]))
        {
            throw std::invalid_argument("高斯拟合输入数据包含无效数值。");
        }
    }
}

// 用法：根据采样数据生成 Gauss-Newton 迭代所需的初始 a、mu、sigma、c。
FitResult InitialEstimate(const std::vector<double>& zValues,
                          const std::vector<double>& sharpnessValues,
                          double minSigma)
{
    const auto [minYIt, maxYIt] = std::minmax_element(sharpnessValues.begin(), sharpnessValues.end());
    const auto [minZIt, maxZIt] = std::minmax_element(zValues.begin(), zValues.end());
    const double zRange = *maxZIt - *minZIt;
    if (std::abs(zRange) < kTiny)
    {
        throw std::invalid_argument("高斯拟合位置数据不能全部相同。");
    }

    const size_t maxIndex = static_cast<size_t>(std::distance(sharpnessValues.begin(), maxYIt));

    FitResult result;
    result.baseline = *minYIt;
    result.amplitude = *maxYIt - result.baseline;
    result.center = zValues[maxIndex];
    result.sigma = ClampSigma(0.2 * zRange, minSigma);
    return result;
}

// 用法：计算列向量的二范数，用于收敛判断和残差统计。
double VectorNorm(const cv::Mat& values)
{
    double sum = 0.0;
    for (int i = 0; i < values.rows; ++i)
    {
        const double value = values.at<double>(i, 0);
        sum += value * value;
    }

    return std::sqrt(sum);
}
}

// 用法：按拟合参数计算指定位置的高斯模型清晰度。
double Evaluate(const FitResult& fit, double zValue)
{
    if (fit.sigma <= 0.0 || !Detail::IsFinite(fit.sigma))
    {
        return fit.baseline;
    }

    const double u = (zValue - fit.center) / fit.sigma;
    const double g = std::exp(-0.5 * u * u);
    return fit.amplitude * g + fit.baseline;
}

// 用法：对位置和清晰度采样做 Gauss-Newton 高斯拟合，返回峰值中心和拟合质量。
FitResult FitGaussNewton(const std::vector<double>& zValues,
                         const std::vector<double>& sharpnessValues,
                         const FitOptions& options)
{
    Detail::ValidateInput(zValues, sharpnessValues, options);

    FitResult fit = Detail::InitialEstimate(zValues, sharpnessValues, options.minSigma);
    if (fit.amplitude <= Detail::kTiny)
    {
        fit.center = zValues.back();
        fit.residualNorm = 0.0;
        fit.converged = true;
        return fit;
    }

    const int sampleCount = static_cast<int>(zValues.size());
    cv::Mat jacobian(sampleCount, 4, CV_64F);
    cv::Mat residual(sampleCount, 1, CV_64F);

    for (int iter = 0; iter < options.maxIterations; ++iter)
    {
        fit.sigma = Detail::ClampSigma(fit.sigma, options.minSigma);

        for (int i = 0; i < sampleCount; ++i)
        {
            const size_t index = static_cast<size_t>(i);
            const double u = (zValues[index] - fit.center) / fit.sigma;
            const double g = std::exp(-0.5 * u * u);
            const double model = fit.amplitude * g + fit.baseline;

            residual.at<double>(i, 0) = sharpnessValues[index] - model;
            jacobian.at<double>(i, 0) = g;
            jacobian.at<double>(i, 1) = fit.amplitude * u * g / fit.sigma;
            jacobian.at<double>(i, 2) = fit.amplitude * u * u * g / fit.sigma;
            jacobian.at<double>(i, 3) = 1.0;
        }

        cv::Mat delta;
        if (!cv::solve(jacobian, residual, delta, cv::DECOMP_QR))
        {
            fit.residualNorm = Detail::VectorNorm(residual);
            return fit;
        }

        fit.amplitude += delta.at<double>(0, 0);
        fit.center += delta.at<double>(1, 0);
        fit.sigma = Detail::ClampSigma(fit.sigma + delta.at<double>(2, 0), options.minSigma);
        fit.baseline += delta.at<double>(3, 0);
        fit.iterations = iter + 1;

        const double deltaNorm = Detail::VectorNorm(delta);
        if (deltaNorm < options.convergenceTolerance)
        {
            fit.converged = true;
            break;
        }
    }

    fit.residualNorm = 0.0;
    for (size_t i = 0; i < zValues.size(); ++i)
    {
        const double error = sharpnessValues[i] - Evaluate(fit, zValues[i]);
        fit.residualNorm += error * error;
    }
    fit.residualNorm = std::sqrt(fit.residualNorm);
    return fit;
}

// 用法：拟合高斯峰值后返回从最后一个采样位置移动到峰值中心的相对步长。
double CalculateFocusOffset(const std::vector<double>& zValues,
                            const std::vector<double>& sharpnessValues,
                            const FitOptions& options)
{
    const FitResult fit = FitGaussNewton(zValues, sharpnessValues, options);
    return fit.center - zValues.back();
}
}
