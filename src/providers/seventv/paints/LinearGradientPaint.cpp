#include "LinearGradientPaint.hpp"

namespace chatterino {

LinearGradientPaint::LinearGradientPaint(
    const QString name, const std::optional<QColor> color,
    const QGradientStops stops, bool repeat, float angle,
    std::vector<PaintDropShadow> dropShadows)
    : Paint()
    , name_(name)
    , color_(color)
    , stops_(stops)
    , repeat_(repeat)
    , angle_(angle)
    , dropShadows_(dropShadows)
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
    if (this->angle_ > 90)
    {
        startPoint = drawingRect.topLeft();
        endPoint = drawingRect.bottomRight();
    }
    if (this->angle_ > 180)
    {
        startPoint = drawingRect.topRight();
        endPoint = drawingRect.bottomLeft();
    }
    if (this->angle_ > 270)
    {
        startPoint = drawingRect.bottomRight();
        endPoint = drawingRect.topLeft();
    }

    QLineF gradientAxis;
    gradientAxis.setP1(drawingRect.center());
    gradientAxis.setAngle(90.0f - this->angle_);

    QLineF colorStartAxis;
    colorStartAxis.setP1(startPoint);
    colorStartAxis.setAngle(-this->angle_);

    QLineF colorStopAxis;
    colorStopAxis.setP1(endPoint);
    colorStopAxis.setAngle(-this->angle_);

    QPointF gradientStart;
    QPointF gradientEnd;
    gradientAxis.intersects(colorStartAxis, &gradientStart);
    gradientAxis.intersects(colorStopAxis, &gradientEnd);

    if (this->repeat_)
    {
        QLineF gradientLine(gradientStart, gradientEnd);
        gradientStart = gradientLine.pointAt(this->stops_.front().first);
        gradientEnd = gradientLine.pointAt(this->stops_.back().first);
    }

    QLinearGradient gradient(gradientStart, gradientEnd);

    auto spread = this->repeat_ ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    for (auto const &[position, color] : this->stops_)
    {
        auto combinedColor = this->overlayColors(userColor, color);
        float offsetPosition = this->repeat_ ? this->offsetRepeatingStopPosition(
                                                  position, this->stops_)
                                            : position;
        gradient.setColorAt(offsetPosition, combinedColor);
    }

    return QBrush(gradient);
}

std::vector<PaintDropShadow> LinearGradientPaint::getDropShadows() const
{
    return this->dropShadows_;
}

}  // namespace chatterino
