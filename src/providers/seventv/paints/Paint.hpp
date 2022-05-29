#pragma once

#include <QBrush>

namespace chatterino {

class Paint
{
public:
    virtual QBrush asBrush(QColor userColor, QRectF drawingRect) const = 0;
    virtual bool animated() const = 0;

    virtual ~Paint(){};
};

}  // namespace chatterino
