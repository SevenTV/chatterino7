#pragma once

#include "Paint.hpp"
#include "messages/Image.hpp"

namespace chatterino {

class UrlPaint : public Paint
{
public:
    UrlPaint(const QString name, const ImagePtr image);

    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    bool animated() const override;

private:
    QString name;
    ImagePtr image;
};

}  // namespace chatterino
