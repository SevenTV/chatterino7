#include "providers/seventv/paints/Paint.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"

#include <private/qpixmapfilter_p.h>
#include <QLabel>
#include <QPainter>

namespace {
struct CacheEntry {
    size_t hash = 0;
    int offset = 0;
    QPixmap pix;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::array<std::optional<CacheEntry>, 8> CACHE;

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
bool getFromCache(size_t hash, int offset, QPixmap *pix)
{
    auto &it = CACHE[hash % CACHE.size()];
    if (!it || it->hash != hash || it->offset != offset)
    {
        return false;
    }
    *pix = it->pix;
    return true;
}

void putToCache(size_t hash, int offset, const QPixmap &pix)
{
    CACHE[hash % CACHE.size()] = CacheEntry{
        .hash = hash,
        .offset = offset,
        .pix = pix,
    };
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

}  // namespace

namespace chatterino {

using namespace literals;

QPixmap Paint::getPixmap(const QString &text, const QFont &font,
                         QColor userColor, QSize size, float scale,
                         float dpr) const
{
    int durationOffset = this->durationOffset();
    size_t hash =
        qHashMulti(0, this->id, text, font, userColor.rgba(), size, scale, dpr);
    QPixmap pixmap;
    if (getFromCache(hash, durationOffset, &pixmap))
    {
        return pixmap;
    }
    pixmap = QPixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
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
    const QBrush brush = this->asBrush(userColor, nametagBoundingRect);
    pen.setBrush(brush);
    pixmapPainter.setPen(pen);

    pixmapPainter.drawText(nametagBoundingRect, nametagText,
                           QTextOption(Qt::AlignLeft | Qt::AlignTop));
    pixmapPainter.end();

    if (!this->getDropShadows().empty() &&
        getSettings()->displaySevenTVPaintShadows)
    {
        QPixmap outMap(size * dpr);
        outMap.setDevicePixelRatio(dpr);
        for (const auto &shadow : this->getDropShadows())
        {
            if (!shadow.isValid())
            {
                continue;
            }
            outMap.fill(Qt::transparent);

            {
                QPainter outPainter(&outMap);
                auto scaled = shadow.scaled(
                    scale / static_cast<float>(outMap.devicePixelRatio()));

                QPixmapDropShadowFilter filter;
                scaled.apply(filter);
                filter.draw(&outPainter, {0, 0}, pixmap);
            }
            outMap.swap(pixmap);
        }
    }

    if (drawColon)
    {
        auto colonColor = getApp()->getThemes()->messages.textColors.regular;

        pixmapPainter.begin(&pixmap);

        pixmapPainter.setPen(QPen(colonColor));
        pixmapPainter.setFont(font);

        QRectF colonBoundingRect(nametagBoundingRect.right(), 0, 10000, 10000);
        pixmapPainter.drawText(colonBoundingRect, u":"_s,
                               QTextOption(Qt::AlignLeft | Qt::AlignTop));
        pixmapPainter.end();
    }

    putToCache(hash, durationOffset, pixmap);

    return pixmap;
}

QColor Paint::overlayColors(QColor background, QColor foreground)
{
    auto alpha = foreground.alphaF();

    auto r = ((1 - alpha) * static_cast<float>(background.red())) +
             (alpha * static_cast<float>(foreground.red()));
    auto g = ((1 - alpha) * static_cast<float>(background.green())) +
             (alpha * static_cast<float>(foreground.green()));
    auto b = ((1 - alpha) * static_cast<float>(background.blue())) +
             (alpha * static_cast<float>(foreground.blue()));

    return {static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)};
}

qreal Paint::offsetRepeatingStopPosition(const qreal position,
                                         const QGradientStops &stops)
{
    const qreal gradientStart = stops.first().first;
    const qreal gradientEnd = stops.last().first;
    const qreal gradientLength = gradientEnd - gradientStart;
    const qreal offsetPosition = (position - gradientStart) / gradientLength;

    return offsetPosition;
}

}  // namespace chatterino
