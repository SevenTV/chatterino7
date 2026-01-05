#include "providers/kick/KickChatServer.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickMessageBuilder.hpp"
#include "util/BoostJsonWrap.hpp"

#include <utility>

namespace chatterino {

KickChatServer::KickChatServer() = default;
KickChatServer::~KickChatServer() = default;

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
    auto existing = this->findByRoomID(roomID);
    if (!existing)
    {
        qCWarning(chatterinoKick) << "No channel found for room" << roomID;
        return;
    }
    auto msg = KickMessageBuilder::makeChatMessage(existing.get(), data);
    if (msg)
    {
        existing->addMessage(msg, MessageContext::Original);
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

}  // namespace chatterino
