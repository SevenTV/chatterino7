#pragma once

#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"

namespace chatterino {

class BoostJsonObject;
class KickChannel;
struct HighlightAlert;

class KickMessageBuilder : public MessageBuilder
{
public:
    KickMessageBuilder(SystemMessageTag, KickChannel *channel,
                       const QDateTime &time);
    KickMessageBuilder(KickChannel *channel);

    static std::pair<MessagePtrMut, HighlightAlert> makeChatMessage(
        KickChannel *kickChannel, BoostJsonObject data);

    static MessagePtrMut makeTimeoutMessage(KickChannel *channel,
                                            const QDateTime &now,
                                            BoostJsonObject data);

    static MessagePtrMut makeUntimeoutMessage(KickChannel *channel,
                                              BoostJsonObject data);

    KickChannel *channel() const
    {
        return this->channel_;
    }

private:
    void appendChannelName();
    void appendUsername(BoostJsonObject senderObj, BoostJsonObject identityObj);
    void appendMentionedUser(const QString &username, QString &text,
                             bool trailingSpace = true);

    KickChannel *channel_ = nullptr;
};

}  // namespace chatterino
