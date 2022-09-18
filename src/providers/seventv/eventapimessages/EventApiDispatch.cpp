#include "EventApiDispatch.hpp"

namespace chatterino {
EventApiDispatch::EventApiDispatch(QJsonObject obj)
    : body(obj["body"].toObject())
    , id(this->body["id"].toString())
    , actorName(this->body["actor"].toObject()["display_name"].toString())
{
    auto subType = magic_enum::enum_cast<SeventvEventApiSubscriptionType>(
        obj["type"].toString().toStdString());
    if (subType.has_value())
    {
        this->type = subType.value();
    }
}

EventApiEmoteAddDispatch::EventApiEmoteAddDispatch(
    const EventApiDispatch &dispatch, QJsonObject emote)
    : emoteSetId(dispatch.id)
    , actorName(dispatch.actorName)
    , emoteJson(emote)
    , emoteId(this->emoteJson["id"].toString())
{
}

EventApiEmoteRemoveDispatch::EventApiEmoteRemoveDispatch(
    const EventApiDispatch &dispatch, QJsonObject emote)
    : emoteSetId(dispatch.id)
    , actorName(dispatch.actorName)
    , emoteId(emote["id"].toString())
    , emoteName(emote["name"].toString())
{
}

EventApiEmoteUpdateDispatch::EventApiEmoteUpdateDispatch(
    const EventApiDispatch &dispatch, QJsonObject changeField)
    : emoteSetId(dispatch.id)
    , actorName(dispatch.actorName)
{
    auto oldValue = changeField["old_value"].toObject();
    auto value = changeField["value"].toObject();
    this->emoteId = value["id"].toString();
    this->emoteName = value["name"].toString();
    this->oldEmoteName = oldValue["name"].toString();
}
}  // namespace chatterino
