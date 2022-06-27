#include "RadialGradientPaint.hpp"
#include <algorithm>

namespace chatterino {

RadialGradientPaint::RadialGradientPaint(
    const QString name, const QGradientStops stops, const bool repeat,
    std::vector<PaintDropShadow> dropShadows)
    : Paint()
    , name_(name)
    , stops_(stops)
    , repeat_(repeat)
    , dropShadows_(dropShadows)
{
}

QBrush RadialGradientPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    double x = drawingRect.x() + drawingRect.width() / 2;
    double y = drawingRect.y() + drawingRect.height() / 2;

    double radius = std::max(drawingRect.width(), drawingRect.height()) / 2;
    radius = this->repeat_ ? radius * this->stops_.back().first : radius;

    QRadialGradient gradient(x, y, radius);

    auto spread = this->repeat_ ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    for (const auto &[position, color] : this->stops_)
    {
        auto combinedColor = this->overlayColors(userColor, color);
        float offsetPosition =
            this->repeat_ ? this->offsetRepeatingStopPosition(position, this->stops_)
                         : position;

        gradient.setColorAt(offsetPosition, combinedColor);
    }

    return QBrush(gradient);
}

bool RadialGradientPaint::animated() const
{
    return false;
}

std::vector<PaintDropShadow> RadialGradientPaint::getDropShadows() const
{
    return this->dropShadows_;
}

}  // namespace chatterino
