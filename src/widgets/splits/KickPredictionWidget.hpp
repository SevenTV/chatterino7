// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QString>
#include <QTimer>

#include <vector>

class QLabel;
class QVBoxLayout;

namespace chatterino {

class KickChannel;
class PredictionOutcomeBar;
class DrawnButton;
struct KickPrediction;

/**
 * Banner shown between the split header and the chat view that displays the
 * channel's running prediction, updating live as votes come in.
 */
class KickPredictionWidget final : public BaseWidget
{
    Q_OBJECT

public:
    explicit KickPredictionWidget(QWidget *parent = nullptr);

    // Pass nullptr to detach from any channel.
    void setChannel(KickChannel *channel);

protected:
    void scaleChangedEvent(float newScale) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void paintEvent(QPaintEvent *event) override;
    void refresh();
    void tickCountdown();
    /// Rebuilds the outcome rows, reusing them when the outcomes themselves
    /// haven't changed so incoming votes don't cause flicker.
    void rebuildOutcomes(const std::vector<QString> &ids);
    void updateHeader();
    /// True once a settled prediction is old enough that switching back to the
    /// channel shouldn't resurface it.
    bool hasExpired(const KickPrediction &prediction) const;
    /// Hides the banner until the channel starts a different prediction.
    void dismissCurrent();

    KickChannel *channel_ = nullptr;
    pajlada::Signals::SignalHolder signalHolder_;

    QLabel *stateLabel_ = nullptr;
    QLabel *countdownLabel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    DrawnButton *closeButton_ = nullptr;
    QVBoxLayout *outcomeBox_ = nullptr;
    std::vector<PredictionOutcomeBar *> outcomeBars_;
    /// Outcome ids currently laid out, to detect when a rebuild is needed.
    std::vector<QString> outcomeIDs_;

    QTimer *countdownTimer_ = nullptr;
    QTimer *autoHideTimer_ = nullptr;

    /// Invalid when voting isn't open.
    QDateTime votingEndsAt_;
    QString dismissedID_;
};

}  // namespace chatterino
