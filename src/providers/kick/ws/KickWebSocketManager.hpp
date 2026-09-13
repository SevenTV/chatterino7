// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

class KickWebSocketManager
{
public:
    virtual ~KickWebSocketManager() = default;

    virtual void joinChannel(QString channelName) = 0;
    virtual void partChannel(QString channelName) = 0;
};

}  // namespace chatterino
