#include "PaintDropShadow.hpp"

namespace chatterino {

PaintDropShadow::PaintDropShadow(const float xOffset, const float yOffset,
                                 const float radius, const QColor color)
    : xOffset_(xOffset)
    , yOffset_(yOffset)
    , radius_(radius)
    , color_(color)
{
}

QGraphicsDropShadowEffect *PaintDropShadow::getGraphicsEffect() const
{
    auto effect = new QGraphicsDropShadowEffect();

    effect->setXOffset(xOffset_);
    effect->setYOffset(yOffset_);
    effect->setBlurRadius(radius_);
    effect->setColor(color_);

    return effect;
}

}  // namespace chatterino
