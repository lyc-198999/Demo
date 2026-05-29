#include "SharpnessTrendWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizePolicy>

namespace SharpnessTrendWidgetDetail
{
constexpr int kMaxSamples = 180;
constexpr int kPaddingLeft = 44;
constexpr int kPaddingRight = 16;
constexpr int kPaddingTop = 20;
constexpr int kPaddingBottom = 28;

double ClampRange(double value, double minimum, double maximum)
{
    if (maximum <= minimum)
    {
        return 0.0;
    }

    return (value - minimum) / (maximum - minimum);
}
}

SharpnessTrendWidget::SharpnessTrendWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sharpnessTrendWidget");
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void SharpnessTrendWidget::appendSample(double position, double sharpness)
{
    if (!std::isfinite(position) || !std::isfinite(sharpness))
    {
        return;
    }

    samples.append(QPointF(position, sharpness));
    while (samples.size() > SharpnessTrendWidgetDetail::kMaxSamples)
    {
        samples.removeFirst();
    }

    update();
}

void SharpnessTrendWidget::clearSamples()
{
    samples.clear();
    update();
}

void SharpnessTrendWidget::setTargetMarkers(bool hasTarget,
                                            double targetPosition,
                                            bool hasEstimate,
                                            double estimatedPosition)
{
    targetPositionValid = hasTarget && std::isfinite(targetPosition);
    estimatedPositionValid = hasEstimate && std::isfinite(estimatedPosition);
    targetPositionValue = targetPosition;
    estimatedPositionValue = estimatedPosition;
    update();
}

QSize SharpnessTrendWidget::minimumSizeHint() const
{
    return QSize(420, 160);
}

void SharpnessTrendWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    using namespace SharpnessTrendWidgetDetail;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#ffffff"));

    const QRectF plotRect(kPaddingLeft,
                          kPaddingTop,
                          width() - kPaddingLeft - kPaddingRight,
                          height() - kPaddingTop - kPaddingBottom);

    painter.setPen(QPen(QColor("#d8e1eb"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    if (plotRect.width() <= 0.0 || plotRect.height() <= 0.0)
    {
        return;
    }

    painter.setPen(QPen(QColor("#e6edf5"), 1));
    for (int i = 0; i <= 4; ++i)
    {
        const double y = plotRect.top() + plotRect.height() * static_cast<double>(i) / 4.0;
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    for (int i = 0; i <= 5; ++i)
    {
        const double x = plotRect.left() + plotRect.width() * static_cast<double>(i) / 5.0;
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
    }

    painter.setPen(QColor("#52616f"));
    painter.setFont(QFont("Microsoft YaHei UI", 8));

    if (samples.isEmpty())
    {
        painter.drawText(plotRect,
                         Qt::AlignCenter,
                         QStringLiteral("\u7b49\u5f85\u6e05\u6670\u5ea6\u6570\u636e"));
        return;
    }

    auto minMaxSharpness = std::minmax_element(samples.cbegin(),
                                               samples.cend(),
                                               [](const QPointF& lhs, const QPointF& rhs) {
                                                   return lhs.y() < rhs.y();
                                               });
    auto minMaxPosition = std::minmax_element(samples.cbegin(),
                                             samples.cend(),
                                             [](const QPointF& lhs, const QPointF& rhs) {
                                                 return lhs.x() < rhs.x();
                                             });

    double minSharpness = minMaxSharpness.first->y();
    double maxSharpness = minMaxSharpness.second->y();
    if (std::abs(maxSharpness - minSharpness) < 0.001)
    {
        minSharpness -= 1.0;
        maxSharpness += 1.0;
    }

    const double minPosition = minMaxPosition.first->x();
    const double maxPosition = minMaxPosition.second->x();

    painter.drawText(QRectF(4, plotRect.top() - 2, kPaddingLeft - 8, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxSharpness, 'f', 1));
    painter.drawText(QRectF(4, plotRect.bottom() - 14, kPaddingLeft - 8, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(minSharpness, 'f', 1));

    QPainterPath path;
    QPointF bestPoint;
    double bestSharpness = -std::numeric_limits<double>::infinity();

    for (int i = 0; i < samples.size(); ++i)
    {
        const double xRatio = samples.size() == 1
                                  ? 1.0
                                  : static_cast<double>(i) / static_cast<double>(samples.size() - 1);
        const double yRatio = ClampRange(samples.at(i).y(), minSharpness, maxSharpness);
        const QPointF point(plotRect.left() + xRatio * plotRect.width(),
                            plotRect.bottom() - yRatio * plotRect.height());

        if (i == 0)
        {
            path.moveTo(point);
        }
        else
        {
            path.lineTo(point);
        }

        if (samples.at(i).y() > bestSharpness)
        {
            bestSharpness = samples.at(i).y();
            bestPoint = point;
        }
    }

    const auto drawPositionMarker = [&](double position, const QColor& color, const QString& label) {
        if (maxPosition <= minPosition)
        {
            return;
        }

        const double ratio = ClampRange(position, minPosition, maxPosition);
        if (ratio < 0.0 || ratio > 1.0)
        {
            return;
        }

        const double x = plotRect.left() + ratio * plotRect.width();
        QPen markerPen(color, 1, Qt::DashLine);
        painter.setPen(markerPen);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        painter.setPen(color);
        painter.drawText(QRectF(x - 36, plotRect.top(), 72, 18),
                         Qt::AlignCenter,
                         label);
    };

    if (targetPositionValid)
    {
        drawPositionMarker(targetPositionValue, QColor("#d97706"), QStringLiteral("\u76ee\u6807"));
    }

    if (estimatedPositionValid)
    {
        drawPositionMarker(estimatedPositionValue, QColor("#7c3aed"), QStringLiteral("\u4f30\u8ba1"));
    }

    painter.setPen(QPen(QColor("#2563eb"), 2));
    painter.drawPath(path);

    painter.setBrush(QColor("#16a34a"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(bestPoint, 4.5, 4.5);

    const QPointF latestPoint = path.currentPosition();
    painter.setBrush(QColor("#dc2626"));
    painter.drawEllipse(latestPoint, 4.0, 4.0);

    painter.setPen(QColor("#52616f"));
    painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 8, plotRect.width(), 16),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("\u6700\u8fd1 %1 \u5e27 | \u5f53\u524d %2 | \u5cf0\u503c %3")
                         .arg(samples.size())
                         .arg(samples.last().y(), 0, 'f', 2)
                         .arg(bestSharpness, 0, 'f', 2));
}
