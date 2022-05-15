#include "LinearGradientPaint.hpp"

#include <boost/math/constants/constants.hpp>

namespace chatterino {

LinearGradientPaint::LinearGradientPaint(const QString name, const std::optional<QColor> color, const std::vector<std::pair<float, QColor>> stops, bool repeat, float angle)
    : Paint()
    , name(name)
    , color(color)
    , stops(stops)
    , repeat(repeat)
    , angle(angle)
{
}

QBrush LinearGradientPaint::asBrush(QColor userColor) const
{
    const auto pi = boost::math::constants::pi<double>();

    float cosRotation = std::cos(this->angle * pi / 180.0);
    float sinRotation = std::sin(this->angle * pi / 180.0);

    auto scale = repeat ? 0.25 : 1.0;

    float startX = std::clamp(sinRotation, -1.0f, 0.0f) * -0.1f;
    float startY = std::clamp(cosRotation, -1.0f, 0.0f) * -0.1f;
    QPointF startPoint = QPointF(startX, startY) * scale;

    float endX = std::clamp(sinRotation, 0.0f, 1.0f);
    float endY = std::clamp(cosRotation, 0.0f, 1.0f);
    QPointF endPoint = QPointF(startX, startY) * scale;

    QLinearGradient gradient(startPoint, endPoint);

    gradient.setCoordinateMode(QGradient::ObjectMode);

    auto spread = repeat ? QGradient::RepeatSpread : QGradient::PadSpread;
    gradient.setSpread(spread);

    auto baseColor = color.value_or(userColor);
    for (auto const &[position, color] : this->stops)
    {
        gradient.setColorAt(position, color);
    }

    QBrush brush(gradient);

    return brush;
}

}
