#include "PaintDropShadow.hpp"

namespace chatterino {

PaintDropShadow::PaintDropShadow(const float xOffset, const float yOffset,
                                 const float radius, const QColor color)
    : xOffset(xOffset)
    , yOffset(yOffset)
    , radius(radius)
    , color(color)
{
}

QGraphicsDropShadowEffect *PaintDropShadow::getGraphicsEffect() const
{
    auto effect = new QGraphicsDropShadowEffect();

    effect->setXOffset(xOffset);
    effect->setYOffset(yOffset);
    effect->setBlurRadius(radius);
    effect->setColor(color);

    return effect;
}

}  // namespace chatterino
