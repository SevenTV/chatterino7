#include "LinearGradientPaint.hpp"

namespace chatterino {

LinearGradientPaint::LinearGradientPaint(
    const QString name, const std::optional<QColor> color,
    const QGradientStops stops, bool repeat, float angle)
    : Paint()
    , name(name)
    , color(color)
    , stops(stops)
    , repeat(repeat)
    , angle(angle)
{
}

bool LinearGradientPaint::animated() const
{
    return false;
}

QBrush LinearGradientPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    QPointF startPoint = drawingRect.bottomLeft();
    QPointF endPoint = drawingRect.topRight();
    if (angle > 90)
    {
        startPoint = drawingRect.topLeft();
        endPoint = drawingRect.bottomRight();
    }
    if (angle > 180)
    {
        startPoint = drawingRect.topRight();
        endPoint = drawingRect.bottomLeft();
    }
    if (angle > 270)
    {
        startPoint = drawingRect.bottomRight();
        endPoint = drawingRect.topLeft();
    }

    QLineF gradientAxis;
    gradientAxis.setP1(drawingRect.center());
    gradientAxis.setAngle(90.0f - angle);

    QLineF colorStartAxis;
    colorStartAxis.setP1(startPoint);
    colorStartAxis.setAngle(-angle);

    QLineF colorStopAxis;
    colorStopAxis.setP1(endPoint);
    colorStopAxis.setAngle(-angle);

    QPointF gradientStart;
    QPointF gradientEnd;
    gradientAxis.intersects(colorStartAxis, &gradientStart);
    gradientAxis.intersects(colorStopAxis, &gradientEnd);

    QLinearGradient gradient(gradientStart, gradientEnd);

    // FIX: repeating gradients need different start/end points as well
    auto spread = repeat ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    // TODO: merge with username colors if transparent
    for (auto const &[position, color] : this->stops)
    {
        gradient.setColorAt(position, color);
    }

    QBrush brush(gradient);

    return brush;
}

}  // namespace chatterino
