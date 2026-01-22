#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "messages/Emote.hpp"
#include "messages/Link.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageFlag.hpp"
#include "messages/MessageThread.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/KickLiveUpdates.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"

using namespace Qt::Literals;
using namespace std::chrono_literals;

namespace chatterino {

KickChannel::KickChannel(const QString &name)
    : Channel(name.toLower(), Type::Kick)
    , ChannelChatters(static_cast<Channel &>(*this))
    , displayName_(name)
    , slug_(KickApi::slugify(this->getName()))
    , seventvEmotes_(std::make_shared<const EmoteMap>())
{
    this->setMentionFlag(MessageElementFlag::KickUsername);
}

KickChannel::~KickChannel()
{
    auto *app = getApp();
    if (app)
    {
        app->getKickChatServer()->liveUpdates().leaveRoom(this->roomID(),
                                                          this->channelID());
        auto *eventApi = app->getSeventvEventAPI();
        if (eventApi)
        {
            eventApi->unsubscribeKickChannel(QString::number(this->userID()));
        }
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
    bool cacheHit = readProviderEmotesCache(
        u"kick." % QString::number(this->userID()), "seventv",
        [this](const auto &jsonDoc) {
            const auto json = jsonDoc.object();
            const auto emoteSet = json["emote_set"].toObject();
            const auto parsedEmotes = emoteSet["emotes"].toArray();
            auto emoteMap = seventv::detail::parseEmotes(
                parsedEmotes, SeventvEmoteSetKind::Channel);
            this->seventvEmotes_.set(
                std::make_shared<const EmoteMap>(emoteMap));
        });

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
        manualRefresh, cacheHit);
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

bool KickChannel::canSendMessage() const
{
    return getApp()->getAccounts()->kick.isLoggedIn();
}

void KickChannel::sendMessage(const QString &message)
{
    this->sendReply(message, {});
}

void KickChannel::sendReply(const QString &message, const QString &replyToId)
{
    if (!getApp()->getAccounts()->kick.isLoggedIn())
    {
        this->addLoginMessage();
        return;
    }

    auto prepared = this->prepareMessage(message);
    if (prepared.isEmpty())
    {
        return;
    }

    if (!this->checkMessageRatelimit())
    {
        return;
    }

    this->updateSevenTVActivity();
    getKickApi()->sendMessage(
        this->userID(), prepared, replyToId,
        [weak = this->weakFromThis()](const auto &res) {
            if (res)
            {
                return;  // message sent
            }
            auto self = weak.lock();
            if (self)
            {
                self->addSystemMessage(u"Failed to send message: " %
                                       res.error());
            }
        });
}

void KickChannel::deleteMessage(const QString &messageID)
{
    getKickApi()->deleteChatMessage(
        messageID, [weak = this->weakFromThis()](const auto &res) {
            auto self = weak.lock();
            if (!self || res)
            {
                return;
            }
            self->addSystemMessage(u"Failed to delete message: " % res.error());
        });
}

bool KickChannel::isMod() const
{
    return this->isMod_;
}

void KickChannel::setMod(bool mod)
{
    if (this->isMod_ == mod)
    {
        return;
    }
    this->isMod_ = mod;
    this->userStateChanged.invoke();
}

bool KickChannel::isVip() const
{
    return this->isVip_;
}

void KickChannel::setVip(bool vip)
{
    if (this->isVip_ == vip)
    {
        return;
    }
    this->isVip_ = vip;
    this->userStateChanged.invoke();
}

bool KickChannel::isBroadcaster() const
{
    return this->userID() == getApp()->getAccounts()->kick.current()->userID();
}

bool KickChannel::hasModRights() const
{
    return this->isMod() || this->isBroadcaster();
}

bool KickChannel::hasHighRateLimit() const
{
    return this->hasModRights() || this->isVip();
}

bool KickChannel::isLive() const
{
    return this->streamData_.isLive;
}

void KickChannel::updateStreamData(const KickChannelInfo &info)
{
    assert(info.userID == this->userID());

    bool changed = false;
    if (this->streamData_.isLive != info.stream.isLive)
    {
        changed = true;
        this->streamData_.isLive = info.stream.isLive;

        if (this->streamData_.isLive)
        {
            this->addMessage(
                MessageBuilder::makeLiveMessage(
                    this->getDisplayName(), QString::number(this->userID()),
                    info.streamTitle,
                    {MessageFlag::System,
                     MessageFlag::DoNotTriggerNotification}),
                MessageContext::Original);
        }
        else
        {
            this->addMessage(
                MessageBuilder::makeOfflineSystemMessage(
                    this->getDisplayName(), QString::number(this->userID())),
                MessageContext::Original);
        }
        this->liveStatusChanged.invoke();
    }
    if (this->streamData_.title != info.streamTitle)
    {
        changed = true;
        this->streamData_.title = info.streamTitle;
    }
    if (this->streamData_.category != info.category.name)
    {
        changed = true;
        this->streamData_.category = info.category.name;
    }

    if (this->streamData_.isLive)
    {
        changed = true;
        this->streamData_.thumbnailUrl = info.stream.thumbnailUrl;
        this->streamData_.viewerCount = info.stream.viewerCount;
        auto uptimeMinutes =
            info.stream.startTime.secsTo(QDateTime::currentDateTime()) / 60;
        this->streamData_.uptime = QString::number(uptimeMinutes / 60) % u"h " %
                                   QString::number(uptimeMinutes % 60) % u"m";
    }

    if (changed)
    {
        this->streamDataChanged.invoke();
    }
}

const KickChannel::StreamData &KickChannel::streamData() const
{
    return this->streamData_;
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

            self->slug_ = res->slug;
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
        srv->registerRoomID(this->roomID(), this->channelID(),
                            this->weakFromThis());
        srv->liveUpdates().joinRoom(this->roomID(), this->channelID());
    }

    if (oldUserID != this->userID())
    {
        this->reloadSeventvEmotes(false);
        this->userIDChanged.invoke();
    }
}

size_t KickChannel::maxBurstMessages() const
{
    // FIXME: this isn't fully tested (maybe these are higher?)
    if (this->hasHighRateLimit())
    {
        return 10;
    }
    return 5;
}

std::chrono::milliseconds KickChannel::minMessageOffset() const
{
    // FIXME: this isn't fully tested
    if (this->hasHighRateLimit())
    {
        return 50ms;
    }
    return 100ms;
}

bool KickChannel::checkMessageRatelimit()
{
    auto now = std::chrono::steady_clock::now();
    auto &timestamps = this->lastMessageTimestamps_;

    // FIXME: haven't tested this fully
    const auto cooldown = 30s;

    // This is mostly identical to the logic in TwitchIrcServer
    if (!timestamps.empty() &&
        timestamps.back() + this->minMessageOffset() > now)
    {
        if (this->lastMessageSpeedErrorTs_ + 30s < now)
        {
            this->addSystemMessage(u"You are sending messages too quickly."_s);
            this->lastMessageSpeedErrorTs_ = now;
        }
        return false;
    }

    // remove messages older than `cooldown`
    while (!timestamps.empty() && timestamps.front() + cooldown < now)
    {
        timestamps.pop();
    }

    // check if you are sending too many messages
    if (timestamps.size() >= this->maxBurstMessages())
    {
        if (this->lastMessageAmountErrorTs_ + 30s < now)
        {
            this->addSystemMessage(u"You are sending too many messages."_s);

            this->lastMessageAmountErrorTs_ = now;
        }
        return false;
    }

    timestamps.push(now);
    return true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- might need some state later
QString KickChannel::prepareMessage(const QString &message) const
{
    const QString baseMessage = getApp()
                                    ->getEmotes()
                                    ->getEmojis()
                                    ->replaceShortCodes(message)
                                    .simplified();

    // We need to manually add the emotes. They're in the format
    // "[emote:{id}:{name}]". If the name doesn't match the emote name, Kick
    // will reject the message.
    auto globalEmotes = getApp()->getKickChatServer()->globalEmotes();
    QString outMessage;
    const QChar *lastEnd = nullptr;
    for (QStringView word : baseMessage.tokenize(u' '))
    {
        EmoteName emote{word.toString()};  // FIXME: get rid of this
        auto it = globalEmotes->find(emote);
        if (it == globalEmotes->end())
        {
            continue;
        }

        if (lastEnd)
        {
            outMessage += QStringView(lastEnd, word.begin());
        }
        else if (word.begin() != baseMessage.begin())
        {
            outMessage += QStringView(baseMessage.begin(), word.begin());
        }

        lastEnd = word.end();
        outMessage += u"[emote:";
        outMessage += it->second->id.string;
        outMessage += ':';
        outMessage += it->second->name.string;
        outMessage += ']';
    }

    if (lastEnd)
    {
        outMessage += QStringView(lastEnd, baseMessage.end());
    }
    else
    {
        // no emote added
        outMessage = baseMessage;
    }
    return outMessage;
}

void KickChannel::updateSevenTVActivity()
{
    const auto currentSeventvUserID =
        getApp()->getAccounts()->kick.current()->seventvUserID();
    if (currentSeventvUserID.isEmpty())
    {
        return;
    }

    if (!getSettings()->enableSevenTVEventAPI ||
        !getSettings()->sendSevenTVActivity)
    {
        return;
    }

    if (this->nextSeventvActivity_.isValid() &&
        QDateTime::currentDateTimeUtc() < this->nextSeventvActivity_)
    {
        return;
    }
    // Make sure to not send activity again before receiving the response
    this->nextSeventvActivity_ = this->nextSeventvActivity_.addSecs(300);

    qCDebug(chatterinoSeventv) << "Sending activity in" << this->getName();

    getApp()->getSeventvAPI()->updateKickPresence(
        this->userID(), currentSeventvUserID,
        [weak = this->weakFromThis()]() {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            self->nextSeventvActivity_ =
                QDateTime::currentDateTimeUtc().addSecs(60);
        },
        [](const auto &result) {
            qCDebug(chatterinoSeventv)
                << "Failed to update 7TV activity:" << result.formatError();
        });
}

void KickChannel::addLoginMessage()
{
    auto builder = MessageBuilder();
    builder->flags.set(MessageFlag::System);
    builder->flags.set(MessageFlag::DoNotTriggerNotification);

    builder.emplace<TimestampElement>();
    builder.emplace<TextElement>(
        u"You need to log in to send messages. You can link your "_s
        "Kick account",
        MessageElementFlag::Text, MessageColor::System);
    builder
        .emplace<TextElement>(u"in the settings."_s, MessageElementFlag::Text,
                              MessageColor::Link)
        ->setLink({Link::OpenAccountsPage, {}});

    this->addMessage(builder.release(), MessageContext::Original);
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
