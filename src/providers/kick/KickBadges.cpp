#include "providers/kick/KickBadges.hpp"

#include "debug/AssertInGuiThread.hpp"

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

}  // namespace chatterino
