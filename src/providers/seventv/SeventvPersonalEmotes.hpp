#pragma once

#include "common/Atomic.hpp"
#include "common/Singleton.hpp"
#include "messages/Emote.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"

#include <boost/optional.hpp>

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace chatterino {

class SeventvPersonalEmotes : public Singleton
{
public:
    void createEmoteSet(const QString &id);
    void assignUserToEmoteSet(const QString &emoteSetID,
                              const QString &userTwitchID);

    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteAddDispatch &dispatch);
    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteUpdateDispatch &dispatch);
    void updateEmoteSet(const QString &id,
                        const seventv::eventapi::EmoteRemoveDispatch &dispatch);

    bool hasEmoteSet(const QString &id) const;

    boost::optional<std::shared_ptr<const EmoteMap>> getEmoteSetForUser(
        const QString &userID) const;

    boost::optional<EmotePtr> getEmoteForUser(const QString &userID,
                                              const EmoteName &emoteName) const;

private:
    std::unordered_map<QString, Atomic<std::shared_ptr<const EmoteMap>>>
        emoteSets_;
    std::unordered_map<QString, QString> userEmoteSets_;
    mutable std::shared_mutex mutex_;
};

}  // namespace chatterino
