#pragma once

#include "messages/Message.hpp"

namespace chatterino {

class BoostJsonObject;
class KickChannel;
struct HighlightAlert;

class KickMessageBuilder
{
public:
    static std::pair<MessagePtrMut, HighlightAlert> makeChatMessage(
        KickChannel *kickChannel, BoostJsonObject data);
};

}  // namespace chatterino
