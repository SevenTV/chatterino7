// SPDX-FileCopyrightText: 2022 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/BadgeRegistry.hpp"

#include "messages/Emote.hpp"

#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino {

std::optional<EmotePtr> BadgeRegistry::getBadge(const UserId &id) const
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap_.find(id.string);
    if (it != this->badgeMap_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<EmotePtr> BadgeRegistry::getKickBadge(uint64_t id) const
{
    std::shared_lock lock(this->mutex_);

    auto it = this->kickBadgeMap_.find(id);
    if (it != this->kickBadgeMap_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void BadgeRegistry::assignBadgeToUser(const QString &badgeID,
                                      const UserId &userID, uint64_t kickUserID)
{
    const std::unique_lock lock(this->mutex_);

    const auto badgeIt = this->knownBadges_.find(badgeID);
    if (badgeIt != this->knownBadges_.end())
    {
        if (!userID.string.isEmpty())
        {
            this->badgeMap_[userID.string] = badgeIt->second;
        }
        if (kickUserID != 0)
        {
            this->kickBadgeMap_[kickUserID] = badgeIt->second;
        }
    }
}

void BadgeRegistry::clearBadgeFromUser(const QString &badgeID,
                                       const UserId &userID,
                                       uint64_t kickUserID)
{
    const std::unique_lock lock(this->mutex_);

    if (!userID.string.isEmpty())
    {
        const auto it = this->badgeMap_.find(userID.string);
        if (it != this->badgeMap_.end() && it->second->id.string == badgeID)
        {
            this->badgeMap_.erase(userID.string);
        }
    }
    if (kickUserID != 0)
    {
        const auto it = this->kickBadgeMap_.find(kickUserID);
        if (it != this->kickBadgeMap_.end() && it->second->id.string == badgeID)
        {
            this->kickBadgeMap_.erase(kickUserID);
        }
    }
}

QString BadgeRegistry::registerBadge(const QJsonObject &badgeJson)
{
    const auto badgeID = this->idForBadge(badgeJson);

    const std::unique_lock lock(this->mutex_);

    if (this->knownBadges_.contains(badgeID))
    {
        return badgeID;
    }

    auto emote = this->createBadge(badgeID, badgeJson);
    if (!emote)
    {
        return badgeID;
    }

    this->knownBadges_[badgeID] = std::move(emote);
    return badgeID;
}

}  // namespace chatterino
