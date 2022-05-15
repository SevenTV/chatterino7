#pragma once

#include "Paint.hpp"

namespace chatterino {

class UrlPaint : public Paint {
public:
    QBrush asBrush(QColor userColor) const override;
};

}
