#include "UrlPaint.hpp"

namespace chatterino {

QBrush UrlPaint::asBrush(QColor userColor) const
{
    return QBrush(userColor);
}

}
