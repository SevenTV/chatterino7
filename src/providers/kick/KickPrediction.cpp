// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickPrediction.hpp"

#include "util/BoostJsonWrap.hpp"

#include <numeric>

namespace {

using namespace chatterino;

KickPrediction::State parseState(std::string_view state)
{
    if (state == "ACTIVE")
    {
        return KickPrediction::State::Active;
    }
    if (state == "LOCKED")
    {
        return KickPrediction::State::Locked;
    }
    if (state == "RESOLVED")
    {
        return KickPrediction::State::Resolved;
    }
    if (state == "CANCELLED")
    {
        return KickPrediction::State::Cancelled;
    }
    return KickPrediction::State::Unknown;
}

QDateTime parseTimestamp(const QString &raw)
{
    if (raw.isEmpty())
    {
        return {};
    }
    return QDateTime::fromString(raw, Qt::ISODateWithMs);
}

}  // namespace

namespace chatterino {

std::optional<KickPrediction> KickPrediction::parse(BoostJsonObject prediction)
{
    auto id = prediction["id"].toQString();
    if (id.isEmpty())
    {
        return std::nullopt;
    }

    KickPrediction pred{
        .id = std::move(id),
        .title = prediction["title"].toQString(),
        .state = parseState(prediction["state"].toStringView()),
        .duration = std::chrono::seconds{prediction["duration"].toInt64()},
        .createdAt = parseTimestamp(prediction["created_at"].toQString()),
        .updatedAt = parseTimestamp(prediction["updated_at"].toQString()),
        .lockedAt = parseTimestamp(prediction["locked_at"].toQString()),
        .winningOutcomeID = prediction["winning_outcome_id"].toQString(),
    };

    for (auto outcomeValue : prediction["outcomes"].toArray())
    {
        auto outcome = outcomeValue.toObject();
        pred.outcomes.push_back({
            .id = outcome["id"].toQString(),
            .title = outcome["title"].toQString(),
            .totalVoteAmount = outcome["total_vote_amount"].toUint64(),
            .voteCount = outcome["vote_count"].toUint64(),
            .returnRate = outcome["return_rate"].toDouble(),
        });
    }

    return pred;
}

QDateTime KickPrediction::votingEndsAt() const
{
    if (!this->createdAt.isValid() || this->duration.count() <= 0)
    {
        return {};
    }
    return this->createdAt.addSecs(this->duration.count());
}

uint64_t KickPrediction::totalVoteAmount() const
{
    return std::accumulate(this->outcomes.begin(), this->outcomes.end(),
                           uint64_t{0}, [](uint64_t acc, const auto &outcome) {
                               return acc + outcome.totalVoteAmount;
                           });
}

bool KickPrediction::isFinished() const
{
    return this->state == State::Resolved || this->state == State::Cancelled;
}

const KickPredictionOutcome *KickPrediction::winningOutcome() const
{
    if (this->winningOutcomeID.isEmpty())
    {
        return nullptr;
    }
    for (const auto &outcome : this->outcomes)
    {
        if (outcome.id == this->winningOutcomeID)
        {
            return &outcome;
        }
    }
    return nullptr;
}

}  // namespace chatterino
