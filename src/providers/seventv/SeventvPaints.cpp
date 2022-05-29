#include "SeventvPaints.hpp"

#include "common/NetworkRequest.hpp"
#include "common/Outcome.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"

#include <QUrl>
#include <QUrlQuery>

namespace chatterino {
void SeventvPaints::initialize(Settings &settings, Paths &paths)
{
    this->loadSeventvCosmetics();
}

std::optional<Paint *> SeventvPaints::getPaint(const QString &userName)
{
    auto it = paints.find(userName);
    if (it != paints.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void SeventvPaints::loadSeventvCosmetics()
{
    static QUrl url("https://api.7tv.app/v2/cosmetics");

    static QUrlQuery urlQuery;
    // valid user_identifier values: "object_id", "twitch_id", "login"
    urlQuery.addQueryItem("user_identifier", "login");

    url.setQuery(urlQuery);

    NetworkRequest(url)
        .onSuccess([this](NetworkResult result) -> Outcome {
            auto root = result.parseJson();

            auto jsonPaints = root.value("paints").toArray();
            loadSeventvPaints(jsonPaints);

            return Success;
        })
        .execute();
}

void SeventvPaints::loadSeventvPaints(QJsonArray paints)
{
    for (const auto &jsonPaint : paints)
    {
        Paint *paint;
        auto paintObject = jsonPaint.toObject();

        QString name = paintObject.value("name").toString();
        QStringList userNames =
            parsePaintUsers(paintObject.value("users").toArray());

        auto color = parsePaintColor(paintObject.value("color"));
        bool repeat = paintObject.value("repeat").toBool();
        float angle = paintObject.value("angle").toDouble();

        QGradientStops stops =
            parsePaintStops(paintObject.value("stops").toArray());

        QString function = paintObject.value("function").toString();
        if (function == "linear-gradient")
        {
            paint = new LinearGradientPaint(name, color, stops, repeat, angle);
        }
        else if (function == "radial-gradient")
        {
            QString shape = paintObject.value("shape").toString();

            paint = new RadialGradientPaint();
        }
        else if (function == "url")
        {
            QString url = paintObject.value("image_url").toString();

            paint = new UrlPaint();
        }
        else
        {
            continue;
        }

        for (const auto &userName : userNames)
        {
            this->paints[userName] = paint;
        }
    }
}

QStringList SeventvPaints::parsePaintUsers(QJsonArray users)
{
    QStringList userIds;

    for (const auto &user : users)
    {
        userIds.push_back(user.toString());
    }

    return userIds;
}

std::optional<QColor> SeventvPaints::parsePaintColor(QJsonValue color)
{
    if (color.isNull())
        return std::nullopt;

    return decimalColorToQColor(color.toInt());
}

QGradientStops SeventvPaints::parsePaintStops(QJsonArray stops)
{
    QGradientStops parsedStops;
    double lastStop = -1;

    for (const auto &stop : stops)
    {
        auto stopObject = stop.toObject();

        auto decimalColor = stopObject.value("color").toInt();
        auto position = stopObject.value("at").toDouble();

        // HACK: qt does not support hard edges in gradients like css does
        // Setting a different color at the same position twice just overwrites
        // the previous color. So we have to shift the second point slightly
        // ahead, simulating an actual hard edge
        if (position == lastStop)
        {
            position += 0.0000001;
        }

        lastStop = position;
        parsedStops.append(
            QGradientStop(position, decimalColorToQColor(decimalColor)));
    }

    return parsedStops;
}

QColor SeventvPaints::decimalColorToQColor(uint32_t color)
{
    auto red = (color >> 24) & 0xFF;
    auto green = (color >> 16) & 0xFF;
    auto blue = (color >> 8) & 0xFF;
    auto alpha = color & 0xFF;

    return QColor(red, green, blue, alpha);
}

}  // namespace chatterino
