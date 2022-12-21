#pragma once

#include <magic_enum.hpp>
#include <QByteArray>
#include <QHash>
#include <QString>

#include <variant>

namespace chatterino {

// https://github.com/SevenTV/EventAPI/tree/ca4ff15cc42b89560fa661a76c5849047763d334#subscription-types
enum class SeventvEventAPISubscriptionType {
    UpdateEmoteSet,
    UpdateUser,

    AnyCosmetic,
    CreateCosmetic,
    UpdateCosmetic,
    DeleteCosmetic,

    AnyEntitlement,
    CreateEntitlement,
    UpdateEntitlement,
    DeleteEntitlement,

    INVALID,
};

// https://github.com/SevenTV/EventAPI/tree/ca4ff15cc42b89560fa661a76c5849047763d334#opcodes
enum class SeventvEventAPIOpcode {
    Dispatch = 0,
    Hello = 1,
    Heartbeat = 2,
    Reconnect = 4,
    Ack = 5,
    Error = 6,
    EndOfStream = 7,
    Identify = 33,
    Resume = 34,
    Subscribe = 35,
    Unsubscribe = 36,
    Signal = 37,
};

struct SeventvEventAPIObjectIDCondition {
    SeventvEventAPIObjectIDCondition(QString objectID);

    QString objectID;

    QJsonObject encode() const;

    friend QDebug &operator<<(
        QDebug &dbg, const SeventvEventAPIObjectIDCondition &subscription);
    bool operator==(const SeventvEventAPIObjectIDCondition &rhs) const;
    bool operator!=(const SeventvEventAPIObjectIDCondition &rhs) const;
};

struct SeventvEventAPIChannelCondition {
    SeventvEventAPIChannelCondition(QString twitchID);

    QString twitchID;

    QJsonObject encode() const;

    friend QDebug &operator<<(
        QDebug &dbg, const SeventvEventAPIChannelCondition &subscription);
    bool operator==(const SeventvEventAPIChannelCondition &rhs) const;
    bool operator!=(const SeventvEventAPIChannelCondition &rhs) const;
};

using SeventvEventAPICondition = std::variant<SeventvEventAPIObjectIDCondition,
                                              SeventvEventAPIChannelCondition>;

struct SeventvEventAPISubscription {
    bool operator==(const SeventvEventAPISubscription &rhs) const;
    bool operator!=(const SeventvEventAPISubscription &rhs) const;
    SeventvEventAPICondition condition;
    SeventvEventAPISubscriptionType type;

    QByteArray encodeSubscribe() const;
    QByteArray encodeUnsubscribe() const;

    friend QDebug &operator<<(QDebug &dbg,
                              const SeventvEventAPISubscription &subscription);
};

}  // namespace chatterino

template <>
constexpr magic_enum::customize::customize_t magic_enum::customize::enum_name<
    chatterino::SeventvEventAPISubscriptionType>(
    chatterino::SeventvEventAPISubscriptionType value) noexcept
{
    switch (value)
    {
        case chatterino::SeventvEventAPISubscriptionType::UpdateEmoteSet:
            return "emote_set.update";
        case chatterino::SeventvEventAPISubscriptionType::UpdateUser:
            return "user.update";
        case chatterino::SeventvEventAPISubscriptionType::AnyCosmetic:
            return "cosmetic.*";
        case chatterino::SeventvEventAPISubscriptionType::CreateCosmetic:
            return "cosmetic.create";
        case chatterino::SeventvEventAPISubscriptionType::UpdateCosmetic:
            return "cosmetic.update";
        case chatterino::SeventvEventAPISubscriptionType::DeleteCosmetic:
            return "cosmetic.delete";
        case chatterino::SeventvEventAPISubscriptionType::AnyEntitlement:
            return "entitlement.*";
        case chatterino::SeventvEventAPISubscriptionType::CreateEntitlement:
            return "entitlement.create";
        case chatterino::SeventvEventAPISubscriptionType::UpdateEntitlement:
            return "entitlement.update";
        case chatterino::SeventvEventAPISubscriptionType::DeleteEntitlement:
            return "entitlement.delete";

        default:
            return default_tag;
    }
}

namespace std {

template <>
struct hash<chatterino::SeventvEventAPIObjectIDCondition> {
    size_t operator()(
        const chatterino::SeventvEventAPIObjectIDCondition &c) const
    {
        return (size_t)qHash(c.objectID);
    }
};

template <>
struct hash<chatterino::SeventvEventAPIChannelCondition> {
    size_t operator()(
        const chatterino::SeventvEventAPIChannelCondition &c) const
    {
        return qHash(c.twitchID);
    }
};

template <>
struct hash<chatterino::SeventvEventAPISubscription> {
    size_t operator()(const chatterino::SeventvEventAPISubscription &sub) const
    {
        const size_t conditionHash =
            std::hash<chatterino::SeventvEventAPICondition>{}(sub.condition);
        return (size_t)qHash(conditionHash, qHash((int)sub.type));
    }
};

}  // namespace std
