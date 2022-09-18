#include "providers/seventv/eventapimessages/EventApiMessage.hpp"

namespace chatterino {
EventApiMessage::EventApiMessage(QJsonObject _json)
    : data(_json["d"].toObject())
    , op(SeventvEventApiOpcode(_json["op"].toInt()))
{
}
}  // namespace chatterino
