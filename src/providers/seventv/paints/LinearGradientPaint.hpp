#pragma once

#include "Paint.hpp"

namespace chatterino {

class LinearGradientPaint : public Paint {
public:
    QBrush asBrush();
};

}
