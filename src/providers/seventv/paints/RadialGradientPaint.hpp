#pragma once

#include "Paint.hpp"

namespace chatterino {

class RadialGradientPaint : public Paint
{
public:
    QBrush asBrush(QColor serColor) const override;
    bool animated() const override;
};

}  // namespace chatterino
