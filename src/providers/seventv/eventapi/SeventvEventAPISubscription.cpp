#include "providers/seventv/eventapi/SeventvEventAPISubscription.hpp"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include <tuple>
#include <utility>

namespace {

using namespace chatterino;

const char *typeToString(SeventvEventAPISubscriptionType type)
{
    return magic_enum::enum_name(type).data();
}

QJsonObject createDataJson(const char *typeName,
                           const SeventvEventAPICondition &condition)
{
    QJsonObject data;
    data["type"] = typeName;
    data["condition"] = std::visit(
        [](const auto &c) {
            return c.encode();
        },
        condition);
    return data;
}

}  // namespace

namespace chatterino {

bool SeventvEventAPISubscription::operator==(
    const SeventvEventAPISubscription &rhs) const
{
    return std::tie(this->condition, this->type) ==
           std::tie(rhs.condition, rhs.type);
}

bool SeventvEventAPISubscription::operator!=(
    const SeventvEventAPISubscription &rhs) const
{
    return !(rhs == *this);
}

QByteArray SeventvEventAPISubscription::encodeSubscribe() const
{
    const auto *typeName = typeToString(this->type);
    QJsonObject root;
    root["op"] = (int)SeventvEventAPIOpcode::Subscribe;
    root["d"] = createDataJson(typeName, this->condition);
    qDebug() << root;
    return QJsonDocument(root).toJson();
}

QByteArray SeventvEventAPISubscription::encodeUnsubscribe() const
{
    const auto *typeName = typeToString(this->type);
    QJsonObject root;
    root["op"] = (int)SeventvEventAPIOpcode::Unsubscribe;
    root["d"] = createDataJson(typeName, this->condition);
    return QJsonDocument(root).toJson();
}

QDebug &operator<<(QDebug &dbg, const SeventvEventAPISubscription &subscription)
{
    std::visit(
        [&](const auto &cond) {
            dbg << "SeventvEventAPISubscription{ condition:" << cond
                << "type:" << magic_enum::enum_name(subscription.type).data()
                << '}';
        },
        subscription.condition);
    return dbg;
}

SeventvEventAPIObjectIDCondition::SeventvEventAPIObjectIDCondition(
    QString objectID)
    : objectID(std::move(objectID))
{
}

QJsonObject SeventvEventAPIObjectIDCondition::encode() const
{
    QJsonObject obj;
    obj["object_id"] = this->objectID;

    return obj;
}

bool SeventvEventAPIObjectIDCondition::operator==(
    const SeventvEventAPIObjectIDCondition &rhs) const
{
    return this->objectID == rhs.objectID;
}

bool SeventvEventAPIObjectIDCondition::operator!=(
    const SeventvEventAPIObjectIDCondition &rhs) const
{
    return !(*this == rhs);
}

QDebug &operator<<(QDebug &dbg,
                   const SeventvEventAPIObjectIDCondition &condition)
{
    dbg << "{ objectID:" << condition.objectID << "}";
    return dbg;
}

SeventvEventAPIChannelCondition::SeventvEventAPIChannelCondition(
    QString twitchID)
    : twitchID(std::move(twitchID))
{
}

QJsonObject SeventvEventAPIChannelCondition::encode() const
{
    QJsonObject obj;
    obj["ctx"] = "channel";
    obj["platform"] = "TWITCH";
    obj["id"] = this->twitchID;
    return obj;
}

QDebug &operator<<(QDebug &dbg,
                   const SeventvEventAPIChannelCondition &condition)
{
    dbg << "{ twitchID:" << condition.twitchID << '}';
    return dbg;
}

bool SeventvEventAPIChannelCondition::operator==(
    const SeventvEventAPIChannelCondition &rhs) const
{
    return this->twitchID == rhs.twitchID;
}

bool SeventvEventAPIChannelCondition::operator!=(
    const SeventvEventAPIChannelCondition &rhs) const
{
    return !(*this == rhs);
}

}  // namespace chatterino
