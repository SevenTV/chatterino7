#include "providers/seventv/SeventvBadges.hpp"

#include "common/NetworkRequest.hpp"
#include "common/NetworkResult.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"
#include "providers/seventv/SeventvEmotes.hpp"

#include <QUrl>
#include <QUrlQuery>

#include <map>

namespace chatterino {

void SeventvBadges::initialize(Settings & /*settings*/, Paths & /*paths*/)
{
    this->loadSeventvBadges();
}

boost::optional<EmotePtr> SeventvBadges::getBadge(const UserId &id) const
{
    const std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap_.find(id.string);
    if (it != this->badgeMap_.end())
    {
        return it->second;
    }
    return boost::none;
}

void SeventvBadges::assignBadgeToUser(const QString &badgeID,
                                      const UserId &userID)
{
    const std::shared_lock lock(this->mutex_);

    const auto badgeIt = this->knownBadges_.find(badgeID);
    if (badgeIt != this->knownBadges_.end())
    {
        this->badgeMap_[userID.string] = badgeIt->second;
    }
}

void SeventvBadges::addBadge(const QJsonObject &badgeJson)
{
    const auto badgeID = badgeJson["id"].toString();

    const std::shared_lock lock(this->mutex_);

    if (this->knownBadges_.find(badgeID) != this->knownBadges_.end())
    {
        return;
    }

    auto emote = Emote{EmoteName{}, makeSeventvImageSet(badgeJson),
                       Tooltip{badgeJson["tooltip"].toString()}, Url{}};

    if (emote.images.getImage1()->isEmpty())
    {
        return;  // Bad images
    }

    this->knownBadges_[badgeID] =
        std::make_shared<const Emote>(std::move(emote));
}

void SeventvBadges::loadSeventvBadges()
{
    static QUrl url("https://7tv.io/v2/cosmetics");

    static QUrlQuery urlQuery;
    // valid user_identifier values: "object_id", "twitch_id", "login"
    urlQuery.addQueryItem("user_identifier", "twitch_id");

    url.setQuery(urlQuery);

    NetworkRequest(url)
        .onSuccess([this](const NetworkResult &result) -> Outcome {
            auto root = result.parseJson();

            const std::shared_lock lock(this->mutex_);

            for (const auto &jsonBadge : root.value("badges").toArray())
            {
                auto badge = jsonBadge.toObject();
                auto badgeID = badge["id"].toString();
                auto urls = badge["urls"].toArray();
                auto emote =
                    Emote{EmoteName{},
                          ImageSet{Url{urls[0].toArray()[1].toString()},
                                   Url{urls[1].toArray()[1].toString()},
                                   Url{urls[2].toArray()[1].toString()}},
                          Tooltip{badge["tooltip"].toString()}, Url{}};

                auto emotePtr = std::make_shared<const Emote>(std::move(emote));
                this->knownBadges_[badgeID] = emotePtr;

                for (const auto &user : badge["users"].toArray())
                {
                    this->badgeMap_[user.toString()] = emotePtr;
                }
            }

            return Success;
        })
        .execute();
}

}  // namespace chatterino
