#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/MessageThread.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/KickLiveUpdates.hpp"

using namespace Qt::Literals;

namespace chatterino {

KickChannel::KickChannel(const QString &name)
    : Channel(name.toLower(), Type::Kick)
{
}

KickChannel::~KickChannel()
{
    auto *app = getApp();
    if (app)
    {
        app->getKickChatServer()->liveUpdates().leaveRoom(this->roomID(),
                                                          this->channelID());
    }
}

void KickChannel::initialize(UserInit init)
{
    this->setUserInfo(init);
    this->resolveChannelInfo();
}

std::shared_ptr<KickChannel> KickChannel::sharedFromThis()
{
    return std::static_pointer_cast<KickChannel>(this->shared_from_this());
}

std::weak_ptr<KickChannel> KickChannel::weakFromThis()
{
    return this->sharedFromThis();
}

std::shared_ptr<MessageThread> KickChannel::getOrCreateThread(
    const QString &messageID)
{
    auto existingIt = this->threads_.find(messageID);
    if (existingIt != this->threads_.end())
    {
        auto existing = existingIt->second.lock();
        if (existing)
        {
            return existing;
        }
    }

    auto msg = this->findMessageByID(messageID);
    if (!msg)
    {
        return nullptr;
    }

    auto thread = std::make_shared<MessageThread>(msg);
    this->threads_[messageID] = thread;
    return thread;
}

void KickChannel::resolveChannelInfo()
{
    auto weak = this->weakFromThis();
    KickApi::privateChannelInfo(
        this->getName(),
        [weak](const ExpectedStr<KickPrivateChannelInfo> &res) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            if (!res)
            {
                qCWarning(chatterinoKick)
                    << *self
                    << "Failed to resolve channel info:" << res.error();
                self->addSystemMessage(u"Failed to resolve channel info: "_s %
                                       res.error());
                return;
            }

            self->setUserInfo(UserInit{
                .roomID = res->chatroom.roomID,
                .userID = res->user.userID,
                .channelID = res->channelID,
            });
            auto oldDisplayName =
                std::exchange(self->displayName_, res->user.username);
            if (oldDisplayName != self->displayName_)
            {
                self->displayNameChanged.invoke();
            }
        });
}

void KickChannel::setUserInfo(UserInit init)
{
    this->userID_ = init.userID;
    auto oldChannelID = std::exchange(this->channelID_, init.channelID);
    auto oldRoomID = std::exchange(this->roomID_, init.roomID);

    if (oldChannelID != this->channelID() || oldRoomID != this->roomID())
    {
        if (oldChannelID != 0 || oldRoomID != 0)
        {
            qCWarning(chatterinoKick)
                << *this << "Unexpected room/channel ID change - oldChannelID:"
                << oldChannelID << "channelID:" << this->channelID()
                << "oldRoomID:" << oldRoomID << "roomID:" << this->roomID();
            return;
        }

        auto *srv = getApp()->getKickChatServer();
        srv->registerRoomID(this->roomID(), this->weakFromThis());
        srv->liveUpdates().joinRoom(this->roomID(), this->channelID());
    }
}

QDebug operator<<(QDebug dbg, const KickChannel &chan)
{
    QDebugStateSaver s(dbg);
    dbg.nospace().noquote() << "[KickChannel " << chan.getName() << ']';
    return dbg;
}

}  // namespace chatterino
