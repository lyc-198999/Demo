#include "Demo.h"
#include "Sharpness.h"
#include "SharpnessTrendWidget.h"
#include "ui_Demo.h"

#include <algorithm>
#include <QPixmap>

// 用法：由 ImOpenCV.cpp 提供，从相机获取一帧图像。
cv::Mat GetFrameFromCamera();

namespace DemoImageDetail
{
constexpr int kMaxDisplayImageSide = 900;
constexpr int kMaxCameraMissesBeforeOffline = 3;

// 用法：将 OpenCV 图像帧转换为 Qt 可显示图像。
QImage MatToQImage(const cv::Mat& mat)
{
    if (mat.empty())
    {
        return {};
    }

    switch (mat.type())
    {
    case CV_8UC1:
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      static_cast<qsizetype>(mat.step),
                      QImage::Format_Grayscale8).copy();

    case CV_8UC3:
    {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data,
                      rgb.cols,
                      rgb.rows,
                      static_cast<qsizetype>(rgb.step),
                      QImage::Format_RGB888).copy();
    }

    case CV_8UC4:
    {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data,
                      rgba.cols,
                      rgba.rows,
                      static_cast<qsizetype>(rgba.step),
                      QImage::Format_RGBA8888).copy();
    }

    default:
        return {};
    }
}
}

// 用法：窗口尺寸变化后重新按比例刷新图像显示。
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    refreshDisplayedImage();
}

// 用法：定时采集相机帧、刷新清晰度并推进自动对焦流程。
void MainWindow::updateFrame()
{
    frame = GetFrameFromCamera();
    if (frame.empty())
    {
        ++consecutiveEmptyFrames;
        if (consecutiveEmptyFrames < DemoImageDetail::kMaxCameraMissesBeforeOffline)
        {
            return;
        }

        if (cameraOnline)
        {
            appendLog("相机图像流中断。");
        }

        cameraOnline = false;
        frameFormatValid = false;
        currentSharpness = 0.0;
        currentImage = QImage();

        ui->label_image->setPixmap(QPixmap());
        ui->label_image->setText("无图像");
        updateStatusDisplay();
        return;
    }

    consecutiveEmptyFrames = 0;
    if (!cameraOnline)
    {
        appendLog("相机图像流已恢复。");
    }

    cameraOnline = true;
    showFrame(frame);
    currentSharpness = frameFormatValid ? Sharpness::Calculate(frame) : 0.0;
    if (frameFormatValid && sharpnessTrendWidget != nullptr)
    {
        sharpnessTrendWidget->appendSample(static_cast<double>(currentPosition), currentSharpness);
    }
    updateStatusDisplay();
    processAutoFocus();
}

// 用法：显示一帧图像，并标记当前帧格式是否可用于计算。
void MainWindow::showFrame(const cv::Mat& frame)
{
    cv::Mat displayFrame = frame;
    const int maxSide = std::max(frame.cols, frame.rows);
    if (maxSide > DemoImageDetail::kMaxDisplayImageSide)
    {
        const double scale = static_cast<double>(DemoImageDetail::kMaxDisplayImageSide) /
                             static_cast<double>(maxSide);
        cv::resize(frame, displayFrame, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    currentImage = DemoImageDetail::MatToQImage(displayFrame);
    frameFormatValid = !currentImage.isNull();

    if (!frameFormatValid)
    {
        ui->label_image->setPixmap(QPixmap());
        ui->label_image->setText("不支持的图像帧");
        appendLog("接收到不支持的图像帧格式。");
        updateStatusDisplay();
        return;
    }

    refreshDisplayedImage();
}

// 用法：按显示区域大小等比例刷新当前图像。
void MainWindow::refreshDisplayedImage()
{
    if (currentImage.isNull())
    {
        return;
    }

    const QSize targetSize = ui->label_image->size();
    if (!targetSize.isValid())
    {
        return;
    }

    ui->label_image->setText(QString());
    ui->label_image->setPixmap(QPixmap::fromImage(currentImage).scaled(
        targetSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}
