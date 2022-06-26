#include "providers/seventv/paints/Paint.hpp"

#include "Application.hpp"
#include "singletons/Theme.hpp"

#include <QLabel>
#include <QPainter>

namespace chatterino {

QPixmap Paint::getPixmap(QString text, QFont font, QColor userColor,
                         QSize size)
{
    QPixmap buffer(size);
    buffer.fill(Qt::transparent);

    QPainter bufferPainter(&buffer);
    bufferPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    bufferPainter.setFont(font);

    // NOTE: draw colon separately from the nametag
    // otherwise the paint would extend onto the colon
    bool drawColon = false;
    QRectF nametagBoundingRect = buffer.rect();
    QString nametagText = text;
    if (nametagText.endsWith(':'))
    {
        drawColon = true;
        nametagText = nametagText.chopped(1);
        nametagBoundingRect = bufferPainter.boundingRect(
            QRectF(0, 0, 10000, 10000), nametagText,
            QTextOption(Qt::AlignLeft | Qt::AlignTop));
    }

    QPen paintPen;
    QBrush paintBrush = this->asBrush(userColor, nametagBoundingRect);
    paintPen.setBrush(paintBrush);
    bufferPainter.setPen(paintPen);

    bufferPainter.drawText(nametagBoundingRect, nametagText,
                           QTextOption(Qt::AlignLeft | Qt::AlignTop));
    bufferPainter.end();

    for (const auto &shadow : this->getDropShadows())
    {
        // HACK: create a QLabel from the pixmap to apply drop shadows
        QLabel *label = new QLabel();
        label->setPixmap(buffer);
        label->setGraphicsEffect(shadow.getGraphicsEffect());

        buffer = label->grab();
    }

    if (drawColon)
    {
        auto colonColor = getApp()->getThemes()->messages.textColors.regular;

        bufferPainter.begin(&buffer);

        bufferPainter.setPen(QPen(colonColor));
        bufferPainter.setFont(font);

        QRectF colonBoundingRect(nametagBoundingRect.right(), 0, 10000, 10000);
        bufferPainter.drawText(colonBoundingRect, ":",
                               QTextOption(Qt::AlignLeft | Qt::AlignTop));
        bufferPainter.end();
    }

    return buffer;
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
