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
    KickMessageBuilder(KickChannel *channel, const QDateTime &time);
    KickMessageBuilder(KickChannel *channel);

    static std::pair<MessagePtrMut, HighlightAlert> makeChatMessage(
        KickChannel *kickChannel, BoostJsonObject data);

    static MessagePtrMut makeTimeoutMessage(KickChannel *channel,
                                            const QDateTime &now,
                                            BoostJsonObject data);

    static MessagePtrMut makeUntimeoutMessage(KickChannel *channel,
                                              BoostJsonObject data);

    static MessagePtrMut makePinnedMessage(KickChannel *channel,
                                           BoostJsonObject data);

    static MessagePtrMut makeHostMessage(KickChannel *channel,
                                         BoostJsonObject data);

    static std::tuple<MessagePtrMut, MessagePtrMut, HighlightAlert>
        makeSubscriptionMessage(KickChannel *channel, BoostJsonObject data);

    static MessagePtrMut makeGiftedSubscriptionMessage(KickChannel *channel,
                                                       BoostJsonObject data);

    static MessagePtrMut makeRewardRedeemedMessage(KickChannel *channel,
                                                   BoostJsonObject data);

    static MessagePtrMut makeKicksGiftedMessage(KickChannel *channel,
                                                BoostJsonObject data);

    KickChannel *channel() const
    {
        return this->channel_;
    }

    uint64_t senderID = 0;

private:
    void appendChannelName();
    void appendUsername(BoostJsonObject identityObj);
    void appendUsernameAsSender(const QString &username);
    void appendMentionedUser(const QString &username, QString &text,
                             bool trailingSpace = true);

    KickChannel *channel_ = nullptr;
};

}  // namespace chatterino
