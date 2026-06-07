#pragma once

#include "providers/kick/KickChannel.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QString>

namespace chatterino {

/// Format room modes into a comma-separated string (e.g. "emote, sub, ")
QString formatRoomModeUnclean(const TwitchChannel::RoomModes &modes);
QString formatRoomModeUnclean(const KickChannel::RoomModes &modes);
/// Trim trailing comma/space and add line breaks for long mode lists
void cleanRoomModeText(QString &text, bool hasModRights);

}  // namespace chatterino
