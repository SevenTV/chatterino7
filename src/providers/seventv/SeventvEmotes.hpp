#pragma once

#include "boost/optional.hpp"
#include "common/Aliases.hpp"
#include "common/Atomic.hpp"
#include "providers/seventv/eventapimessages/EventApiDispatch.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <memory>

namespace chatterino {

// https://github.com/SevenTV/ServerGo/blob/dfe867f991e8cfd7a79d93b9bec681216c32abdb/src/mongo/datastructure/datastructure.go#L56-L67
enum class SeventvEmoteVisibilityFlag : int64_t {
    None = 0LL,

    Private = (1LL << 0),
    Global = (1LL << 1),
    Unlisted = (1LL << 2),

    OverrideBttv = (1LL << 3),
    OverrideFfz = (1LL << 4),
    OverrideTwitchGlobal = (1LL << 5),
    OverrideTwitchSubscriber = (1LL << 6),

    ZeroWidth = (1LL << 7),
};

// https://github.com/SevenTV/API/blob/8fbfc702a3fe0ada59f4b9593c65748d36ac7c0b/data/model/emote-set.model.go#L33-L38
enum class SeventvActiveEmoteFlag : int64_t {
    None = 0LL,

    ZeroWidth = (1LL << 0),

    OverrideTwitchGlobal = (1 << 16),
    OverrideTwitchSubscriber = (1 << 17),
    OverrideBetterTTV = (1 << 18),
};

enum class SeventvEmoteFlag : int64_t {
    None = 0LL,
    // The emote is private and can only be accessed by its owner, editors and moderators
    Private = 1 << 0,
    // The emote was verified to be an original creation by the uploader
    Authentic = (1LL << 1),
    // The emote is recommended to be enabled as Zero-Width
    ZeroWidth = (1LL << 8),

    // Content Flags

    // Sexually Suggesive
    ContentSexual = (1LL << 16),
    // Rapid flashing
    ContentEpilepsy = (1LL << 17),
    // Edgy or distasteful, may be offensive to some users
    ContentEdgy = (1 << 18),
    // Not allowed specifically on the Twitch platform
    ContentTwitchDisallowed = (1LL << 24),
};

using SeventvEmoteVisibilityFlags = FlagsEnum<SeventvEmoteVisibilityFlag>;
using SeventvActiveEmoteFlags = FlagsEnum<SeventvActiveEmoteFlag>;
using SeventvEmoteFlags = FlagsEnum<SeventvEmoteFlag>;

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;
class EmoteMap;

class SeventvEmotes final
{
    static constexpr const char *apiUrlGQL = "https://api.7tv.app/v2/gql";

    struct ChannelInfo {
        QString userId;
        QString emoteSetId;
    };

public:
    SeventvEmotes();

    std::shared_ptr<const EmoteMap> emotes() const;
    boost::optional<EmotePtr> emote(const EmoteName &name) const;
    void loadEmotes();
    static boost::optional<EmotePtr> addEmote(
        Atomic<std::shared_ptr<const EmoteMap>> &map,
        const EventApiEmoteAddDispatch &dispatch);
    static boost::optional<EmotePtr> updateEmote(
        Atomic<std::shared_ptr<const EmoteMap>> &map,
        const EventApiEmoteUpdateDispatch &dispatch);
    static bool removeEmote(Atomic<std::shared_ptr<const EmoteMap>> &map,
                            const EventApiEmoteRemoveDispatch &dispatch);
    static void loadChannel(
        std::weak_ptr<Channel> channel, const QString &channelId,
        std::function<void(EmoteMap &&, ChannelInfo)> callback,
        bool manualRefresh);

private:
    Atomic<std::shared_ptr<const EmoteMap>> global_;
};

}  // namespace chatterino
