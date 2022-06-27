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

    effect->setXOffset(this->xOffset_);
    effect->setYOffset(this->yOffset_);
    effect->setBlurRadius(this->radius_);
    effect->setColor(this->color_);

    return effect;
}

}  // namespace chatterino
