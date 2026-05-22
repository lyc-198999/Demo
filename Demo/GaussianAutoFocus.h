#ifndef GAUSSIAN_AUTO_FOCUS_H
#define GAUSSIAN_AUTO_FOCUS_H

#include <vector>

namespace GaussianAutoFocus
{
struct FitOptions
{
    int maxIterations = 30;
    double convergenceTolerance = 1e-6;
    double minSigma = 1e-9;
};

struct FitResult
{
    double amplitude = 0.0;
    double center = 0.0;
    double sigma = 0.0;
    double baseline = 0.0;
    double residualNorm = 0.0;
    int iterations = 0;
    bool converged = false;
};

// 用法：根据拟合结果计算指定位置的高斯模型值。
double Evaluate(const FitResult& fit, double zValue);
// 用法：使用 Gauss-Newton 迭代拟合高斯曲线。
FitResult FitGaussNewton(const std::vector<double>& zValues,
                         const std::vector<double>& sharpnessValues,
                         const FitOptions& options = FitOptions());
// 用法：返回从最后一个采样点移动到拟合峰值的相对步长。
double CalculateFocusOffset(const std::vector<double>& zValues,
                            const std::vector<double>& sharpnessValues,
                            const FitOptions& options = FitOptions());
}

#endif
