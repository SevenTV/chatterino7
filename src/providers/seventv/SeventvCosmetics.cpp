#include "SeventvCosmetics.hpp"

#include "common/NetworkRequest.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"

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
            loadSeventvBadges(jsonBadges);

            auto jsonPaints = root.value("paints").toArray();
            loadSeventvPaints(jsonPaints);

            return Success;
        })
        .execute();
}

void SeventvCosmetics::loadSeventvBadges(QJsonArray badges)
{
    int index = 0;
    for (const auto &jsonBadge_ : badges)
    {
        auto badge = jsonBadge_.toObject();
        auto urls = badge.value("urls").toArray();
        auto emote = Emote{EmoteName{},
                           ImageSet{Url{urls.at(0).toArray().at(1).toString()},
                                    Url{urls.at(1).toArray().at(1).toString()},
                                    Url{urls.at(2).toArray().at(1).toString()}},
                           Tooltip{badge.value("tooltip").toString()}, Url{}};

        emotes.push_back(std::make_shared<const Emote>(std::move(emote)));

        for (const auto &user : badge.value("users").toArray())
        {
            badgeMap[user.toString()] = index;
        }
        ++index;
    }
}

void SeventvCosmetics::loadSeventvPaints(QJsonArray paints)
{
    for (const auto &jsonPaint : paints)
    {
        Paint *paint;
        auto paintObject = jsonPaint.toObject();

        QString name = paintObject.value("name").toString();
        QStringList userIds =
            parsePaintUsers(paintObject.value("users").toArray());

        auto color = parsePaintColor(paintObject.value("color"));
        bool repeat = paintObject.value("repeat").toBool();
        float angle = paintObject.value("angle").toDouble();

        std::vector<std::pair<float, QColor>> stops =
            parsePaintStops(paintObject.value("stops").toArray());

        QString function = paintObject.value("function").toString();
        if (function == "linear-gradient")
        {
            paint = new LinearGradientPaint(name, color, stops, repeat, angle);
        }
        else if (function == "radial-gradient")
        {
            QString shape = paintObject.value("shape").toString();
        }
        else if (function == "url")
        {
            QString url = paintObject.value("image_url").toString();
        }
        else
        {
            continue;
        }

        for (const auto &userId : userIds)
        {
            this->paints[userId] = paint;
        }
    }
}

QStringList SeventvCosmetics::parsePaintUsers(QJsonArray users)
{
    QStringList userIds;

    for (const auto &user : users)
    {
        userIds.push_back(user.toString());
    }

    return userIds;
}

std::optional<QColor> SeventvCosmetics::parsePaintColor(QJsonValue color)
{
    if (color.isNull())
        return std::nullopt;

    return decimalColorToQColor(color.toInt());
}

std::vector<std::pair<float, QColor>> SeventvCosmetics::parsePaintStops(
    QJsonArray stops)
{
    std::vector<std::pair<float, QColor>> parsedStops;

    for (const auto &stop : stops)
    {
        auto stopObject = stop.toObject();
        parsedStops.push_back(std::make_pair(
            stopObject.value("at").toDouble(),
            decimalColorToQColor(stopObject.value("color").toInt())));
    }

    return parsedStops;
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
