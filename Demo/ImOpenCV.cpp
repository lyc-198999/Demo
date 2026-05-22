#define _CRT_SECURE_NO_WARNINGS
#define FLIR_SPINNAKER_DEVICE_EVENT_HANDLER_H
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"

#include <opencv2/opencv.hpp>

using namespace Spinnaker;

// 用法：从 FLIR 相机获取一帧 Mono8 图像，失败时返回空 Mat。
cv::Mat GetFrameFromCamera()
{
    try
    {
        static SystemPtr system = System::GetInstance();
        static CameraList camList = system->GetCameras();
        if (camList.GetSize() == 0)
        {
            return cv::Mat();
        }

        static CameraPtr pCam = camList.GetByIndex(0);
        static bool initialized = false;
        static ImageProcessor processor;

        if (!pCam || !pCam->IsValid())
        {
            return cv::Mat();
        }

        if (!initialized)
        {
            pCam->Init();
            pCam->BeginAcquisition();
            processor.SetColorProcessing(SPINNAKER_COLOR_PROCESSING_ALGORITHM_HQ_LINEAR);
            initialized = true;
        }

        ImagePtr pImage = pCam->GetNextImage(60);
        if (!pImage || pImage->IsIncomplete())
        {
            if (pImage)
            {
                pImage->Release();
            }
            return cv::Mat();
        }

        ImagePtr converted = processor.Convert(pImage, PixelFormat_Mono8);
        if (!converted || !converted->GetData())
        {
            pImage->Release();
            return cv::Mat();
        }

        cv::Mat img(converted->GetHeight(),
                    converted->GetWidth(),
                    CV_8UC1,
                    converted->GetData());

        cv::Mat safe = img.clone();
        pImage->Release();
        return safe;
    }
    catch (const Spinnaker::Exception&)
    {
        return cv::Mat();
    }
    catch (...)
    {
        return cv::Mat();
    }
}
