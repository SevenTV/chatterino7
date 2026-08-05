// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace chatterino {

class BoostJsonObject;

struct KickPredictionOutcome {
    QString id;
    QString title;
    /// Points staked here across all voters.
    uint64_t totalVoteAmount = 0;
    /// Number of voters, not points.
    uint64_t voteCount = 0;
    /// Payout multiplier, e.g. 1.75 returns 1.75x a winning stake. Zero while
    /// nothing is staked here.
    double returnRate = 0.0;

    auto operator<=>(const KickPredictionOutcome &other) const = default;
};

/// A prediction as described by Kick's `PredictionCreated`/`PredictionUpdated`
/// events. Both carry a complete snapshot, so this replaces previously known
/// state rather than being merged into it.
struct KickPrediction {
    enum class State : uint8_t {
        Active,
        Locked,
        Resolved,
        Cancelled,
        Unknown,
    };

    QString id;
    QString title;
    State state = State::Unknown;
    std::vector<KickPredictionOutcome> outcomes;
    /// How long voting stays open, measured from `createdAt`.
    std::chrono::seconds duration{0};
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime lockedAt;
    QString winningOutcomeID;

    /// Parses the inner `prediction` object of a Kick prediction event.
    /// Returns `nullopt` if the payload has no usable id.
    static std::optional<KickPrediction> parse(BoostJsonObject prediction);

    /// Invalid if `createdAt` or `duration` is missing.
    QDateTime votingEndsAt() const;

    uint64_t totalVoteAmount() const;
    bool isFinished() const;
    const KickPredictionOutcome *winningOutcome() const;

    auto operator<=>(const KickPredictionOutcome &other) const = default;
};

}  // namespace chatterino
