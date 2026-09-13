// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <string_view>

namespace chatterino::kick::ws {

bool stripPrefix(std::string_view &str, std::string_view prefix);

bool stripSuffix(std::string_view &str, std::string_view suffix);

struct IDs {
    uint64_t roomID = 0;
    uint64_t channelID = 0;
};

IDs parseIDs(std::string_view channel);

}  // namespace chatterino::kick::ws
