#pragma once

namespace chatterino {

class Paint {
public:
    virtual QBrush asBrush() = 0;

    virtual ~Paint() {};
};

}
