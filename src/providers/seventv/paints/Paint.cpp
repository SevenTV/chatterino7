#include "providers/seventv/paints/Paint.hpp"

#include "Application.hpp"
#include "singletons/Theme.hpp"

#include <QLabel>
#include <QPainter>

namespace chatterino {

QPixmap Paint::getPixmap(QString text, QFont font, QColor userColor, QSize size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter pixmapPainter(&pixmap);
    pixmapPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    pixmapPainter.setFont(font);

    // NOTE: draw colon separately from the nametag
    // otherwise the paint would extend onto the colon
    bool drawColon = false;
    QRectF nametagBoundingRect = pixmap.rect();
    QString nametagText = text;
    if (nametagText.endsWith(':'))
    {
        drawColon = true;
        nametagText = nametagText.chopped(1);
        nametagBoundingRect = pixmapPainter.boundingRect(
            QRectF(0, 0, 10000, 10000), nametagText,
            QTextOption(Qt::AlignLeft | Qt::AlignTop));
    }

    QPen pen;
    QBrush brush = this->asBrush(userColor, nametagBoundingRect);
    pen.setBrush(brush);
    pixmapPainter.setPen(pen);

    pixmapPainter.drawText(nametagBoundingRect, nametagText,
                           QTextOption(Qt::AlignLeft | Qt::AlignTop));
    pixmapPainter.end();

    for (const auto &shadow : this->getDropShadows())
    {
        // HACK: create a QLabel from the pixmap to apply drop shadows
        QLabel *label = new QLabel();
        label->setPixmap(pixmap);
        label->setGraphicsEffect(shadow.getGraphicsEffect());

        pixmap = label->grab();
    }

    if (drawColon)
    {
        auto colonColor = getApp()->getThemes()->messages.textColors.regular;

        pixmapPainter.begin(&pixmap);

        pixmapPainter.setPen(QPen(colonColor));
        pixmapPainter.setFont(font);

        QRectF colonBoundingRect(nametagBoundingRect.right(), 0, 10000, 10000);
        pixmapPainter.drawText(colonBoundingRect, ":",
                               QTextOption(Qt::AlignLeft | Qt::AlignTop));
        pixmapPainter.end();
    }

    return pixmap;
}

QColor Paint::overlayColors(QColor background, QColor foreground) const
{
    auto alpha = foreground.alphaF();

    auto r = (1 - alpha) * background.red() + alpha * foreground.red();
    auto g = (1 - alpha) * background.green() + alpha * foreground.green();
    auto b = (1 - alpha) * background.blue() + alpha * foreground.blue();

    return QColor(r, g, b);
}

float Paint::offsetRepeatingStopPosition(float position,
                                         QGradientStops stops) const
{
    float gradientStart = stops.first().first;
    float gradientEnd = stops.last().first;
    float gradientLength = gradientEnd - gradientStart;
    float offsetPosition = (position - gradientStart) / gradientLength;

    return offsetPosition;
}

}  // namespace chatterino
