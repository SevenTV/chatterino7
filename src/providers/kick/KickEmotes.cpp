#include "providers/kick/KickEmotes.hpp"

#include "messages/Image.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

namespace {

using namespace chatterino;

struct StringViewHash : std::hash<QStringView> {
    using is_transparent = std::true_type;
};

template <typename T>
using StringMap =
    boost::unordered_flat_map<QString, T, StringViewHash, std::equal_to<>>;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, clazy-non-pod-global-static)
StringMap<EmotePtr> CACHE;

}  // namespace

namespace chatterino {

EmotePtr KickEmotes::emoteForID(QStringView id, QStringView name)
{
    auto it = CACHE.find(id);
    if (it != CACHE.end())
    {
        return it->second;
    }

    auto nameStr = name.toString();
    QString tooltip = nameStr.toHtmlEscaped() % u"<br>Kick Emote";
    auto emote = std::make_shared<const Emote>(Emote{
        .name = {std::move(nameStr)},
        .images = ImageSet(Image::fromAutoscaledUrl(
            {u"https://files.kick.com/emotes/" % id % u"/fullsize"}, 28)),
        .tooltip = {std::move(tooltip)},
    });
    CACHE.emplace(id.toString(), emote);
    return emote;
}

}  // namespace chatterino
