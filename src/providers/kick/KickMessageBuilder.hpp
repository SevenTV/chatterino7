#pragma once

#include "messages/Message.hpp"

namespace chatterino {

class BoostJsonObject;
class KickChannel;

class KickMessageBuilder
{
public:
    static MessagePtrMut makeChatMessage(KickChannel *kickChannel,
                                         BoostJsonObject data);
};

}  // namespace chatterino
