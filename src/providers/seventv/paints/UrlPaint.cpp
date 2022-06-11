#include "UrlPaint.hpp"

namespace chatterino {

UrlPaint::UrlPaint(const QString name, const ImagePtr image)
    : Paint()
    , name(name)
    , image(image)
{
}

bool UrlPaint::animated() const
{
    return image->animated();
}

QBrush UrlPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    // FIX: displaying animated text will need more work inside of chatterinos rendering
    // if (auto pixmap = this->image->pixmapOrLoad())
    // {
    //     return QBrush(pixmap->scaled(drawingRect.size().toSize(),
    //                                  Qt::IgnoreAspectRatio,
    //                                  Qt::SmoothTransformation));
    //     return QBrush(*pixmap);
    // }

    return QBrush(userColor);
}

}  // namespace chatterino
