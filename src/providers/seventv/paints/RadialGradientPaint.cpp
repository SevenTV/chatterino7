#include "RadialGradientPaint.hpp"

namespace chatterino {

QBrush RadialGradientPaint::asBrush(QColor userColor) const
{
    return QBrush(userColor);
}

}
