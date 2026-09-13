// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <variant>

namespace chatterino::variant {

/// Compile-time safe visitor for std and boost variants.
///
/// From https://en.cppreference.com/w/cpp/utility/variant/visit
///
/// Usage:
///
/// ```
/// std::variant<int, double> v;
/// std::visit(variant::Overloaded{
///     [](double) { qDebug() << "double"; },
///     [](int) { qDebug() << "int"; }
/// }, v);
/// ```
template <class... Ts>
struct [[nodiscard]] Overloaded : Ts... {
    using Ts::operator()...;

    constexpr decltype(auto) visit(auto &&v) &
    {
        return std::visit(*this, std::forward<decltype(v)>(v));
    }

    constexpr decltype(auto) visit(auto &&v) const &
    {
        return std::visit(*this, std::forward<decltype(v)>(v));
    }

    constexpr decltype(auto) visit(auto &&v) &&
    {
        return std::visit(std::move(*this), std::forward<decltype(v)>(v));
    }

    constexpr decltype(auto) visit(auto &&v) const &&
    {
        return std::visit(std::move(*this), std::forward<decltype(v)>(v));
    }
};

// Technically, we shouldn't need this, as we're on C++ 20,
// but not all of our compilers support CTAD for aggregates yet.
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace chatterino::variant
