#ifndef DEMO_SHARPNESS_H
#define DEMO_SHARPNESS_H

#include <opencv2/opencv.hpp>

namespace Sharpness
{
// 用法：计算图像清晰度，返回 0-100 归一化 Tenengrad 指标。
double Calculate(const cv::Mat& image);
}

#endif
