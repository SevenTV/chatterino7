#include "SeventvCosmetics.hpp"

#include "common/NetworkRequest.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"

#include <QUrl>
#include <QUrlQuery>

namespace chatterino {
void SeventvCosmetics::initialize(Settings &settings, Paths &paths)
{
    this->loadSeventvCosmetics();
}


std::optional<EmotePtr> SeventvCosmetics::getBadge(const QString &userId)
{
    auto it = badgeMap.find(userId);
    if (it != badgeMap.end())
    {
        return emotes[it->second];
    }
    return std::nullopt;
}

void SeventvCosmetics::loadSeventvCosmetics()
{
    static QUrl url("https://api.7tv.app/v2/cosmetics");

    static QUrlQuery urlQuery;
    // valid user_identifier values: "object_id", "twitch_id", "login"
    urlQuery.addQueryItem("user_identifier", "twitch_id");

    url.setQuery(urlQuery);

    NetworkRequest(url)
        .onSuccess([this](NetworkResult result) -> Outcome {
            auto root = result.parseJson();
            auto jsonBadges = root.value("badges").toArray();

            int index = 0;
            for (const auto &jsonBadge_ : jsonBadges)
            {
                auto badge = jsonBadge_.toObject();
                auto urls = badge.value("urls").toArray();
                auto emote =
                    Emote{EmoteName{},
                          ImageSet{Url{urls.at(0).toArray().at(1).toString()},
                                   Url{urls.at(1).toArray().at(1).toString()},
                                   Url{urls.at(2).toArray().at(1).toString()}},
                          Tooltip{badge.value("tooltip").toString()}, Url{}};

                emotes.push_back(
                    std::make_shared<const Emote>(std::move(emote)));

                for (const auto &user : badge.value("users").toArray())
                {
                    badgeMap[user.toString()] = index;
                }
                ++index;
            }

            return Success;
        })
        .execute();
}

QColor SeventvCosmetics::decimalColorToQColor(uint32_t color)
{
    auto red = (color >> 24) & 0xFF;
    auto green = (color >> 16) & 0xFF;
    auto blue = (color >> 8) & 0xFF;
    auto alpha = color & 0xFF;

    return QColor(red, green, blue, alpha);
}

}  // namespace chatterino

