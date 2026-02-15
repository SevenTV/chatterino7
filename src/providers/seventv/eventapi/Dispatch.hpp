// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Emote.hpp"
#include "providers/seventv/eventapi/Subscription.hpp"
#include "providers/seventv/SeventvCosmetics.hpp"

#include <QJsonObject>
#include <QString>
#include <QVarLengthArray>

#include <span>

namespace chatterino::seventv::eventapi {

// https://github.com/SevenTV/EventAPI/tree/ca4ff15cc42b89560fa661a76c5849047763d334#message-payload
struct Dispatch {
    SubscriptionType type;
    QJsonObject body;
    QString id;
    // it's okay for this to be empty
    QString actorName;

    Dispatch(QJsonObject obj);
};

struct EmoteAddDispatch {
    QString emoteSetID;
    QString actorName;
    QJsonObject emoteJson;
    QString emoteID;

    EmoteAddDispatch(const Dispatch &dispatch, QJsonObject emote);

    bool validate() const;
};

struct EmoteRemoveDispatch {
    QString emoteSetID;
    QString actorName;
    QString emoteName;
    QString emoteID;

    EmoteRemoveDispatch(const Dispatch &dispatch, QJsonObject emote);

    bool validate() const;
};

struct EmoteUpdateDispatch {
    QString emoteSetID;
    QString actorName;
    QString emoteID;
    QString oldEmoteName;
    QString emoteName;

    EmoteUpdateDispatch(const Dispatch &dispatch, QJsonObject oldValue,
                        QJsonObject value);

    bool validate() const;
};

struct UserConnectionUpdateDispatch {
    QString userID;
    QString actorName;
    QString oldEmoteSetID;
    QString emoteSetID;
    size_t connectionIndex;

    UserConnectionUpdateDispatch(const Dispatch &dispatch,
                                 const QJsonObject &update,
                                 size_t connectionIndex);

    bool validate() const;
};

struct CosmeticCreateDispatch {
    QJsonObject data;
    CosmeticKind kind;

    CosmeticCreateDispatch(const Dispatch &dispatch);

    bool validate() const;
};

struct TwitchUser {
    TwitchUser(const QJsonObject &connection);
    QString id;
    QString userName;
};

struct KickUser {
    KickUser(const QJsonObject &connection);

    uint64_t id;
    QString userName;
};
using User = std::variant<TwitchUser, KickUser>;

struct EntitlementCreateDeleteDispatch {
    QString seventvUsername;
    QVarLengthArray<User, 1> connections;
    /** id of the entitlement */
    QString refID;
    CosmeticKind kind;

    EntitlementCreateDeleteDispatch(const Dispatch &dispatch);

    bool validate() const;
};

struct EmoteSetCreateDispatch {
    QString emoteSetID;
    bool isPersonalOrCommercial;

    EmoteSetCreateDispatch(const QJsonObject &emoteSet);

    bool validate() const;
};

struct PersonalEmoteSetAdded {
    std::span<const User> connections;
    std::shared_ptr<const EmoteMap> emoteSet;
};

}  // namespace chatterino::seventv::eventapi
