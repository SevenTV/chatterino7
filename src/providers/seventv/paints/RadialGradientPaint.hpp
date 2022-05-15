#pragma once

#include "Paint.hpp"

namespace chatterino {

class RadialGradientPaint : public Paint {
public:
    QBrush asBrush();
};

}
