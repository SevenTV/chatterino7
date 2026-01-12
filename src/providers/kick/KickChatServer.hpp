#pragma once

#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickLiveUpdates.hpp"
#include "util/FunctionRef.hpp"
#include "util/QStringHash.hpp"  // IWYU pragma: keep

#include <boost/unordered/unordered_flat_map.hpp>
#include <pajlada/signals/signalholder.hpp>

#include <memory>

namespace chatterino {

class BoostJsonObject;
class KickLiveUpdates;
class SeventvEventAPI;

class KickChatServer : public QObject
{
public:
    KickChatServer();
    ~KickChatServer() override;

    Q_DISABLE_COPY_MOVE(KickChatServer)

    void initialize();

    std::shared_ptr<KickChannel> findByRoomID(uint64_t roomID) const;
    std::shared_ptr<KickChannel> findBySlug(const QString &slug) const;

    void forEachChannel(FunctionRef<void(KickChannel &channel)> cb);
    void forEachSeventvEmoteSet(const QString &emoteSetID,
                                FunctionRef<void(KickChannel &channel)> cb);
    void forEachSeventvUser(const QString &seventvUserID,
                            FunctionRef<void(KickChannel &channel)> cb);

    std::shared_ptr<Channel> getOrCreate(
        const QString &slug, const KickChannel::UserInit &init = {});

    bool onAppEvent(uint64_t roomID, std::string_view event,
                    BoostJsonObject data);

    void onJoin(uint64_t roomID) const;

    KickLiveUpdates &liveUpdates()
    {
        return this->liveUpdates_;
    }

private:
    void registerRoomID(uint64_t roomID, std::weak_ptr<KickChannel> chan);

    void initializeSeventvEventApi(SeventvEventAPI *api);

    void onChatMessage(KickChannel *channel, BoostJsonObject data);
    void onUserBanned(KickChannel *channel, BoostJsonObject data);
    void onUserUnbanned(KickChannel *channel, BoostJsonObject data);
    void onMessageDeleted(KickChannel *channel, BoostJsonObject data);
    void onChatroomClear(KickChannel *channel, BoostJsonObject data);

    boost::unordered_flat_map<uint64_t, std::weak_ptr<KickChannel>>
        channelsByRoomID;
    boost::unordered_flat_map<QString, std::weak_ptr<KickChannel>>
        channelsBySlug;

    KickLiveUpdates liveUpdates_;

    pajlada::Signals::SignalHolder signalHolder_;

    friend class ChatServerListener;
    friend KickChannel;
};

}  // namespace chatterino
