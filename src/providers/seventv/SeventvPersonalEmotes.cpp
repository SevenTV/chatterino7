#include "providers/seventv/SeventvPersonalEmotes.hpp"

#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "singletons/Settings.hpp"
#include "util/DebugCount.hpp"
#include "util/Variant.hpp"

#include <memory>
#include <mutex>
#include <optional>

namespace chatterino {

using namespace Qt::Literals;

SeventvPersonalEmotes::SeventvPersonalEmotes()
{
    getSettings()->enableSevenTVPersonalEmotes.connect(
        [this]() {
            std::unique_lock<std::shared_mutex> lock(this->mutex_);
            this->enabled_ = getSettings()->enableSevenTVPersonalEmotes;
        },
        this->signalHolder_);
}

void SeventvPersonalEmotes::createEmoteSet(const QString &id)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->emoteSets_.contains(id))
    {
        DebugCount::increase(DebugObject::SeventvPersonalEmoteSets);
        this->emoteSets_.emplace(id, std::make_shared<const EmoteMap>());
    }
}

std::optional<std::shared_ptr<const EmoteMap>>
    SeventvPersonalEmotes::assignUsersToEmoteSet(
        const QString &emoteSetID,
        std::span<const seventv::eventapi::User> users)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);

    int64_t additions = 0;
    auto tryAssign = [&](auto &list) {
        if (list.contains(emoteSetID))
        {
            return false;
        }
        list.append(emoteSetID);
        additions++;
        return true;
    };
    for (const auto &user : users)
    {
        bool changed =
            std::visit(variant::Overloaded{
                           [&](const seventv::eventapi::TwitchUser &u) {
                               return tryAssign(this->twitchEmoteSets_[u.id]);
                           },
                           [&](const seventv::eventapi::KickUser &u) {
                               return tryAssign(this->kickEmoteSets_[u.id]);
                           }},
                       user);
        if (!changed)
        {
            // checking for one is enough because we always update all
            // ...unless the user changed their connections
            return std::nullopt;
        }
    }

    DebugCount::increase(DebugObject::SeventvPersonalEmoteAssignments,
                         additions);

    auto set = this->emoteSets_.find(emoteSetID);
    if (set == this->emoteSets_.end())
    {
        return std::nullopt;
    }
    return set->second.get();  // copy the shared_ptr
}

void SeventvPersonalEmotes::updateEmoteSet(
    const QString &id, const seventv::eventapi::EmoteAddDispatch &dispatch)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    auto emoteSet = this->emoteSets_.find(id);
    if (emoteSet != this->emoteSets_.end())
    {
        // Make sure this emote is actually new to avoid copying the map
        if (emoteSet->second.get()->contains(
                EmoteName{dispatch.emoteJson["name"].toString()}))
        {
            return;
        }
        SeventvEmotes::addEmote(emoteSet->second, dispatch,
                                SeventvEmoteSetKind::Personal);
    }
}
void SeventvPersonalEmotes::updateEmoteSet(
    const QString &id, const seventv::eventapi::EmoteUpdateDispatch &dispatch)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    auto emoteSet = this->emoteSets_.find(id);
    if (emoteSet != this->emoteSets_.end())
    {
        SeventvEmotes::updateEmote(emoteSet->second, dispatch,
                                   SeventvEmoteSetKind::Personal);
    }
}
void SeventvPersonalEmotes::updateEmoteSet(
    const QString &id, const seventv::eventapi::EmoteRemoveDispatch &dispatch)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    auto emoteSet = this->emoteSets_.find(id);
    if (emoteSet != this->emoteSets_.end())
    {
        SeventvEmotes::removeEmote(emoteSet->second, dispatch);
    }
}

void SeventvPersonalEmotes::addEmoteSetForTwitchUser(
    const QString &emoteSetID, EmoteMap &&map, const QString &userTwitchID)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    bool added = this->emoteSets_
                     .emplace(emoteSetID,
                              std::make_shared<const EmoteMap>(std::move(map)))
                     .second;
    if (added)
    {
        DebugCount::increase(DebugObject::SeventvPersonalEmoteSets);
    }
    this->twitchEmoteSets_[userTwitchID].append(emoteSetID);
    DebugCount::increase(DebugObject::SeventvPersonalEmoteAssignments);
}

void SeventvPersonalEmotes::addEmoteSetForKickUser(const QString &emoteSetID,
                                                   EmoteMap &&map,
                                                   uint64_t kickUserID)
{
    std::unique_lock<std::shared_mutex> lock(this->mutex_);
    bool added = this->emoteSets_
                     .emplace(emoteSetID,
                              std::make_shared<const EmoteMap>(std::move(map)))
                     .second;
    if (added)
    {
        DebugCount::increase(DebugObject::SeventvPersonalEmoteSets);
    }
    this->kickEmoteSets_[kickUserID].append(emoteSetID);
    DebugCount::increase(DebugObject::SeventvPersonalEmoteAssignments);
}

bool SeventvPersonalEmotes::hasEmoteSet(const QString &id) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    return this->emoteSets_.contains(id);
}

QList<std::shared_ptr<const EmoteMap>>
    SeventvPersonalEmotes::getEmoteSetsForTwitchUser(
        const QString &userID) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->enabled_)
    {
        return {};
    }

    auto ids = this->twitchEmoteSets_.find(userID);
    if (ids == this->twitchEmoteSets_.end())
    {
        return {};
    }

    return this->collectEmoteSets(ids->second);
}

QList<std::shared_ptr<const EmoteMap>>
    SeventvPersonalEmotes::getEmoteSetsForKickUser(uint64_t userID) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->enabled_)
    {
        return {};
    }

    auto ids = this->kickEmoteSets_.find(userID);
    if (ids == this->kickEmoteSets_.end())
    {
        return {};
    }

    return this->collectEmoteSets(ids->second);
}

EmotePtr SeventvPersonalEmotes::getEmoteForTwitchUser(
    const QString &userID, const EmoteName &emoteName) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->enabled_)
    {
        return {};
    }

    auto ids = this->twitchEmoteSets_.find(userID);
    if (ids == this->twitchEmoteSets_.end())
    {
        return {};
    }

    return this->findInEmoteSets(ids->second, emoteName);
}

EmotePtr SeventvPersonalEmotes::getEmoteForKickUser(
    uint64_t userID, const EmoteName &emoteName) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->enabled_)
    {
        return {};
    }

    auto ids = this->kickEmoteSets_.find(userID);
    if (ids == this->kickEmoteSets_.end())
    {
        return {};
    }

    return this->findInEmoteSets(ids->second, emoteName);
}

std::optional<std::shared_ptr<const EmoteMap>>
    SeventvPersonalEmotes::getEmoteSetByID(const QString &emoteSetID) const
{
    std::shared_lock<std::shared_mutex> lock(this->mutex_);
    if (!this->enabled_)
    {
        return std::nullopt;
    }

    auto id = this->emoteSets_.find(emoteSetID);
    if (id == this->emoteSets_.end())
    {
        return std::nullopt;
    }
    return id->second.get();
}

QList<std::shared_ptr<const EmoteMap>> SeventvPersonalEmotes::collectEmoteSets(
    std::span<const QString> emoteSetIDs) const
{
    QList<std::shared_ptr<const EmoteMap>> sets;
    sets.reserve(static_cast<qsizetype>(emoteSetIDs.size()));
    for (const auto &id : emoteSetIDs)
    {
        auto set = this->emoteSets_.find(id);
        if (set == this->emoteSets_.end())
        {
            continue;
        }
        sets.append(set->second.get());  // copy the shared_ptr
    }
    return sets;
}

EmotePtr SeventvPersonalEmotes::findInEmoteSets(
    std::span<const QString> emoteSetIDs, const EmoteName &name) const
{
    for (const auto &id : emoteSetIDs)
    {
        auto setIt = this->emoteSets_.find(id);
        if (setIt == this->emoteSets_.end())
        {
            continue;  // set doesn't exist
        }

        const auto &set = setIt->second.get();
        auto it = set->find(name);
        if (it == set->end())
        {
            continue;  // not in this set
        }
        return it->second;  // found the emote
    }

    return {};
}

}  // namespace chatterino
