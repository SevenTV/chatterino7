#pragma once

#include "common/Atomic.hpp"
#include "common/Channel.hpp"
#include "common/ChannelChatters.hpp"

#include <unordered_map>

namespace chatterino {

namespace seventv::eventapi {
struct EmoteAddDispatch;
struct EmoteRemoveDispatch;
struct EmoteUpdateDispatch;
struct UserConnectionUpdateDispatch;
}  // namespace seventv::eventapi

class MessageThread;
class EmoteMap;

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

struct EmoteName;

class KickChannel : public Channel, public ChannelChatters
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

    void reloadSeventvEmotes(bool manualRefresh);

    std::shared_ptr<const EmoteMap> seventvEmotes() const;
    EmotePtr seventvEmote(const EmoteName &name) const;

    void addSeventvEmote(const seventv::eventapi::EmoteAddDispatch &dispatch);

    void updateSeventvEmote(
        const seventv::eventapi::EmoteUpdateDispatch &dispatch);
    void removeSeventvEmote(
        const seventv::eventapi::EmoteRemoveDispatch &dispatch);
    void updateSeventvUser(
        const seventv::eventapi::UserConnectionUpdateDispatch &dispatch);

    const QString &seventvUserID() const;
    const QString &seventvEmoteSetID() const;

    friend QDebug operator<<(QDebug dbg, const KickChannel &chan);

private:
    /// Message ID -> thread
    std::unordered_map<QString, std::weak_ptr<MessageThread>> threads_;

    uint64_t roomID_ = 0;
    uint64_t userID_ = 0;
    uint64_t channelID_ = 0;

    void resolveChannelInfo();
    void setUserInfo(UserInit init);

    void updateSeventvData(const QString &newUserID,
                           const QString &newEmoteSetID);
    void addOrReplaceSeventvAddRemove(bool isEmoteAdd, const QString &actor,
                                      const QString &emoteName);
    bool tryReplaceLastSeventvAddOrRemove(MessageFlag op, const QString &actor,
                                          const QString &emoteName);

    // Kick usually calls this username
    QString displayName_;

    Atomic<std::shared_ptr<const EmoteMap>> seventvEmotes_;

    QString seventvUserID_;
    QString seventvEmoteSetID_;
    size_t seventvKickConnectionIndex_ = 0;
    /// The actor name of the last 7TV emote update.
    QString lastSeventvEmoteActor_;
    /// A weak reference to the last 7TV emote update message.
    std::weak_ptr<const Message> lastSeventvMessage_;
    /// A list of the emotes listed in the lat 7TV emote update message.
    std::vector<QString> lastSeventvEmoteNames_;
};

}  // namespace chatterino
