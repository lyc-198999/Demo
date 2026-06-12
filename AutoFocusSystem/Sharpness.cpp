#include "Sharpness.h"

#include <algorithm>
#include <cmath>

namespace SharpnessDetail
{
constexpr int kMaxSharpnessImageSide = 640;
constexpr double kTenengradEnergyScale = 0.25;

bool ConvertToGray(const cv::Mat& image, cv::Mat& gray)
{
    if (image.channels() == 1)
    {
        gray = image;
        return true;
    }

    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return true;
    }

    if (image.channels() == 4)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        return true;
    }

    return false;
}

cv::Mat DownsampleForSharpness(const cv::Mat& gray)
{
    cv::Mat sampledGray = gray;
    const int maxSide = std::max(gray.cols, gray.rows);
    if (maxSide > kMaxSharpnessImageSide)
    {
        const double scale = static_cast<double>(kMaxSharpnessImageSide) /
                             static_cast<double>(maxSide);
        cv::resize(gray, sampledGray, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    return sampledGray;
}

bool ConvertToUnitFloat(const cv::Mat& gray, cv::Mat& grayFloat)
{
    switch (gray.depth())
    {
    case CV_8U:
        gray.convertTo(grayFloat, CV_32F, 1.0 / 255.0);
        return true;

    case CV_16U:
        gray.convertTo(grayFloat, CV_32F, 1.0 / 65535.0);
        return true;

    case CV_32F:
    case CV_64F:
    {
        double minValue = 0.0;
        double maxValue = 0.0;
        cv::minMaxLoc(gray, &minValue, &maxValue);
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue)
        {
            return false;
        }

        if (minValue >= 0.0 && maxValue <= 1.0)
        {
            gray.convertTo(grayFloat, CV_32F);
        }
        else if (minValue >= 0.0 && maxValue <= 255.0)
        {
            gray.convertTo(grayFloat, CV_32F, 1.0 / 255.0);
        }
        else
        {
            cv::normalize(gray, grayFloat, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);
        }
        return true;
    }

    default:
        cv::normalize(gray, grayFloat, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);
        return true;
    }
}

double NormalizeTenengradEnergy(double rawEnergy)
{
    if (!std::isfinite(rawEnergy) || rawEnergy <= 0.0)
    {
        return 0.0;
    }

    const double normalized = rawEnergy / (rawEnergy + kTenengradEnergyScale) * 100.0;
    return std::clamp(normalized, 0.0, 100.0);
}
}

namespace Sharpness
{
// 用法：传入相机帧或仿真图像，返回 0-100 归一化 Tenengrad 清晰度，数值越大表示越清晰。
double Calculate(const cv::Mat& image)
{
    if (image.empty())
    {
        return 0.0;
    }

    cv::Mat gray;
    if (!SharpnessDetail::ConvertToGray(image, gray))
    {
        return 0.0;
    }

    const cv::Mat sampledGray = SharpnessDetail::DownsampleForSharpness(gray);
    cv::Mat grayFloat;
    if (!SharpnessDetail::ConvertToUnitFloat(sampledGray, grayFloat))
    {
        return 0.0;
    }

    cv::GaussianBlur(grayFloat, grayFloat, cv::Size(3, 3), 0.0);

    cv::Mat gradientX;
    cv::Mat gradientY;
    cv::Sobel(grayFloat, gradientX, CV_32F, 1, 0, 3);
    cv::Sobel(grayFloat, gradientY, CV_32F, 0, 1, 3);

    const cv::Mat gradientEnergy = gradientX.mul(gradientX) + gradientY.mul(gradientY);
    const double rawEnergy = cv::mean(gradientEnergy)[0];
    return SharpnessDetail::NormalizeTenengradEnergy(rawEnergy);
}

}
