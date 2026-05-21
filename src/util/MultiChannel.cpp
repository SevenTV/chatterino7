#include "util/MultiChannel.hpp"

#include "Application.hpp"
#include "common/WindowDescriptors.hpp"
#include "messages/Message.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "util/QCompareTransparent.hpp"
#include "util/QMagicEnum.hpp"

#include <QUuid>
#include <QVarLengthArray>

#include <set>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

QString makeChannelName(const std::set<QString, QCompareCaseInsensitive> &known,
                        bool space)
{
    if (known.empty())
    {
        return u"Multichannel[empty]"_s;
    }

    bool first = true;
    QString combined;
    for (const auto &name : known)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            combined += u',';
            if (space)
            {
                combined += u' ';
            }
        }
        combined += name;
    }
    return combined;
}

QString makeChannelName(std::span<const MultiChannel::Spec> specs, bool space)
{
    std::set<QString, QCompareCaseInsensitive> known;
    for (const auto &spec : specs)
    {
        known.emplace(spec.name);
    }
    return makeChannelName(known, space);
}

QString makeChannelName(std::span<const MultiChannel::ChildChannel> specs,
                        bool space)
{
    std::set<QString, QCompareCaseInsensitive> known;
    for (const auto &spec : specs)
    {
        known.emplace(spec.channel->getName());
    }
    return makeChannelName(known, space);
}

ChannelPtr resolveChannel(const MultiChannel::Spec &spec)
{
    switch (spec.platform)
    {
        case MultiChannel::Platform::Twitch:
            return getApp()->getTwitch()->getOrAddChannel(spec.name);
        case MultiChannel::Platform::Kick:
            return getApp()->getKickChatServer()->getOrCreate(spec.name);
    }
    return Channel::getEmpty();
}

}  // namespace

namespace chatterino {

ChildChannelDescriptor MultiChannel::Spec::descriptor() const
{
    return ChildChannelDescriptor{
        .platform = qmagicenum::enumNameString(this->platform),
        .channelName = this->name,
    };
}

std::optional<MultiChannel::Spec> MultiChannel::Spec::fromDescriptor(
    const ChildChannelDescriptor &descriptor)
{
    auto platform = qmagicenum::enumCast<Platform>(descriptor.platform);
    if (!platform)
    {
        return std::nullopt;
    }
    return MultiChannel::Spec{
        .platform = *platform,
        .name = descriptor.channelName,
    };
}

QDataStream &operator<<(QDataStream &stream, const MultiChannel::Spec &spec)
{
    stream << spec.platform;
    stream << spec.name;
    return stream;
}

QDataStream &operator>>(QDataStream &stream, MultiChannel::Spec &spec)
{
    stream >> spec.platform;
    stream >> spec.name;
    return stream;
}

MultiChannel::Spec MultiChannel::ChildChannel::spec() const
{
    return {
        .platform = this->platform,
        .name = this->channel->getName(),
    };
}

ChildChannelDescriptor MultiChannel::ChildChannel::descriptor() const
{
    return this->spec().descriptor();
}

MultiChannel::MultiChannel(std::span<const Spec> channels,
                           MultiChannelIndicatorMode indicatorMode)
    : Channel(makeChannelName(channels, false), Type::Multi)
    , indicatorMode_(indicatorMode)
{
    for (const auto &spec : channels)
    {
        auto channel = resolveChannel(spec);
        std::vector<pajlada::Signals::ScopedConnection> connections;
        connections.emplace_back(channel->messageAppended.connect(
            [this](const auto &ptr, auto flags) {
                this->addMessage(ptr, MessageContext::Repost, flags);
            }));
        connections.emplace_back(
            channel->messagesAddedAtStart.connect([this](const auto &msgs) {
                this->addMessagesAtStart(msgs);
            }));
        connections.emplace_back(channel->messageReplaced.connect(
            [this](size_t idx, const MessagePtr &prev,
                   const MessagePtr &replaced) {
                this->replaceMessage(idx, prev, replaced);
            }));
        connections.emplace_back(
            channel->filledInMessages.connect([this](const auto &msgs) {
                this->fillInMissingMessages(msgs);
            }));
        connections.emplace_back(channel->displayNameChanged.connect([this] {
            this->refreshDisplayName();
        }));
        // Ignore messagesCleared - we'd need to figure out which messages to clear.

        this->channels_.emplace_back(ChildChannel{
            .platform = spec.platform,
            .channel = std::move(channel),
            .connections = std::move(connections),
        });
    }
    this->refreshDisplayName();

    QVarLengthArray<std::vector<MessagePtr>, 4> snapshots;
    QVarLengthArray<std::span<const MessagePtr>, 4> snapshotViews;
    for (const auto &chan : this->channels_)
    {
        snapshotViews.emplace_back(
            snapshots.emplace_back(chan.channel->getMessageSnapshot()));
    }
    this->mergeFrom(snapshotViews);
}

const QString &MultiChannel::getDisplayName() const
{
    return this->computedName;
}

const QString &MultiChannel::getLocalizedName() const
{
    return this->computedName;
}

std::span<const MultiChannel::ChildChannel> MultiChannel::channels() const
{
    return this->channels_;
}

const MultiChannel::ChildChannel *MultiChannel::activeChannel() const
{
    if (this->activeChannel_ >= this->channels_.size())
    {
        return nullptr;
    }
    return &this->channels_[this->activeChannel_];
}

size_t MultiChannel::activeChannelIndex() const
{
    return this->activeChannel_;
}

void MultiChannel::setActiveChannelIndex(size_t index)
{
    if (this->activeChannel_ == index)
    {
        return;
    }
    this->activeChannel_ = std::clamp<size_t>(index, 0, this->channels_.size());
    this->activeChannelChanged.invoke();
}

bool MultiChannel::isEmpty() const
{
    return false;
}

bool MultiChannel::canSendMessage() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->canSendMessage();
    }
    return false;
}

bool MultiChannel::isWritable() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->isWritable();
    }
    return false;
}

void MultiChannel::sendMessage(const QString &message)
{
    const auto *active = this->activeChannel();
    if (active)
    {
        active->channel->sendMessage(message);
    }
}

bool MultiChannel::isMod() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->isMod();
    }
    return false;
}

bool MultiChannel::isBroadcaster() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->isBroadcaster();
    }
    return false;
}

bool MultiChannel::hasModRights() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->hasModRights();
    }
    return false;
}

bool MultiChannel::hasHighRateLimit() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->hasHighRateLimit();
    }
    return false;
}

bool MultiChannel::isLive() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->isLive();
    }
    return false;
}

bool MultiChannel::isRerun() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->isRerun();
    }
    return false;
}

bool MultiChannel::shouldIgnoreHighlights() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->shouldIgnoreHighlights();
    }
    return false;
}

bool MultiChannel::canReconnect() const
{
    return true;
}

void MultiChannel::reconnect()
{
    for (const auto &chan : this->channels_)
    {
        chan.channel->reconnect();
    }
}

QString MultiChannel::getCurrentStreamID() const
{
    const auto *active = this->activeChannel();
    if (active)
    {
        return active->channel->getCurrentStreamID();
    }
    return {};
}

MultiChannelIndicatorMode MultiChannel::indicatorMode() const
{
    return this->indicatorMode_;
}

void MultiChannel::refreshDisplayName()
{
    if (this->channels_.empty())
    {
        this->setComputedName(u"empty"_s);
        return;
    }
    this->setComputedName(makeChannelName(this->channels_, true));
}

void MultiChannel::setComputedName(const QString &name)
{
    if (this->computedName == name)
    {
        return;
    }
    this->computedName = name;
    this->displayNameChanged.invoke();
}

bool platformMatches(MessagePlatform lhs, MultiChannel::Platform rhs) noexcept
{
    switch (lhs)
    {
        case MessagePlatform::AnyOrTwitch:
            return rhs == MultiChannel::Platform::Twitch;
        case MessagePlatform::Kick:
            return rhs == MultiChannel::Platform::Kick;
    }
    return false;
}

}  // namespace chatterino
