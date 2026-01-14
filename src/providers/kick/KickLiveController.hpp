#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

#include <span>
#include <vector>

namespace chatterino {

class KickChatServer;
class KickChannel;

class KickLiveController : public QObject
{
public:
    KickLiveController(KickChatServer &chatServer);
    ~KickLiveController() override;

    Q_DISABLE_COPY_MOVE(KickLiveController);

    void queueNewChannel(uint64_t userID);

private:
    void refreshImmediate();
    void refreshAll();

    void refreshList(std::span<uint64_t> userIDs);

    KickChatServer &chatServer;

    QTimer immediateTimer;
    QTimer refreshTimer;

    std::vector<uint64_t> immediateChannels;
};

}  // namespace chatterino
