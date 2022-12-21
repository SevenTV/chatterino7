#pragma once

#include <magic_enum.hpp>

namespace chatterino {

enum class SeventvCosmeticKind {
    Badge,
    Paint,

    INVALID,
};

}

template <>
constexpr magic_enum::customize::customize_t
    magic_enum::customize::enum_name<chatterino::SeventvCosmeticKind>(
        chatterino::SeventvCosmeticKind value) noexcept
{
    switch (value)
    {
        case chatterino::SeventvCosmeticKind::Badge:
            return "BADGE";
        case chatterino::SeventvCosmeticKind::Paint:
            return "PAINT";

        default:
            return default_tag;
    }
}
