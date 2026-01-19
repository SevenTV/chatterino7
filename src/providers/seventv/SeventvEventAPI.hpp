// SPDX-FileCopyrightText: 2022 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signal.hpp>
#include <QString>

#include <memory>

namespace chatterino {

namespace liveupdates {
struct Diag;
}  // namespace liveupdates

namespace seventv::eventapi {
struct EmoteAddDispatch;
struct EmoteUpdateDispatch;
struct EmoteRemoveDispatch;
struct UserConnectionUpdateDispatch;
}  // namespace seventv::eventapi

class SeventvBadges;
class SeventvPaints;
class EmoteMap;

class SeventvEventAPIPrivate;
class SeventvEventAPI
{
    template <typename T>
    using Signal = pajlada::Signals::Signal<T>;

public:
    SeventvEventAPI(QString host,
                    std::chrono::milliseconds defaultHeartbeatInterval =
                        std::chrono::milliseconds(25000));
    ~SeventvEventAPI();

    struct PersonalEmoteSetAdded {
        QString twitchUserName;
        QString kickUserName;
        std::shared_ptr<const EmoteMap> emoteSet;
    };

    struct {
        Signal<const seventv::eventapi::EmoteAddDispatch &> emoteAdded;
        Signal<const seventv::eventapi::EmoteUpdateDispatch &> emoteUpdated;
        Signal<const seventv::eventapi::EmoteRemoveDispatch &> emoteRemoved;
        Signal<const seventv::eventapi::UserConnectionUpdateDispatch &>
            userUpdated;
        Signal<const PersonalEmoteSetAdded &> personalEmoteSetAdded;
    } signals_;  // NOLINT(readability-identifier-naming)

    /**
     * Subscribes to a user and emote-set
     * if not already subscribed.
     *
     * @param userID 7TV user-id, may be empty.
     * @param emoteSetID 7TV emote-set-id, may be empty.
     */
    void subscribeUser(const QString &userID, const QString &emoteSetID);
    /**
     * Subscribes to cosmetics and entitlements in a Twitch channel
     * if not already subscribed.
     *
     * @param id Twitch channel id
     */
    void subscribeTwitchChannel(const QString &id);
    void subscribeKickChannel(const QString &id);

    void subscribePlatformChannel(const QString &userID,
                                  const QString &platform);

    /** Unsubscribes from a user by its 7TV user id */
    void unsubscribeUser(const QString &id);
    /** Unsubscribes from an emote-set by its id */
    void unsubscribeEmoteSet(const QString &id);
    /** Unsubscribes from cosmetics and entitlements in a Twitch channel */
    void unsubscribeTwitchChannel(const QString &id);
    void unsubscribeKickChannel(const QString &id);

    void unsubscribePlatformChannel(const QString &userID,
                                    const QString &platform);

    /// Stop the manager
    ///
    /// Used in tests to check that connections are closed (through #diag()).
    /// Otherwise, calling the destructor is sufficient.
    void stop();

    /// Statistics about the opened/closed connections and received messages
    ///
    /// Used in tests.
    const liveupdates::Diag &diag() const;

private:
    std::unique_ptr<SeventvEventAPIPrivate> private_;
};

}  // namespace chatterino
