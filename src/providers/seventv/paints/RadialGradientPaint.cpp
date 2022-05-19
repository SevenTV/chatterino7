#include "RadialGradientPaint.hpp"

namespace chatterino {

QBrush RadialGradientPaint::asBrush(QColor userColor) const
{
    return QBrush(userColor);
}

bool RadialGradientPaint::animated() const
{
    return false;
}

}  // namespace chatterino
