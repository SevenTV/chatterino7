#pragma once

#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickLiveUpdates.hpp"
#include "util/QStringHash.hpp"  // IWYU pragma: keep

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>

namespace chatterino {

class BoostJsonObject;
class KickLiveUpdates;

class KickChatServer : public std::enable_shared_from_this<KickChatServer>
{
public:
    KickChatServer();
    ~KickChatServer();

    Q_DISABLE_COPY_MOVE(KickChatServer)

    std::shared_ptr<KickChannel> findByRoomID(uint64_t roomID) const;
    std::shared_ptr<KickChannel> findBySlug(const QString &slug) const;

    std::shared_ptr<Channel> getOrCreate(
        const QString &slug, const KickChannel::UserInit &init = {});

    void onChatMessage(uint64_t roomID, BoostJsonObject data) const;
    void onJoin(uint64_t roomID) const;

    KickLiveUpdates &liveUpdates()
    {
        return this->liveUpdates_;
    }

private:
    void registerRoomID(uint64_t roomID, std::weak_ptr<KickChannel> chan);

    boost::unordered_flat_map<uint64_t, std::weak_ptr<KickChannel>>
        channelsByRoomID;
    boost::unordered_flat_map<QString, std::weak_ptr<KickChannel>>
        channelsBySlug;

    KickLiveUpdates liveUpdates_;

    friend class ChatServerListener;
    friend KickChannel;
};

}  // namespace chatterino
