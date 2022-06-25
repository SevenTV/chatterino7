#pragma once

#include <QGraphicsDropShadowEffect>

namespace chatterino {

class PaintDropShadow
{
public:
    PaintDropShadow(const float xOffset, const float yOffset,
                    const float radius, const QColor color);

    QGraphicsDropShadowEffect *getGraphicsEffect() const;

private:
    const float xOffset;
    const float yOffset;
    const float radius;
    const QColor color;
};

}  // namespace chatterino
