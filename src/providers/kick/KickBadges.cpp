#include "providers/kick/KickBadges.hpp"

#include "debug/AssertInGuiThread.hpp"
#include "util/QStringHash.hpp"

#include <boost/unordered/unordered_flat_map.hpp>
#include <magic_enum/magic_enum.hpp>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

enum class BadgeID : uint8_t {
    bot,
    broadcaster,
    founder,
    moderator,
    og,
    sidekick,
    staff,
    sub_gifter,
    subscriber,
    trainwreckstv,
    verified,
    vip
};

struct BadgeNameData {
    QString friendlyName;
    QStringView pathSegment;
    MessageElementFlag flag{};
};
BadgeNameData nameDataFor(BadgeID id)
{
    switch (id)
    {
        case BadgeID::bot:
            return {
                .friendlyName = u"Bot"_s,
                .pathSegment = u"bot",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::broadcaster:
            return {
                .friendlyName = u"Broadcaster"_s,
                .pathSegment = u"broadcaster",
                .flag = MessageElementFlag::BadgeChannelAuthority,
            };
        case BadgeID::founder:
            return {.friendlyName = u"Founder"_s,
                    .pathSegment = u"founder",
                    .flag = MessageElementFlag::BadgeSubscription};
        case BadgeID::moderator:
            return {
                .friendlyName = u"Moderator"_s,
                .pathSegment = u"moderator",
                .flag = MessageElementFlag::BadgeChannelAuthority,
            };
        case BadgeID::og:
            return {
                .friendlyName = u"OG"_s,
                .pathSegment = u"og",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::sidekick:
            return {
                .friendlyName = u"Sidekick"_s,
                .pathSegment = u"sidekick",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::staff:
            return {
                .friendlyName = u"Staff"_s,
                .pathSegment = u"staff",
                .flag = MessageElementFlag::BadgeGlobalAuthority,
            };
        case BadgeID::sub_gifter:
            return {
                .friendlyName = u"Sub Gifter"_s,
                .pathSegment = u"sub_gifter",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::subscriber:
            return {
                .friendlyName = u"Subscriber"_s,
                .pathSegment = u"subscriber",
                .flag = MessageElementFlag::BadgeSubscription,
            };
        case BadgeID::trainwreckstv:
            return {
                .friendlyName = u"TrainwrecksTV"_s,
                .pathSegment = u"trainwreckstv",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::verified:
            return {
                .friendlyName = u"Verified"_s,
                .pathSegment = u"verified",
                .flag = MessageElementFlag::BadgeVanity,
            };
        case BadgeID::vip:
            return {
                .friendlyName = u"VIP"_s,
                .pathSegment = u"vip",
                .flag = MessageElementFlag::BadgeChannelAuthority,
            };
    }
    return {};
}

using CacheData = std::pair<EmotePtr, MessageElementFlag>;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<CacheData, magic_enum::enum_count<BadgeID>()> CACHE{};

std::vector<std::pair<unsigned, ImageSet>> makeGiftFixpoints()
{
    std::vector<std::pair<unsigned, ImageSet>> vec;
    const auto add = [&](unsigned count) {
        QString numStr = QString::number(count);
        vec.emplace_back(
            count,
            ImageSet{
                Image::fromUrl({u":/kick/badges/gift-" % numStr % u"-18.webp"},
                               1.0, {18, 18}),
                Image::fromUrl({u":/kick/badges/gift-" % numStr % u"-36.webp"},
                               .5, {36, 36}),
            });
    };

    add(1);
    add(5);
    add(10);
    add(25);
    add(50);
    add(100);
    add(150);
    add(200);
    add(250);
    add(300);
    add(350);
    add(400);
    add(450);
    add(500);
    add(550);
    add(600);
    add(650);
    add(700);
    add(750);
    add(800);
    add(850);
    add(900);
    add(950);
    add(1000);
    add(2000);
    add(3000);
    add(4000);
    add(5000);

    return vec;
}

}  // namespace

namespace chatterino {

std::pair<EmotePtr, MessageElementFlag> KickBadges::lookup(
    std::string_view name)
{
    assertInGuiThread();

    auto id = magic_enum::enum_cast<BadgeID>(name);
    if (!id)
    {
        return {nullptr, {}};
    }

    auto idx = static_cast<uint8_t>(*id);
    if (idx >= CACHE.size())
    {
        assert(false);
        return {nullptr, {}};
    }

    assertInGuiThread();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) -- see above
    auto &entry = CACHE[idx];
    if (!entry.first)
    {
        auto data = nameDataFor(*id);

        entry.first = std::make_shared<const Emote>(Emote{
            .name = {data.friendlyName},
            .images =
                ImageSet{
                    Image::fromUrl(
                        {u":/kick/badges/" % data.pathSegment % u"-18.webp"},
                        1.0, {18, 18}),
                    Image::fromUrl(
                        {u":/kick/badges/" % data.pathSegment % u"-36.webp"},
                        .5, {36, 36}),
                },
            .tooltip = Tooltip{data.friendlyName},
        });
        entry.second = data.flag;
    }

    return entry;
}

std::pair<EmotePtr, MessageElementFlag> KickBadges::getV2Cached(
    BoostJsonObject badgeObj)
{
    static boost::unordered_flat_map<QString, EmotePtr> cache;
    assertInGuiThread();

    auto imageUrl = badgeObj["image_url"].toQString();
    auto it = cache.find(imageUrl);
    if (it != cache.end())
    {
        return {it->second, MessageElementFlag::BadgeVanity};
    }

    QString name;
    auto origName = badgeObj["name"].toStringView();
    if (origName == "level")
    {
        auto level = QString::number(badgeObj["metadata"]["level"].toUint64());
        name = u"Level " % level;
    }
    else
    {
        name = QString::fromUtf8(origName.data(),
                                 static_cast<qsizetype>(origName.size()));
    }

    auto emote = std::make_shared<const Emote>(Emote{
        .name = {name},
        .images =
            ImageSet{
                Image::fromAutoscaledUrl({imageUrl}, 18),
            },
        .tooltip = Tooltip{name},
    });
    cache.emplace(imageUrl, emote);
    return {emote, MessageElementFlag::BadgeVanity};
}

EmotePtr KickBadges::lookupSubGifter(unsigned amount)
{
    static std::map<unsigned, EmotePtr> fullCache;
    static auto fixpoints = makeGiftFixpoints();

    assertInGuiThread();

    auto fullIt = fullCache.find(amount);
    if (fullIt != fullCache.end())
    {
        return fullIt->second;
    }

    auto fixpointsIt = std::ranges::lower_bound(
        fixpoints, amount, std::less<>{}, [](const auto &it) {
            return it.first;
        });
    if (fixpointsIt != fixpoints.begin() &&
        (fixpointsIt == fixpoints.end() || fixpointsIt->first != amount))
    {
        --fixpointsIt;
    }
    assert(fixpointsIt != fixpoints.end());
    if (fixpointsIt->first > amount)
    {
        return nullptr;
    }
    QString name = u"Gifted " % QString::number(amount) % u" sub";
    if (amount != 1)
    {
        name += 's';
    }
    fullIt = fullCache
                 .emplace(amount, std::make_shared<const Emote>(Emote{
                                      .name = {name},
                                      .images = fixpointsIt->second,
                                      .tooltip = Tooltip{name},
                                  }))
                 .first;
    return fullIt->second;
}

}  // namespace chatterino
