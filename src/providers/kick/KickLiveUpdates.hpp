#pragma once

#include <QString>

#include <memory>

namespace chatterino {

class KickLiveUpdatesPrivate;
class KickLiveUpdates
{
public:
    KickLiveUpdates();
    ~KickLiveUpdates();

    Q_DISABLE_COPY_MOVE(KickLiveUpdates)

    void joinRoom(uint64_t roomID, uint64_t channelID);
    void leaveRoom(uint64_t roomID, uint64_t channelID);

private:
    std::shared_ptr<KickLiveUpdatesPrivate> private_;

    friend KickLiveUpdatesPrivate;
};

}  // namespace chatterino
