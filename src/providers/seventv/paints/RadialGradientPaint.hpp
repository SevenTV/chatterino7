#pragma once

#include "Paint.hpp"

namespace chatterino {

class RadialGradientPaint : public Paint
{
public:
    RadialGradientPaint(const QString name, const QGradientStops stops,
                        const bool repeat, std::vector<PaintDropShadow>);
    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    std::vector<PaintDropShadow> getDropShadows() const override;
    bool animated() const override;

private:
    const QString name;
    const QGradientStops stops;
    const bool repeat;

    std::vector<PaintDropShadow> dropShadows;
};

}  // namespace chatterino
