#pragma once

#include "Paint.hpp"

namespace chatterino {

class UrlPaint : public Paint
{
public:
    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    bool animated() const override;
};

}  // namespace chatterino
