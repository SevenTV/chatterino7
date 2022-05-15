#pragma once

namespace chatterino {

class Paint {
public:
    virtual QBrush asBrush(QColor userColor) = 0;

    virtual ~Paint() {};
};

}
