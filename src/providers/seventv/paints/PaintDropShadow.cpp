#include "providers/seventv/paints/PaintDropShadow.hpp"

#include <private/qpixmapfilter_p.h>

namespace chatterino {

PaintDropShadow::PaintDropShadow(float xOffset, float yOffset, float radius,
                                 QColor color)
    : xOffset_(xOffset)
    , yOffset_(yOffset)
    , radius_(radius)
    , color_(color)
{
}

bool PaintDropShadow::isValid() const
{
    return radius_ > 0;
}

PaintDropShadow PaintDropShadow::scaled(float scale) const
{
    return {this->xOffset_ * scale, this->yOffset_ * scale,
            this->radius_ * scale, this->color_};
}

void PaintDropShadow::apply(QPixmapDropShadowFilter &effect) const
{
    effect.setOffset({this->xOffset_, this->yOffset_});
    // Multiplied by 3 to match the appearance from the extension.
    // Best value found through manual testing.
    effect.setBlurRadius(this->radius_ * 3);
    effect.setColor(this->color_);
}

}  // namespace chatterino
