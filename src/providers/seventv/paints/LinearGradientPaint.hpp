#pragma once

#include <utility>
#include "Paint.hpp"

namespace chatterino {

class LinearGradientPaint : public Paint
{
public:
    LinearGradientPaint(const QString name, const std::optional<QColor> color,
                        const std::vector<std::pair<float, QColor>> stops,
                        bool repeat, float angle);

    QBrush asBrush(QColor userColor) const override;

private:
    QString name;
    std::optional<QColor> color;
    std::vector<std::pair<float, QColor>> stops;
    bool repeat;
    float angle;
};

}  // namespace chatterino
