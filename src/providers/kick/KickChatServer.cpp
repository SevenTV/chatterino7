#include "providers/kick/KickChatServer.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/kick/KickMessageBuilder.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"  // IWYU pragma: keep
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "util/BoostJsonWrap.hpp"
#include "util/PostToThread.hpp"

#include <utility>

namespace chatterino {

KickChatServer::KickChatServer() = default;
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
    chan->initialize(init);
    return chan;
}

void KickChatServer::onChatMessage(uint64_t roomID, BoostJsonObject data) const
{
    auto channel = this->findByRoomID(roomID);
    if (!channel)
    {
        qCWarning(chatterinoKick) << "No channel found for room" << roomID;
        return;
    }
    auto [msg, highlight] =
        KickMessageBuilder::makeChatMessage(channel.get(), data);
    if (msg)
    {
        channel->applySimilarityFilters(msg);

        if (!msg->flags.has(MessageFlag::Similar) ||
            (!getSettings()->hideSimilar &&
             getSettings()->shownSimilarTriggerHighlights))
        {
            MessageBuilder::triggerHighlights(channel.get(), highlight);
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

void KickChatServer::registerRoomID(uint64_t roomID,
                                    std::weak_ptr<KickChannel> chan)
{
    this->channelsByRoomID[roomID] = std::move(chan);
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
                    this->forEachChannel([data](auto &chan) {
                        chan.upsertPersonalSeventvEmotes(data.first,
                                                         data.second);
                    });
                },
                this);
        });
}

}  // namespace chatterino
