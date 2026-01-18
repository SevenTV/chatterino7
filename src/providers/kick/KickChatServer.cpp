#include "providers/kick/KickChatServer.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/kick/KickMessageBuilder.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"  // IWYU pragma: keep
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "util/BoostJsonWrap.hpp"
#include "util/PostToThread.hpp"

#include <QPointer>

#include <utility>

namespace {

using namespace Qt::Literals;

// fallback case
template <typename T>
T stringSwitch(std::string_view /* provided */)
{
    return {};
}

template <typename T>
T stringSwitch(std::string_view provided, std::string_view match, T &&value,
               auto &&...rest)
{
    if (provided == match)
    {
        return std::forward<T>(value);
    }
    return stringSwitch<T>(provided, std::forward<decltype(rest)>(rest)...);
}

}  // namespace

namespace chatterino {

KickChatServer::KickChatServer()
    : liveController_(*this)
{
}

KickChatServer::~KickChatServer() = default;

void KickChatServer::initialize()
{
    this->initializeSeventvEventApi(getApp()->getSeventvEventAPI());
}

std::shared_ptr<KickChannel> KickChatServer::findByRoomID(uint64_t roomID) const
{
    auto it = this->channelsByRoomID.find(roomID);
    if (it != this->channelsByRoomID.end())
    {
        return it->second.lock();
    }
    return nullptr;
}

std::shared_ptr<KickChannel> KickChatServer::findByChannelID(
    uint64_t channelID) const
{
    auto it = this->channelsByChannelID.find(channelID);
    if (it != this->channelsByChannelID.end())
    {
        return it->second.lock();
    }
    return nullptr;
}

std::shared_ptr<KickChannel> KickChatServer::findByUserID(uint64_t userID) const
{
    auto it = this->channelsByUserID.find(userID);
    if (it != this->channelsByUserID.end())
    {
        return it->second.lock();
    }
    return nullptr;
}

std::shared_ptr<KickChannel> KickChatServer::findBySlug(
    const QString &slug) const
{
    auto it = this->channelsBySlug.find(slug);
    if (it != this->channelsBySlug.end())
    {
        return it->second.lock();
    }
    return nullptr;
}

void KickChatServer::forEachChannel(FunctionRef<void(KickChannel &channel)> cb)
{
    for (const auto &[id, weak] : this->channelsByRoomID)
    {
        auto chan = weak.lock();
        if (chan)
        {
            cb(*chan);
        }
    }
}

void KickChatServer::forEachSeventvEmoteSet(
    const QString &emoteSetID, FunctionRef<void(KickChannel &channel)> cb)
{
    for (const auto &[id, weak] : this->channelsByRoomID)
    {
        auto chan = weak.lock();
        if (chan && chan->seventvEmoteSetID() == emoteSetID)
        {
            cb(*chan);
        }
    }
}

void KickChatServer::forEachSeventvUser(
    const QString &seventvUserID, FunctionRef<void(KickChannel &channel)> cb)
{
    for (const auto &[id, weak] : this->channelsByRoomID)
    {
        auto chan = weak.lock();
        if (chan && chan->seventvUserID() == seventvUserID)
        {
            cb(*chan);
        }
    }
}

std::shared_ptr<Channel> KickChatServer::getOrCreate(
    const QString &slug, const KickChannel::UserInit &init)
{
    auto lower = slug.toLower();
    if (lower.startsWith(u":kick:"))
    {
        lower = std::move(lower).mid(6);
    }

    auto existing = this->findBySlug(lower);
    if (existing)
    {
        return existing;
    }
    auto chan = std::make_shared<KickChannel>(lower);
    this->channelsBySlug[lower] = chan;
    if (init.roomID != 0)
    {
        this->channelsByRoomID[init.roomID] = chan;
    }
    if (init.channelID != 0)
    {
        this->channelsByChannelID[init.channelID] = chan;
    }
    this->signalHolder_.managedConnect(
        chan->userIDChanged, [this, weak{chan->weakFromThis()}] {
            auto chan = weak.lock();
            if (chan && chan->userID() != 0)
            {
                this->channelsByUserID[chan->userID()] = weak;
                this->liveController_.queueNewChannel(chan->userID());
                getApp()->getSeventvEventAPI()->subscribeKickChannel(
                    QString::number(chan->userID()));
            }
        });
    chan->initialize(init);
    this->loadGlobalEmotesIfNeeded();
    return chan;
}

bool KickChatServer::onAppEvent(uint64_t roomID, uint64_t channelID,
                                std::string_view event, BoostJsonObject data)
{
    using Fn = void (KickChatServer::*)(KickChannel *, BoostJsonObject);
    auto fn = stringSwitch<Fn>(
        event,                                                      //
        "ChatMessageEvent", &KickChatServer::onChatMessage,         //
        "MessageDeletedEvent", &KickChatServer::onMessageDeleted,   //
        "ChatroomClearEvent", &KickChatServer::onChatroomClear,     //
        "UserBannedEvent", &KickChatServer::onUserBanned,           //
        "UserUnbannedEvent", &KickChatServer::onUserUnbanned,       //
        "SubscriptionEvent", &KickChatServer::onSubscriptionEvent,  //
        "GiftedSubscriptionsEvent",
        &KickChatServer::onGiftedSubscriptionEvent,  //
        "PinnedMessageCreatedEvent",
        &KickChatServer::onPinnedMessageCreatedEvent,  //
        "PinnedMessageDeletedEvent",
        &KickChatServer::onPinnedMessageDeletedEvent,                   //
        "RewardRedeemedEvent", &KickChatServer::onRewardRedeemedEvent,  //
        "KicksGifted", &KickChatServer::onKicksGiftedEvent,             //
        "StreamHostEvent", &KickChatServer::onStreamHostEvent,          //

        // ignored
        "KicksLeaderboardUpdated", &KickChatServer::onKnownIgnoredMessage,  //
        "GiftsLeaderboardUpdated", &KickChatServer::onKnownIgnoredMessage,  //
        "PredictionUpdated", &KickChatServer::onKnownIgnoredMessage,        //
        // old sub events
        "ChannelSubscriptionEvent", &KickChatServer::onKnownIgnoredMessage,  //
        "LuckyUsersWhoGotGiftSubscriptionsEvent",
        &KickChatServer::onKnownIgnoredMessage,  //
        // v1 stream host event
        "StreamHostedEvent", &KickChatServer::onKnownIgnoredMessage,  //
        // seems to be for subscriptions too
        "ChatMessageSentEvent", &KickChatServer::onKnownIgnoredMessage  //
    );

    if (!fn)
    {
        return false;  // no handler
    }

    std::shared_ptr<KickChannel> channel;
    if (roomID != 0)
    {
        channel = this->findByRoomID(roomID);
    }
    else
    {
        channel = this->findByChannelID(channelID);
    }

    if (!channel)
    {
        qCWarning(chatterinoKick)
            << "No channel found for room" << roomID << "channel" << channelID;
        return true;  // technically it's handled, we just don't have a channel
    }

    (this->*fn)(channel.get(), data);
    return true;
}

// NOLINTBEGIN(readability-convert-member-functions-to-static)
void KickChatServer::onChatMessage(KickChannel *channel, BoostJsonObject data)
{
    auto [msg, highlight] = KickMessageBuilder::makeChatMessage(channel, data);
    if (msg)
    {
        channel->applySimilarityFilters(msg);

        if (!msg->flags.has(MessageFlag::Similar) ||
            (!getSettings()->hideSimilar &&
             getSettings()->shownSimilarTriggerHighlights))
        {
            MessageBuilder::triggerHighlights(channel, highlight);
        }

        const auto highlighted = msg->flags.has(MessageFlag::Highlighted);
        const auto showInMentions = msg->flags.has(MessageFlag::ShowInMentions);

        if (highlighted && showInMentions)
        {
            // yes, we add this to the Twitch channel
            getApp()->getTwitch()->getMentionsChannel()->addMessage(
                msg, MessageContext::Original);
        }
        channel->addMessage(msg, MessageContext::Original);
    }
}

void KickChatServer::onUserBanned(KickChannel *channel, BoostJsonObject data)
{
    auto now = QDateTime::currentDateTime();
    auto msg = KickMessageBuilder::makeTimeoutMessage(channel, now, data);
    if (msg)
    {
        channel->addOrReplaceTimeout(msg, now);
    }
}

void KickChatServer::onUserUnbanned(KickChannel *channel, BoostJsonObject data)
{
    auto msg = KickMessageBuilder::makeUntimeoutMessage(channel, data);
    if (msg)
    {
        channel->addMessage(msg, MessageContext::Original);
    }
}

void KickChatServer::onMessageDeleted(KickChannel *channel,
                                      BoostJsonObject data)
{
    auto messageID = data["message"]["id"].toQString();
    auto msg = channel->findMessageByID(messageID);
    if (!msg)
    {
        return;
    }

    msg->flags.set(MessageFlag::Disabled, MessageFlag::InvalidReplyTarget);
    if (!getSettings()->hideDeletionActions)
    {
        channel->addMessage(MessageBuilder::makeDeletionMessageFromIRC(msg),
                            MessageContext::Original);
    }
}

void KickChatServer::onChatroomClear(KickChannel *channel,
                                     BoostJsonObject /* data */)
{
    auto now = QDateTime::currentDateTime();
    auto clear = KickMessageBuilder::makeClearChatMessage(now, {});
    channel->disableAllMessages();
    channel->addOrReplaceClearChat(clear, now);
}

void KickChatServer::onPinnedMessageCreatedEvent(KickChannel *channel,
                                                 BoostJsonObject data)
{
    channel->addMessage(KickMessageBuilder::makePinnedMessage(channel, data),
                        MessageContext::Original);
}

void KickChatServer::onPinnedMessageDeletedEvent(KickChannel *channel,
                                                 BoostJsonObject /*data*/)
{
    channel->addSystemMessage(u"The pinned message was unpinned."_s);
}

void KickChatServer::onStreamHostEvent(KickChannel *channel,
                                       BoostJsonObject data)
{
    channel->addMessage(KickMessageBuilder::makeHostMessage(channel, data),
                        MessageContext::Original);
}

void KickChatServer::onSubscriptionEvent(KickChannel *channel,
                                         BoostJsonObject data)
{
    auto [first, second, alert] =
        KickMessageBuilder::makeSubscriptionMessage(channel, data);
    if (first)
    {
        MessageBuilder::triggerHighlights(channel, alert);
        channel->addMessage(first, MessageContext::Original);
    }
    channel->addMessage(second, MessageContext::Original);
}

void KickChatServer::onGiftedSubscriptionEvent(KickChannel *channel,
                                               BoostJsonObject data)
{
    auto msg = KickMessageBuilder::makeGiftedSubscriptionMessage(channel, data);
    if (msg)
    {
        channel->addMessage(msg, MessageContext::Original);
    }
}

void KickChatServer::onRewardRedeemedEvent(KickChannel *channel,
                                           BoostJsonObject data)
{
    auto msg = KickMessageBuilder::makeRewardRedeemedMessage(channel, data);
    if (msg)
    {
        channel->addMessage(msg, MessageContext::Original);
    }
}

void KickChatServer::onKicksGiftedEvent(KickChannel *channel,
                                        BoostJsonObject data)
{
    auto msg = KickMessageBuilder::makeKicksGiftedMessage(channel, data);
    if (msg)
    {
        channel->addMessage(msg, MessageContext::Original);
    }
}

void KickChatServer::onKnownIgnoredMessage(KickChannel * /*channel*/,
                                           BoostJsonObject /*data*/)
{
    // nop
}

// NOLINTEND(readability-convert-member-functions-to-static)

void KickChatServer::onJoin(uint64_t roomID) const
{
    auto existing = this->findByRoomID(roomID);
    if (!existing)
    {
        qCWarning(chatterinoKick) << "No channel found for room" << roomID;
        return;
    }
    existing->addSystemMessage("joined");
}

std::shared_ptr<const EmoteMap> KickChatServer::globalEmotes() const
{
    if (!this->globalEmotes_)
    {
        return EMPTY_EMOTE_MAP;
    }
    return this->globalEmotes_;
}

void KickChatServer::loadGlobalEmotesIfNeeded()
{
    if (this->globalEmotes_ || this->loadingGlobalEmotes_)
    {
        return;
    }
    this->loadingGlobalEmotes_ = true;
    KickApi::privateEmotesInChannel(
        u"kick"_s,
        [self = QPointer(this)](
            const ExpectedStr<std::vector<KickPrivateEmoteSetInfo>> &res) {
            if (!self)
            {
                return;
            }
            self->loadingGlobalEmotes_ = false;
            if (!res)
            {
                qCWarning(chatterinoKick)
                    << "Failed to fetch global emotes:" << res.error();
                self->globalEmotes_ = EMPTY_EMOTE_MAP;
                return;
            }
            auto emotes = std::make_shared<EmoteMap>();
            for (const auto &set : *res)
            {
                if (set.userID)
                {
                    continue;  // local set
                }
                for (const auto &emoteInfo : set.emotes)
                {
                    if (emoteInfo.subscribersOnly)
                    {
                        continue;
                    }
                    auto id = QString::number(emoteInfo.emoteID);
                    auto emote = KickEmotes::emoteForID(id, emoteInfo.name);
                    (*emotes)[emote->name] = emote;
                }
            }
            self->globalEmotes_ = std::move(emotes);
            qCDebug(chatterinoKick)
                << "Loaded" << self->globalEmotes_->size() << "global emotes";
        });
}

void KickChatServer::registerRoomID(uint64_t roomID, uint64_t channelID,
                                    std::weak_ptr<KickChannel> chan)
{
    this->channelsByRoomID[roomID] = chan;
    this->channelsByChannelID[channelID] = std::move(chan);
}

void KickChatServer::initializeSeventvEventApi(SeventvEventAPI *api)
{
    if (!api)
    {
        return;
    }

    this->signalHolder_.managedConnect(
        api->signals_.emoteAdded, [&](const auto &data) {
            postToThread(
                [this, data] {
                    this->forEachSeventvEmoteSet(data.emoteSetID,
                                                 [data](KickChannel &chan) {
                                                     chan.addSeventvEmote(data);
                                                 });
                },
                this);
        });
    this->signalHolder_.managedConnect(
        api->signals_.emoteUpdated, [&](const auto &data) {
            postToThread(
                [this, data] {
                    this->forEachSeventvEmoteSet(
                        data.emoteSetID, [data](KickChannel &chan) {
                            chan.updateSeventvEmote(data);
                        });
                },
                this);
        });
    this->signalHolder_.managedConnect(
        api->signals_.emoteRemoved, [&](const auto &data) {
            postToThread(
                [this, data] {
                    this->forEachSeventvEmoteSet(
                        data.emoteSetID, [data](KickChannel &chan) {
                            chan.removeSeventvEmote(data);
                        });
                },
                this);
        });
    this->signalHolder_.managedConnect(
        api->signals_.userUpdated, [&](const auto &data) {
            this->forEachSeventvUser(data.userID, [data](KickChannel &chan) {
                chan.updateSeventvUser(data);
            });
        });
    this->signalHolder_.managedConnect(
        api->signals_.personalEmoteSetAdded, [&](const auto &data) {
            postToThread(
                [this, data] {
                    if (data.kickUserName.isEmpty())
                    {
                        return;
                    }
                    this->forEachChannel([data](auto &chan) {
                        chan.upsertPersonalSeventvEmotes(data.kickUserName,
                                                         data.emoteSet);
                    });
                },
                this);
        });
}

}  // namespace chatterino
