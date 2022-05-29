#include "LinearGradientPaint.hpp"

namespace chatterino {

LinearGradientPaint::LinearGradientPaint(const QString name,
                                         const std::optional<QColor> color,
                                         const QGradientStops stops,
                                         bool repeat, float angle)
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

    if (this->repeat)
    {
        QLineF gradientLine(gradientStart, gradientEnd);
        gradientStart = gradientLine.pointAt(this->stops.front().first);
        gradientEnd = gradientLine.pointAt(this->stops.back().first);
    }

    QLinearGradient gradient(gradientStart, gradientEnd);

    auto spread = repeat ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    for (auto const &[position, color] : this->stops)
    {
        auto combinedColor = this->overlayColors(userColor, color);

        gradient.setColorAt(
            this->translateRepeatingStop(position, this->stops.front().first,
                                         this->stops.back().first),
            combinedColor);
    }

    QBrush brush(gradient);

    return brush;
}

QColor LinearGradientPaint::overlayColors(QColor background,
                                          QColor foreground) const
{
    auto alpha = foreground.alphaF();

    auto r = (1 - alpha) * background.red() + alpha * foreground.red();
    auto g = (1 - alpha) * background.green() + alpha * foreground.green();
    auto b = (1 - alpha) * background.blue() + alpha * foreground.blue();

    return QColor(r, g, b);
}

float LinearGradientPaint::translateRepeatingStop(float stop,
                                                  float gradientStart,
                                                  float gradientEnd) const
{
    if (this->repeat)
    {
        float gradientLength = gradientEnd - gradientStart;
        return (stop - gradientStart) / gradientLength;
    }
    else
    {
        return stop;
    }
}

}  // namespace chatterino
