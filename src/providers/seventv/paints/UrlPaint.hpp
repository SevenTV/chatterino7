#pragma once

#include "Paint.hpp"
#include "messages/Image.hpp"

namespace chatterino {

class UrlPaint : public Paint
{
public:
    UrlPaint(const QString name, const ImagePtr image,
             const std::vector<PaintDropShadow>);

    QBrush asBrush(QColor userColor, QRectF drawingRect) const override;
    std::vector<PaintDropShadow> getDropShadows() const override;
    bool animated() const override;

private:
    QString name_;
    ImagePtr image_;

    std::vector<PaintDropShadow> dropShadows_;
};

}  // namespace chatterino
