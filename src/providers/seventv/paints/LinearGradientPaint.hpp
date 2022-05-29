#pragma once

#include <utility>
#include "Paint.hpp"

namespace chatterino {

class LinearGradientPaint : public Paint
{
public:
    LinearGradientPaint(const QString name, const std::optional<QColor> color,
                        const QGradientStops stops, bool repeat, float angle);

    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    bool animated() const override;

private:
    QColor overlayColors(QColor background, QColor foreground) const;
    float translateRepeatingStop(float stop, float gradientStart,
                                 float gradientEnd) const;

    QString name;
    std::optional<QColor> color;
    QGradientStops stops;
    bool repeat;
    float angle;
};

}  // namespace chatterino
