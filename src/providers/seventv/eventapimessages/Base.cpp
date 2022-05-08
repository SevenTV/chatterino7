#include "Base.hpp"

namespace chatterino {
EventApiMessage::EventApiMessage(QJsonObject _json)
    : json(std::move(_json))
    , actionString(this->json.value("action").toString())
{
    // TODO: magic_enum magic
    this->action = EventApiMessage::Action::INVALID;
    if (actionString == "ping")
    {
        this->action = EventApiMessage::Action::Ping;
    }
    else if (actionString == "success")
    {
        this->action = EventApiMessage::Action::Success;
    }
    else if (actionString == "update")
    {
        this->action = EventApiMessage::Action::Update;
    }
    else if (actionString == "error")
    {
        this->action = EventApiMessage::Action::Error;
    }
}
}  // namespace chatterino