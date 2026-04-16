#pragma once

#include <cstdint>

namespace chatterino {

enum class MultiChannelIndicatorMode : uint8_t {
    None,
    PlatformBadgeIfUnselected,
    PlatformBadgeAlways,
    ChannelName,
};

}  // namespace chatterino
