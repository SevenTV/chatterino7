#pragma once

#include "messages/Emote.hpp"
#include "messages/MessageElement.hpp"

#include <string_view>

namespace chatterino {

class KickBadges
{
public:
    static std::pair<EmotePtr, MessageElementFlag> lookup(
        std::string_view name);
};

}  // namespace chatterino
