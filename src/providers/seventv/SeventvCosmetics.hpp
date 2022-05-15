#pragma once

#include "common/Singleton.hpp"
#include "providers/seventv/paints/Paint.hpp"

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

class SeventvCosmetics : public Singleton
{
public:
    virtual void initialize(Settings &settings, Paths &paths) override;

    std::optional<EmotePtr> getBadge(const QString &userId);
private:
    void loadSeventvCosmetics();
    void loadSeventvBadges(QJsonArray badges);
    void loadSeventvPaints(QJsonArray paints);

    QStringList parsePaintUsers(QJsonArray users);
    std::optional<QColor> parsePaintColor(QJsonValue color);
    std::vector<std::pair<float, QColor>> parsePaintStops(QJsonArray stops);

    QColor decimalColorToQColor(const uint32_t color);

    std::map<QString, int> badgeMap;
    std::vector<EmotePtr> emotes;

    std::map<QString, Paint *> paints;
};

}  // namespace chatterino

