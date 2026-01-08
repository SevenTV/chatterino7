#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageThread.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/KickLiveUpdates.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"

using namespace Qt::Literals;

namespace chatterino {

KickChannel::KickChannel(const QString &name)
    : Channel(name.toLower(), Type::Kick)
    , ChannelChatters(static_cast<Channel &>(*this))
    , seventvEmotes_(std::make_shared<const EmoteMap>())
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

void KickChannel::initialize(const UserInit &init)
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

// FIXME: These are largely the same as in TwitchChannel. They should be
// combined. However, we also want to avoid merge conflicts as much as possible.

void KickChannel::reloadSeventvEmotes(bool manualRefresh)
{
    SeventvEmotes::loadKickChannelEmotes(
        this->weakFromThis(), this->userID(),
        [weak = this->weakFromThis()](EmoteMap &&emotes,
                                      const SeventvEmotes::ChannelInfo &info) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            self->seventvEmotes_.set(
                std::make_shared<const EmoteMap>(std::move(emotes)));
            self->seventvKickConnectionIndex_ = info.twitchConnectionIndex;
            self->updateSeventvData(info.userID, info.emoteSetID);
        },
        manualRefresh);
}

std::shared_ptr<const EmoteMap> KickChannel::seventvEmotes() const
{
    return this->seventvEmotes_.get();
}

EmotePtr KickChannel::seventvEmote(const EmoteName &name) const
{
    auto emotes = this->seventvEmotes_.get();

    auto it = emotes->find(name);
    if (it != emotes->end())
    {
        return it->second;
    }
    return nullptr;
}

void KickChannel::addSeventvEmote(
    const seventv::eventapi::EmoteAddDispatch &dispatch)
{
    if (!SeventvEmotes::addEmote(this->seventvEmotes_, dispatch))
    {
        return;
    }

    this->addOrReplaceSeventvAddRemove(true, dispatch.actorName,
                                       dispatch.emoteJson["name"].toString());
}

void KickChannel::updateSeventvEmote(
    const seventv::eventapi::EmoteUpdateDispatch &dispatch)
{
    if (!SeventvEmotes::updateEmote(this->seventvEmotes_, dispatch))
    {
        return;
    }

    auto builder =
        MessageBuilder(liveUpdatesUpdateEmoteMessage, "7TV", dispatch.actorName,
                       dispatch.emoteName, dispatch.oldEmoteName);
    this->addMessage(builder.release(), MessageContext::Original);
}

void KickChannel::removeSeventvEmote(
    const seventv::eventapi::EmoteRemoveDispatch &dispatch)
{
    auto removed = SeventvEmotes::removeEmote(this->seventvEmotes_, dispatch);
    if (!removed)
    {
        return;
    }

    this->addOrReplaceSeventvAddRemove(false, dispatch.actorName,
                                       (*removed)->name.string);
}

void KickChannel::updateSeventvUser(
    const seventv::eventapi::UserConnectionUpdateDispatch &dispatch)
{
    if (dispatch.connectionIndex != this->seventvKickConnectionIndex_)
    {
        // A different connection was updated
        return;
    }

    this->updateSeventvData(this->seventvUserID_, dispatch.emoteSetID);
    SeventvEmotes::getEmoteSet(
        dispatch.emoteSetID,
        [this, weak = weakOf<Channel>(this), dispatch](auto &&emotes,
                                                       const auto &name) {
            postToThread([this, weak, dispatch, emotes, name]() {
                if (auto shared = weak.lock())
                {
                    this->seventvEmotes_.set(
                        std::make_shared<EmoteMap>(emotes));
                    auto builder =
                        MessageBuilder(liveUpdatesUpdateEmoteSetMessage, "7TV",
                                       dispatch.actorName, name);
                    this->addMessage(builder.release(),
                                     MessageContext::Original);
                }
            });
        },
        [this, weak = weakOf<Channel>(this)](const auto &reason) {
            postToThread([this, weak, reason]() {
                if (auto shared = weak.lock())
                {
                    this->seventvEmotes_.set(EMPTY_EMOTE_MAP);
                    this->addSystemMessage(
                        QString("Failed updating 7TV emote set (%1).")
                            .arg(reason));
                }
            });
        });
}

const QString &KickChannel::seventvUserID() const
{
    return this->seventvUserID_;
}

const QString &KickChannel::seventvEmoteSetID() const
{
    return this->seventvEmoteSetID_;
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
    auto oldUserID = std::exchange(this->userID_, init.userID);
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

    if (oldUserID != this->userID())
    {
        this->reloadSeventvEmotes(false);
    }
}

void KickChannel::updateSeventvData(const QString &newUserID,
                                    const QString &newEmoteSetID)
{
    if (this->seventvUserID_ == newUserID &&
        this->seventvEmoteSetID_ == newEmoteSetID)
    {
        return;
    }

    const auto oldUserID = makeConditionedOptional(
        !this->seventvUserID_.isEmpty() && this->seventvUserID_ != newUserID,
        this->seventvUserID_);
    const auto oldEmoteSetID =
        makeConditionedOptional(!this->seventvEmoteSetID_.isEmpty() &&
                                    this->seventvEmoteSetID_ != newEmoteSetID,
                                this->seventvEmoteSetID_);

    this->seventvUserID_ = newUserID;
    this->seventvEmoteSetID_ = newEmoteSetID;
    runInGuiThread([this, oldUserID, oldEmoteSetID]() {
        if (getApp()->getSeventvEventAPI())
        {
            getApp()->getSeventvEventAPI()->subscribeUser(
                this->seventvUserID_, this->seventvEmoteSetID_);

            if (oldUserID || oldEmoteSetID)
            {
                // FIXME: make sure no TwitchChannel is listenting to this
                getApp()->getTwitch()->dropSeventvChannel(
                    oldUserID.value_or(QString()),
                    oldEmoteSetID.value_or(QString()));
            }
        }
    });
}

void KickChannel::addOrReplaceSeventvAddRemove(bool isEmoteAdd,
                                               const QString &actor,
                                               const QString &emoteName)
{
    if (this->tryReplaceLastSeventvAddOrRemove(
            isEmoteAdd ? MessageFlag::LiveUpdatesAdd
                       : MessageFlag::LiveUpdatesRemove,
            actor, emoteName))
    {
        return;
    }

    this->lastSeventvEmoteNames_ = {emoteName};

    MessagePtr msg;
    if (isEmoteAdd)
    {
        msg = MessageBuilder(liveUpdatesAddEmoteMessage, "7TV", actor,
                             this->lastSeventvEmoteNames_)
                  .release();
    }
    else
    {
        msg = MessageBuilder(liveUpdatesRemoveEmoteMessage, "7TV", actor,
                             this->lastSeventvEmoteNames_)
                  .release();
    }
    this->lastSeventvMessage_ = msg;
    this->lastSeventvEmoteActor_ = actor;
    this->addMessage(msg, MessageContext::Original);
}

bool KickChannel::tryReplaceLastSeventvAddOrRemove(MessageFlag op,
                                                   const QString &actor,
                                                   const QString &emoteName)
{
    auto last = this->lastSeventvMessage_.lock();
    if (!last || !last->flags.has(op) ||
        last->parseTime < QTime::currentTime().addSecs(-5) ||
        last->loginName != actor)
    {
        return false;
    }
    // Update the message
    this->lastSeventvEmoteNames_.push_back(emoteName);

    auto makeReplacement = [&](MessageFlag op) -> MessageBuilder {
        if (op == MessageFlag::LiveUpdatesAdd)
        {
            return {
                liveUpdatesAddEmoteMessage,
                "7TV",
                last->loginName,
                this->lastSeventvEmoteNames_,
            };
        }

        // op == RemoveEmoteMessage
        return {
            liveUpdatesRemoveEmoteMessage,
            "7TV",
            last->loginName,
            this->lastSeventvEmoteNames_,
        };
    };

    auto replacement = makeReplacement(op);

    replacement->flags = last->flags;

    auto msg = replacement.release();
    this->lastSeventvMessage_ = msg;
    this->replaceMessage(last, msg);

    return true;
}

QDebug operator<<(QDebug dbg, const KickChannel &chan)
{
    QDebugStateSaver s(dbg);
    dbg.nospace().noquote() << "[KickChannel " << chan.getName() << ']';
    return dbg;
}

}  // namespace chatterino
