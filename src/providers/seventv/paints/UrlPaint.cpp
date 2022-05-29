#include "UrlPaint.hpp"

namespace chatterino {

QBrush UrlPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    return QBrush(userColor);
}

bool UrlPaint::animated() const
{
    return true;
}

}  // namespace chatterino
