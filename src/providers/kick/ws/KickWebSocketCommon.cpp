// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/ws/KickWebSocketCommon.hpp"

#include <charconv>

namespace chatterino::kick::ws {

bool stripPrefix(std::string_view &str, std::string_view prefix)
{
    if (str.starts_with(prefix))
    {
        str = str.substr(prefix.size());
        return true;
    }
    return false;
}

bool stripSuffix(std::string_view &str, std::string_view suffix)
{
    if (str.ends_with(suffix))
    {
        str = str.substr(0, str.size() - suffix.size());
        return true;
    }
    return false;
}

IDs parseIDs(std::string_view channel)
{
    bool isChannel = false;
    if (stripPrefix(channel, "chatrooms.") || stripPrefix(channel, "chatroom_"))
    {
        stripSuffix(channel, ".v2");
    }
    else if (stripPrefix(channel, "channel_") ||
             stripPrefix(channel, "channel.") ||
             stripPrefix(channel, "predictions-channel-"))
    {
        isChannel = true;
    }

    uint64_t v = 0;
    std::from_chars(channel.data(), channel.data() + channel.size(), v);

    if (isChannel)
    {
        return IDs{.channelID = v};
    }
    return IDs{.roomID = v};
}

}  // namespace chatterino::kick::ws
