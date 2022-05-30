#pragma once

#include <QBrush>

namespace chatterino {

class Paint
{
public:
    virtual QBrush asBrush(QColor userColor, QRectF drawingRect) const = 0;
    virtual bool animated() const = 0;

    virtual ~Paint(){};

protected:
    QColor overlayColors(QColor background, QColor foreground) const
    {
        auto alpha = foreground.alphaF();

        auto r = (1 - alpha) * background.red() + alpha * foreground.red();
        auto g = (1 - alpha) * background.green() + alpha * foreground.green();
        auto b = (1 - alpha) * background.blue() + alpha * foreground.blue();

        return QColor(r, g, b);
    }

    float offsetRepeatingStopPosition(float position,
                                      QGradientStops stops) const
    {
        float gradientStart = stops.first().first;
        float gradientEnd = stops.last().first;
        float gradientLength = gradientEnd - gradientStart;
        float offsetPosition = (position - gradientStart) / gradientLength;

        return offsetPosition;
    }
};

}  // namespace chatterino
