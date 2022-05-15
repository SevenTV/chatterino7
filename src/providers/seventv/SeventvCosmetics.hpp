#pragma once

#include "common/Singleton.hpp"

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

    QColor decimalColorToQColor(const uint32_t color);

    std::map<QString, int> badgeMap;
    std::vector<EmotePtr> emotes;
};

}  // namespace chatterino

