#include "RadialGradientPaint.hpp"
#include <algorithm>

namespace chatterino {

RadialGradientPaint::RadialGradientPaint(const QString name,
                                         const QGradientStops stops,
                                         const bool repeat)
    : Paint()
    , name(name)
    , stops(stops)
    , repeat(repeat)
{
}

QBrush RadialGradientPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    double x = drawingRect.x() + drawingRect.width() / 2;
    double y = drawingRect.y() + drawingRect.height() / 2;

    double radius = std::max(drawingRect.width(), drawingRect.height()) / 2;
    radius = repeat ? radius * this->stops.back().first : radius;

    QRadialGradient gradient(x, y, radius);

    auto spread = repeat ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    for (const auto &[position, color] : this->stops)
    {
        auto combinedColor = this->overlayColors(userColor, color);
        float offsetPosition =
            this->repeat ? offsetRepeatingStopPosition(position, this->stops)
                         : position;

        gradient.setColorAt(offsetPosition, combinedColor);
    }

    return QBrush(gradient);
}

bool RadialGradientPaint::animated() const
{
    return false;
}

}  // namespace chatterino
