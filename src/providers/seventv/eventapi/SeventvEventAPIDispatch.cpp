#include "providers/seventv/eventapi/SeventvEventAPIDispatch.hpp"

#include <utility>

namespace chatterino {

SeventvEventAPIDispatch::SeventvEventAPIDispatch(QJsonObject obj)
    : type(magic_enum::enum_cast<SeventvEventAPISubscriptionType>(
               obj["type"].toString().toStdString())
               .value_or(SeventvEventAPISubscriptionType::INVALID))
    , body(obj["body"].toObject())
    , id(this->body["id"].toString())
    , actorName(this->body["actor"].toObject()["display_name"].toString())
{
}

SeventvEventAPIEmoteAddDispatch::SeventvEventAPIEmoteAddDispatch(
    const SeventvEventAPIDispatch &dispatch, QJsonObject emote)
    : emoteSetID(dispatch.id)
    , actorName(dispatch.actorName)
    , emoteJson(std::move(emote))
    , emoteID(this->emoteJson["id"].toString())
{
}

bool SeventvEventAPIEmoteAddDispatch::validate() const
{
    bool validValues =
        !this->emoteSetID.isEmpty() && !this->emoteJson.isEmpty();
    if (!validValues)
    {
        return false;
    }
    bool validActiveEmote = this->emoteJson.contains("id") &&
                            this->emoteJson.contains("name") &&
                            this->emoteJson.contains("data");
    if (!validActiveEmote)
    {
        return false;
    }
    auto emoteData = this->emoteJson["data"].toObject();
    return emoteData.contains("name") && emoteData.contains("host") &&
           emoteData.contains("owner");
}

SeventvEventAPIEmoteRemoveDispatch::SeventvEventAPIEmoteRemoveDispatch(
    const SeventvEventAPIDispatch &dispatch, QJsonObject emote)
    : emoteSetID(dispatch.id)
    , actorName(dispatch.actorName)
    , emoteName(emote["name"].toString())
    , emoteID(emote["id"].toString())
{
}

bool SeventvEventAPIEmoteRemoveDispatch::validate() const
{
    return !this->emoteSetID.isEmpty() && !this->emoteName.isEmpty() &&
           !this->emoteID.isEmpty();
}

SeventvEventAPIEmoteUpdateDispatch::SeventvEventAPIEmoteUpdateDispatch(
    const SeventvEventAPIDispatch &dispatch, QJsonObject oldValue,
    QJsonObject value)
    : emoteSetID(dispatch.id)
    , actorName(dispatch.actorName)
    , emoteID(value["id"].toString())
    , oldEmoteName(oldValue["name"].toString())
    , emoteName(value["name"].toString())
{
}

bool SeventvEventAPIEmoteUpdateDispatch::validate() const
{
    return !this->emoteSetID.isEmpty() && !this->emoteID.isEmpty() &&
           !this->oldEmoteName.isEmpty() && !this->emoteName.isEmpty() &&
           this->oldEmoteName != this->emoteName;
}

SeventvEventAPIUserConnectionUpdateDispatch::
    SeventvEventAPIUserConnectionUpdateDispatch(
        const SeventvEventAPIDispatch &dispatch, const QJsonObject &update,
        size_t connectionIndex)
    : userID(dispatch.id)
    , actorName(dispatch.actorName)
    , oldEmoteSetID(update["old_value"].toObject()["id"].toString())
    , emoteSetID(update["value"].toObject()["id"].toString())
    , connectionIndex(connectionIndex)
{
}

bool SeventvEventAPIUserConnectionUpdateDispatch::validate() const
{
    return !this->userID.isEmpty() && !this->oldEmoteSetID.isEmpty() &&
           !this->emoteSetID.isEmpty();
}

SeventvEventAPICosmeticCreateDispatch::SeventvEventAPICosmeticCreateDispatch(
    const SeventvEventAPIDispatch &dispatch)
    : data(dispatch.body["object"].toObject()["data"].toObject())
    , kind(magic_enum::enum_cast<SeventvCosmeticKind>(dispatch.body["object"]
                                                          .toObject()["kind"]
                                                          .toString()
                                                          .toStdString())
               .value_or(SeventvCosmeticKind::INVALID))
{
}

bool SeventvEventAPICosmeticCreateDispatch::validate() const
{
    return !this->data.empty() && this->kind != SeventvCosmeticKind::INVALID;
}

SeventvEventAPIEntitlementCreateDispatch::
    SeventvEventAPIEntitlementCreateDispatch(
        const SeventvEventAPIDispatch &dispatch)
{
    const auto obj = dispatch.body["object"].toObject();
    this->userID = QString();
    this->refID = obj["ref_id"].toString();
    this->kind = magic_enum::enum_cast<SeventvCosmeticKind>(
                     obj["kind"].toString().toStdString())
                     .value_or(SeventvCosmeticKind::INVALID);

    const auto userConnections =
        obj["user"].toObject()["connections"].toArray();
    for (const auto &connectionJson : userConnections)
    {
        const auto connection = connectionJson.toObject();
        if (connection["platform"].toString() == "TWITCH")
        {
            this->userID = connection["id"].toString();
            this->userName = connection["username"].toString();
            break;
        }
    }
}

bool SeventvEventAPIEntitlementCreateDispatch::validate() const
{
    return !this->userID.isEmpty() && !this->userName.isEmpty() &&
           !this->refID.isEmpty() && this->kind != SeventvCosmeticKind::INVALID;
}

}  // namespace chatterino
