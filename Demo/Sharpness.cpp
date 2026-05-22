#include "Sharpness.h"

#include <algorithm>
#include <cmath>

namespace SharpnessDetail
{
constexpr int kMaxSharpnessImageSide = 640;
constexpr double kTenengradEnergyScale = 0.02;
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
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else if (image.channels() == 4)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    }
    else
    {
        gray = image;
    }
    //方法一
   /* cv::Mat sampledGray = gray;
    const int maxSide = std::max(gray.cols, gray.rows);
    if (maxSide > SharpnessDetail::kMaxSharpnessImageSide)
    {
        const double scale = static_cast<double>(SharpnessDetail::kMaxSharpnessImageSide) /
                             static_cast<double>(maxSide);
        cv::resize(gray, sampledGray, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    cv::Mat grayFloat;
    if (sampledGray.depth() == CV_8U)
    {
        sampledGray.convertTo(grayFloat, CV_32F, 1.0 / 255.0);
    }
    else if (sampledGray.depth() == CV_16U)
    {
        sampledGray.convertTo(grayFloat, CV_32F, 1.0 / 65535.0);
    }
    else
    {
        cv::normalize(sampledGray, grayFloat, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);
    }

    cv::GaussianBlur(grayFloat, grayFloat, cv::Size(3, 3), 0.0);

    cv::Mat gradientX;
    cv::Mat gradientY;
    cv::Sobel(grayFloat, gradientX, CV_32F, 1, 0, 3);
    cv::Sobel(grayFloat, gradientY, CV_32F, 0, 1, 3);

    cv::Mat gradientEnergy = gradientX.mul(gradientX) + gradientY.mul(gradientY);
    const double rawEnergy = cv::mean(gradientEnergy)[0];
    return (1.0 - std::exp(-rawEnergy / SharpnessDetail::kTenengradEnergyScale)) * 100.0;*/

    //方法二
   /* cv::normalize(gray, gray);*/
    cv::Mat Lap;
    Laplacian(gray, Lap, CV_32F, 3);
    cv::Scalar mean, stddev;
    meanStdDev(Lap, mean, stddev);
    double var = stddev[0] * stddev[0];
    return var;
    

}

}
