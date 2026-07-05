#pragma once

#include "messages/Emote.hpp"
#include "messages/MessageElement.hpp"
#include "util/BoostJsonWrap.hpp"

#include <string_view>

namespace chatterino {

class KickBadges
{
public:
    static std::pair<EmotePtr, MessageElementFlag> lookup(
        std::string_view name);

    static std::pair<EmotePtr, MessageElementFlag> getV2Cached(
        BoostJsonObject badgeObj);
};

}  // namespace chatterino
