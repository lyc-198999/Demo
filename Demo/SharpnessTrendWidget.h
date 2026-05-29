#ifndef DEMO_SHARPNESSTRENDWIDGET_H
#define DEMO_SHARPNESSTRENDWIDGET_H

#include <QPointF>
#include <QVector>
#include <QWidget>

class SharpnessTrendWidget : public QWidget
{
public:
    explicit SharpnessTrendWidget(QWidget* parent = nullptr);

    void appendSample(double position, double sharpness);
    void clearSamples();
    void setTargetMarkers(bool hasTarget,
                          double targetPosition,
                          bool hasEstimate,
                          double estimatedPosition);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<QPointF> samples;
    bool targetPositionValid = false;
    bool estimatedPositionValid = false;
    double targetPositionValue = 0.0;
    double estimatedPositionValue = 0.0;
};

#endif
