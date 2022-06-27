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
    const QString name_;
    const QGradientStops stops_;
    const bool repeat_;

    std::vector<PaintDropShadow> dropShadows_;
};

}  // namespace chatterino
