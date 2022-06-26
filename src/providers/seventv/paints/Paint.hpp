#pragma once

#include <QBrush>
#include <QFont>
#include <vector>
#include "providers/seventv/paints/PaintDropShadow.hpp"

namespace chatterino {

class Paint
{
public:
    virtual QBrush asBrush(QColor userColor, QRectF drawingRect) const = 0;
    virtual std::vector<PaintDropShadow> getDropShadows() const = 0;
    virtual bool animated() const = 0;

    QPixmap getPixmap(QString text, QFont font, QColor userColor, QSize size);

    virtual ~Paint(){};

protected:
    QColor overlayColors(QColor background, QColor foreground) const;
    float offsetRepeatingStopPosition(float position,
                                      QGradientStops stops) const;
};

}  // namespace chatterino
