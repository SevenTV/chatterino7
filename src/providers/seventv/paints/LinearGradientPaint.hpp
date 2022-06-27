#pragma once

#include <utility>
#include "Paint.hpp"

namespace chatterino {

class LinearGradientPaint : public Paint
{
public:
    LinearGradientPaint(const QString name, const std::optional<QColor> color,
                        const QGradientStops stops, bool repeat, float angle,
                        std::vector<PaintDropShadow>);

    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    std::vector<PaintDropShadow> getDropShadows() const override;
    bool animated() const override;

private:
    QString name_;
    std::optional<QColor> color_;
    QGradientStops stops_;
    bool repeat_;
    float angle_;

    std::vector<PaintDropShadow> dropShadows_;
};

}  // namespace chatterino
