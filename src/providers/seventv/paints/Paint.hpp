#pragma once

#include <QBrush>

namespace chatterino {

class Paint
{
public:
    virtual QBrush asBrush(QColor userColor) const = 0;

    virtual ~Paint(){};
};

}  // namespace chatterino
