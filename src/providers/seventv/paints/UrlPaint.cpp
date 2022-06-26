#include "UrlPaint.hpp"

#include <QPainter>

namespace chatterino {

UrlPaint::UrlPaint(const QString name, const ImagePtr image,
                   const std::vector<PaintDropShadow> dropShadows)
    : Paint()
    , name(name)
    , image(image)
    , dropShadows(dropShadows)
{
}

bool UrlPaint::animated() const
{
    return image->animated();
}

QBrush UrlPaint::asBrush(QColor userColor, QRectF drawingRect) const
{
    if (auto paintPixmap = this->image->pixmapOrLoad())
    {
        paintPixmap = paintPixmap->scaledToWidth(drawingRect.width());

        QPixmap userColorPixmap = QPixmap(paintPixmap->size());
        userColorPixmap.fill(userColor);

        QPainter painter(&userColorPixmap);
        painter.drawPixmap(0, 0, *paintPixmap);

        QPixmap combinedPixmap = userColorPixmap.copy(
            QRect(0, 0, drawingRect.width(), drawingRect.height()));
        return QBrush(combinedPixmap);
    }

    return QBrush(userColor);
}

std::vector<PaintDropShadow> UrlPaint::getDropShadows() const
{
    return dropShadows;
}

}  // namespace chatterino
