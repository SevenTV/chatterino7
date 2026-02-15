#pragma once

#include "common/Atomic.hpp"
#include "messages/Emote.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QList>

#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <variant>

namespace chatterino {

namespace seventv::eventapi {
struct TwitchUser;
struct KickUser;
using User = std::variant<TwitchUser, KickUser>;
}  // namespace seventv::eventapi

class SeventvPersonalEmotes
{
public:
    SeventvPersonalEmotes();

    void createEmoteSet(const QString &id);

    // Returns the emote-map of this set if it's new.
    std::optional<std::shared_ptr<const EmoteMap>> assignUsersToEmoteSet(
        const QString &emoteSetID,
        std::span<const seventv::eventapi::User> users);

    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteAddDispatch &dispatch);
    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteUpdateDispatch &dispatch);
    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteRemoveDispatch &dispatch);

    void addEmoteSetForTwitchUser(const QString &emoteSetID, EmoteMap &&map,
                                  const QString &userTwitchID);
    void addEmoteSetForKickUser(const QString &emoteSetID, EmoteMap &&map,
                                uint64_t kickUserID);

    bool hasEmoteSet(const QString &id) const;

    QList<std::shared_ptr<const EmoteMap>> getEmoteSetsForTwitchUser(
        const QString &userID) const;
    QList<std::shared_ptr<const EmoteMap>> getEmoteSetsForKickUser(
        uint64_t userID) const;

    EmotePtr getEmoteForTwitchUser(const QString &userID,
                                   const EmoteName &emoteName) const;
    EmotePtr getEmoteForKickUser(uint64_t userID,
                                 const EmoteName &emoteName) const;

    std::optional<std::shared_ptr<const EmoteMap>> getEmoteSetByID(
        const QString &emoteSetID) const;

private:
    QList<std::shared_ptr<const EmoteMap>> collectEmoteSets(
        std::span<const QString> emoteSetIDs) const;
    EmotePtr findInEmoteSets(std::span<const QString> emoteSetIDs,
                             const EmoteName &name) const;

    // emoteSetID => emoteSet
    std::unordered_map<QString, Atomic<std::shared_ptr<const EmoteMap>>>
        emoteSets_;
    // userID => emoteSetID
    std::unordered_map<QString, QList<QString>> twitchEmoteSets_;
    // userID => emoteSetID
    std::unordered_map<uint64_t, QList<QString>> kickEmoteSets_;

    bool enabled_ = true;
    pajlada::Signals::SignalHolder signalHolder_;

    mutable std::shared_mutex mutex_;
};

}  // namespace chatterino
