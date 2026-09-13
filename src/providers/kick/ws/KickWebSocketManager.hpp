// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

class KickWebSocketManager
{
public:
    KickWebSocketManager() = default;
    virtual ~KickWebSocketManager() = default;

    Q_DISABLE_COPY_MOVE(KickWebSocketManager);

    virtual void joinChannel(const QString &channelName) = 0;
    virtual void partChannel(const QString &channelName) = 0;
};

}  // namespace chatterino
