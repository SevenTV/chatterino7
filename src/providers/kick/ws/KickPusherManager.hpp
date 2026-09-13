// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/ws/KickWebSocketManager.hpp"

#include <memory>

namespace chatterino {

class KickPusherManagerPrivate;

class KickPusherManager : public KickWebSocketManager
{
public:
    KickPusherManager(QStringView appKey, QStringView cluster);
    ~KickPusherManager() override;

    void joinChannel(const QString &channelName) override;
    void partChannel(const QString &channelName) override;

private:
    std::unique_ptr<KickPusherManagerPrivate> private_;
};

}  // namespace chatterino
