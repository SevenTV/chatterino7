#pragma once

#include "common/Channel.hpp"
#include "util/MultiChannelIndicatorMode.hpp"

#include <pajlada/signals/scoped-connection.hpp>

#include <vector>

namespace chatterino {

struct ChildChannelDescriptor;

class MultiChannel : public Channel
{
public:
    enum class Platform : uint8_t {
        Twitch,
        Kick,
    };

    struct Spec {
        Platform platform = Platform::Twitch;
        QString name;

        ChildChannelDescriptor descriptor() const;
        static std::optional<Spec> fromDescriptor(
            const ChildChannelDescriptor &descriptor);

        friend QDataStream &operator<<(QDataStream &stream, const Spec &spec);
        friend QDataStream &operator>>(QDataStream &stream, Spec &spec);
    };

    MultiChannel(std::span<const Spec> channels,
                 MultiChannelIndicatorMode indicatorMode);

    struct ChildChannel {
        Platform platform;
        ChannelPtr channel;
        std::vector<pajlada::Signals::ScopedConnection> connections;

        Spec spec() const;
        ChildChannelDescriptor descriptor() const;
    };

    std::span<const ChildChannel> channels() const;

    const QString &getDisplayName() const override;
    const QString &getLocalizedName() const override;

    pajlada::Signals::NoArgSignal activeChannelChanged;
    const ChildChannel *activeChannel() const;
    size_t activeChannelIndex() const;
    void setActiveChannelIndex(size_t index);

    bool isEmpty() const override;

    bool canSendMessage() const override;
    bool isWritable() const override;
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasModRights() const override;
    bool hasHighRateLimit() const override;
    bool isLive() const override;
    bool isRerun() const override;
    bool shouldIgnoreHighlights() const override;
    bool canReconnect() const override;
    void reconnect() override;
    QString getCurrentStreamID() const override;

    MultiChannelIndicatorMode indicatorMode() const;

private:
    void refreshDisplayName();
    void setComputedName(const QString &name);

    QString uniqueName;
    QString computedName;
    std::vector<ChildChannel> channels_;
    size_t activeChannel_ = 0;

    MultiChannelIndicatorMode indicatorMode_ =
        MultiChannelIndicatorMode::PlatformBadgeIfUnselected;
};

}  // namespace chatterino

Q_DECLARE_METATYPE(chatterino::MultiChannel::Platform)
Q_DECLARE_METATYPE(chatterino::MultiChannel::Spec)
