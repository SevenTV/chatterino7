#pragma once

#include "common/Channel.hpp"

#include <unordered_map>

namespace chatterino {

class MessageThread;

class KickChannel : public Channel
{
public:
    struct UserInit {
        uint64_t roomID = 0;
        uint64_t userID = 0;
        uint64_t channelID = 0;
    };

    KickChannel(const QString &name);
    ~KickChannel() override;

    void initialize(const UserInit &init);

    std::shared_ptr<KickChannel> sharedFromThis();
    std::weak_ptr<KickChannel> weakFromThis();

    const QString &getDisplayName() const override
    {
        return this->displayName_;
    }

    uint64_t roomID() const
    {
        return this->roomID_;
    }
    uint64_t userID() const
    {
        return this->userID_;
    }
    uint64_t channelID() const
    {
        return this->channelID_;
    }

    /// Get the thread for the given message
    /// If no thread can be found for the message, create one
    std::shared_ptr<MessageThread> getOrCreateThread(const QString &messageID);

    friend QDebug operator<<(QDebug dbg, const KickChannel &chan);

private:
    /// Message ID -> thread
    std::unordered_map<QString, std::weak_ptr<MessageThread>> threads_;

    uint64_t roomID_ = 0;
    uint64_t userID_ = 0;
    uint64_t channelID_ = 0;

    void resolveChannelInfo();
    void setUserInfo(UserInit init);

    // Kick usually calls this username
    QString displayName_;
    QString slug_;
};

}  // namespace chatterino
