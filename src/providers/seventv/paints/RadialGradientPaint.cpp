#include "RadialGradientPaint.hpp"

namespace chatterino {

QBrush RadialGradientPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    return QBrush(userColor);
}

bool RadialGradientPaint::animated() const
{
    return false;
}

}  // namespace chatterino
