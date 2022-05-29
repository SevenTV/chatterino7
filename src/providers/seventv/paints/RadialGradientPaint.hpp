#pragma once

#include "Paint.hpp"

namespace chatterino {

class RadialGradientPaint : public Paint
{
public:
    RadialGradientPaint(const QString name, const QGradientStops stops, const bool repeat);
    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    bool animated() const override;

private:
    const QString name;
    const QGradientStops stops;
    const bool repeat;
};

}  // namespace chatterino
