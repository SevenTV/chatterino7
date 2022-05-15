#include "RadialGradientPaint.hpp"

namespace chatterino {

QBrush RadialGradientPaint::asBrush(QColor userColor)
{
    return QBrush(userColor);
}

}
