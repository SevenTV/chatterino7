// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/KickPredictionWidget.hpp"

#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickPrediction.hpp"
#include "singletons/Theme.hpp"
#include "util/Helpers.hpp"
#include "widgets/buttons/DrawnButton.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;
using namespace Qt::Literals;

namespace {

using namespace chatterino;

constexpr auto MUTED_STYLE = "color: #adadb8;";
constexpr auto BAR_RADIUS = 3.0;
constexpr int BAR_PADDING = 6;
constexpr auto FINISHED_LINGER = 30s;
constexpr auto LOCKED_LINGER = std::chrono::minutes{15};

QColor outcomeColor(size_t index)
{
    static const std::array<QColor, 5> COLORS{
        QColor(u"#387aff"_s), QColor(u"#f5009b"_s), QColor(u"#00b87a"_s),
        QColor(u"#ff9800"_s), QColor(u"#9146ff"_s),
    };
    return COLORS.at(index % COLORS.size());
}

QString formatReturnRate(double rate)
{
    if (rate <= 0.0)
    {
        return {};
    }

    auto formatted = QString::number(rate, 'f', 2);
    while (formatted.endsWith(u'0'))
    {
        formatted.chop(1);
    }
    if (formatted.endsWith(u'.'))
    {
        formatted.chop(1);
    }
    return formatted + u"x"_s;
}

QDateTime lockedSince(const KickPrediction &prediction)
{
    return prediction.lockedAt.isValid() ? prediction.lockedAt
                                         : prediction.updatedAt;
}

QString formatCountdown(qint64 millis)
{
    const qint64 totalSecs = (millis + 999) / 1000;
    const qint64 hours = totalSecs / 3600;
    const qint64 mins = (totalSecs % 3600) / 60;
    const qint64 secs = totalSecs % 60;

    if (hours > 0)
    {
        return u"%1:%2:%3"_s.arg(hours)
            .arg(mins, 2, 10, QChar(u'0'))
            .arg(secs, 2, 10, QChar(u'0'));
    }
    return u"%1:%2"_s.arg(mins).arg(secs, 2, 10, QChar(u'0'));
}

}  // namespace

namespace chatterino {

/// A single outcome, drawn as a bar filled in proportion to its share of the
/// votes with the outcome's name and tallies laid over it.
class PredictionOutcomeBar final : public QWidget
{
public:
    explicit PredictionOutcomeBar(QWidget *parent)
        : QWidget(parent)
    {
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setOutcome(const KickPredictionOutcome &outcome, uint64_t totalVotes,
                    const QColor &color, bool dimmed, bool winner)
    {
        this->title_ = outcome.title;
        this->color_ = color;
        this->dimmed_ = dimmed;
        this->winner_ = winner;
        this->ratio_ = totalVotes == 0 ? 0.0
                                       : double(outcome.totalVoteAmount) /
                                             double(totalVotes);

        QStringList stats;
        stats << u"%1%"_s.arg(int(std::lround(this->ratio_ * 100.0)));
        if (outcome.voteCount > 0)
        {
            stats << u"%1 %2"_s.arg(
                localizeNumbers(outcome.voteCount),
                outcome.voteCount == 1 ? u"voter"_s : u"voters"_s);
        }
        auto rate = formatReturnRate(outcome.returnRate);
        if (!rate.isEmpty())
        {
            stats << rate;
        }
        this->stats_ = stats.join(u" · "_s);

        this->update();
    }

    void setScale(float scale)
    {
        QFont f = this->font();
        f.setPointSizeF(9.5F * scale);
        this->setFont(f);
        this->setFixedHeight(int(22 * scale));
    }

private:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        auto *theme = getTheme();
        const QRectF box = this->rect();

        // Tint rather than saturate, so the label stays legible on both the
        // filled and unfilled parts of the bar in either theme.
        QColor track = theme->messages.textColors.regular;
        track.setAlpha(28);
        QColor fill = this->color_;
        fill.setAlpha(this->dimmed_ ? 55 : (this->winner_ ? 190 : 120));

        QPainterPath clip;
        clip.addRoundedRect(box, BAR_RADIUS, BAR_RADIUS);
        painter.setClipPath(clip);
        painter.fillRect(box, track);
        painter.fillRect(QRectF(box.left(), box.top(),
                                box.width() * this->ratio_, box.height()),
                         fill);
        painter.setClipping(false);

        QColor text = theme->messages.textColors.regular;
        if (this->dimmed_)
        {
            text.setAlpha(140);
        }
        painter.setPen(text);

        QFont font = this->font();
        font.setBold(this->winner_);
        painter.setFont(font);

        const QFontMetrics metrics(font);
        QRectF inner = box.adjusted(BAR_PADDING, 0, -BAR_PADDING, 0);

        const int statsWidth = metrics.horizontalAdvance(this->stats_);
        painter.drawText(inner, Qt::AlignRight | Qt::AlignVCenter,
                         this->stats_);

        const auto titleWidth =
            std::max(0, int(inner.width()) - statsWidth - BAR_PADDING);
        painter.drawText(
            inner, Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(this->title_, Qt::ElideRight, titleWidth));
    }

    QString title_;
    QString stats_;
    double ratio_ = 0.0;
    QColor color_;
    bool dimmed_ = false;
    bool winner_ = false;
};

KickPredictionWidget::KickPredictionWidget(QWidget *parent)
    : BaseWidget(parent)
    , stateLabel_(new QLabel(this))
    , countdownLabel_(new QLabel(this))
    , titleLabel_(new QLabel(this))
    , closeButton_(
          new DrawnButton(DrawnButton::Symbol::Cross, {.padding = 5}, this))
    , countdownTimer_(new QTimer(this))
    , autoHideTimer_(new QTimer(this))
{
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *outerBox = new QVBoxLayout(this);
    outerBox->setContentsMargins(0, 0, 0, 0);
    outerBox->setSpacing(0);

    auto *contentBox = new QVBoxLayout();
    contentBox->setContentsMargins(8, 6, 8, 6);
    contentBox->setSpacing(3);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(4);
    headerRow->addWidget(this->stateLabel_);
    headerRow->addStretch(1);
    this->countdownLabel_->setStyleSheet(MUTED_STYLE);
    headerRow->addWidget(this->countdownLabel_);

    this->closeButton_->setScaleIndependentSize(18, 18);
    this->closeButton_->setToolTip(u"Hide this prediction"_s);
    QObject::connect(this->closeButton_, &Button::leftClicked, this, [this] {
        this->dismissCurrent();
    });
    headerRow->addWidget(this->closeButton_);
    contentBox->addLayout(headerRow);

    this->titleLabel_->setWordWrap(true);
    this->titleLabel_->setTextFormat(Qt::PlainText);
    this->titleLabel_->setStyleSheet("background: transparent;");
    contentBox->addWidget(this->titleLabel_);

    this->outcomeBox_ = new QVBoxLayout();
    this->outcomeBox_->setContentsMargins(0, 2, 0, 0);
    this->outcomeBox_->setSpacing(2);
    contentBox->addLayout(this->outcomeBox_);

    outerBox->addLayout(contentBox);

    // 1px bottom border - separates the banner from the chat view below
    auto *bottomBorder = new QWidget(this);
    bottomBorder->setFixedHeight(1);
    bottomBorder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bottomBorder->setAutoFillBackground(true);
    {
        QPalette pal = bottomBorder->palette();
        pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
        bottomBorder->setPalette(pal);
    }
    outerBox->addWidget(bottomBorder);

    this->countdownTimer_->setInterval(1s);
    QObject::connect(this->countdownTimer_, &QTimer::timeout, this, [this] {
        this->tickCountdown();
    });

    this->autoHideTimer_->setSingleShot(true);
    QObject::connect(this->autoHideTimer_, &QTimer::timeout, this, [this] {
        this->hide();
    });

    this->scaleChangedEvent(this->scale());
    this->hide();
}

void KickPredictionWidget::setChannel(KickChannel *channel)
{
    this->signalHolder_.clear();
    this->channel_ = channel;
    this->autoHideTimer_->stop();
    this->dismissedID_.clear();

    if (channel)
    {
        this->signalHolder_.managedConnect(channel->predictionChanged, [this] {
            this->refresh();
        });
    }

    this->refresh();
}

void KickPredictionWidget::refresh()
{
    const auto *prediction =
        this->channel_ ? this->channel_->currentPrediction() : nullptr;
    // Also covers one that settled long ago, so switching back to the channel
    // doesn't resurface a stale result.
    if (!prediction || this->hasExpired(*prediction) ||
        prediction->id == this->dismissedID_)
    {
        this->countdownTimer_->stop();
        this->autoHideTimer_->stop();
        this->hide();
        return;
    }

    std::vector<QString> ids;
    ids.reserve(prediction->outcomes.size());
    for (const auto &outcome : prediction->outcomes)
    {
        ids.push_back(outcome.id);
    }
    this->rebuildOutcomes(ids);

    this->titleLabel_->setText(prediction->title);
    this->titleLabel_->setVisible(!prediction->title.isEmpty());

    const auto total = prediction->totalVoteAmount();
    const bool cancelled =
        prediction->state == KickPrediction::State::Cancelled;
    const bool resolved = prediction->state == KickPrediction::State::Resolved;
    for (size_t i = 0; i < prediction->outcomes.size(); i++)
    {
        const auto &outcome = prediction->outcomes[i];
        const bool winner =
            resolved && outcome.id == prediction->winningOutcomeID;
        this->outcomeBars_[i]->setOutcome(outcome, total, outcomeColor(i),
                                          cancelled || (resolved && !winner),
                                          winner);
    }

    this->updateHeader();

    this->countdownTimer_->stop();
    this->countdownLabel_->hide();
    this->votingEndsAt_ = {};
    if (prediction->state == KickPrediction::State::Active)
    {
        this->votingEndsAt_ = prediction->votingEndsAt();
        if (this->votingEndsAt_.isValid())
        {
            this->tickCountdown();
            this->countdownTimer_->start();
        }
    }

    this->autoHideTimer_->stop();
    if (prediction->isFinished())
    {
        this->autoHideTimer_->start(FINISHED_LINGER);
    }
    else if (prediction->state == KickPrediction::State::Locked)
    {
        auto remaining = std::chrono::milliseconds{LOCKED_LINGER}.count();
        auto since = lockedSince(*prediction);
        if (since.isValid())
        {
            remaining -= since.msecsTo(QDateTime::currentDateTimeUtc());
        }
        this->autoHideTimer_->start(
            std::chrono::milliseconds{std::max<qint64>(0, remaining)});
    }

    this->show();
}

void KickPredictionWidget::updateHeader()
{
    const auto *prediction =
        this->channel_ ? this->channel_->currentPrediction() : nullptr;
    if (!prediction)
    {
        return;
    }

    QString state;
    switch (prediction->state)
    {
        case KickPrediction::State::Active:
            state = u"Prediction"_s;
            break;
        case KickPrediction::State::Locked:
            state = u"Prediction · voting closed"_s;
            break;
        case KickPrediction::State::Resolved: {
            const auto *winner = prediction->winningOutcome();
            state = winner ? u"Prediction · <b>%1</b> won"_s.arg(
                                 winner->title.toHtmlEscaped())
                           : u"Prediction · resolved"_s;
        }
        break;
        case KickPrediction::State::Cancelled:
            state = u"Prediction · cancelled"_s;
            break;
        case KickPrediction::State::Unknown:
            state = u"Prediction"_s;
            break;
    }
    this->stateLabel_->setText(state);
}

void KickPredictionWidget::dismissCurrent()
{
    const auto *prediction =
        this->channel_ ? this->channel_->currentPrediction() : nullptr;
    if (prediction)
    {
        this->dismissedID_ = prediction->id;
    }

    this->countdownTimer_->stop();
    this->autoHideTimer_->stop();
    this->hide();
}

bool KickPredictionWidget::hasExpired(const KickPrediction &prediction) const
{
    const auto now = QDateTime::currentDateTimeUtc();

    if (prediction.isFinished())
    {
        return prediction.updatedAt.isValid() &&
               prediction.updatedAt.secsTo(now) > FINISHED_LINGER.count();
    }

    if (prediction.state == KickPrediction::State::Locked)
    {
        auto since = lockedSince(prediction);
        return since.isValid() &&
               since.secsTo(now) > std::chrono::seconds{LOCKED_LINGER}.count();
    }

    return false;
}

void KickPredictionWidget::rebuildOutcomes(const std::vector<QString> &ids)
{
    if (ids == this->outcomeIDs_)
    {
        return;
    }
    this->outcomeIDs_ = ids;

    for (auto *bar : this->outcomeBars_)
    {
        this->outcomeBox_->removeWidget(bar);
        bar->deleteLater();
    }
    this->outcomeBars_.clear();

    for (size_t i = 0; i < ids.size(); i++)
    {
        auto *bar = new PredictionOutcomeBar(this);
        bar->setScale(this->scale());
        this->outcomeBox_->addWidget(bar);
        this->outcomeBars_.push_back(bar);
    }
}

void KickPredictionWidget::tickCountdown()
{
    const auto remaining =
        QDateTime::currentDateTime().msecsTo(this->votingEndsAt_);
    if (remaining <= 0)
    {
        // Kick sends a LOCKED update of its own; this only stops the clock.
        this->countdownTimer_->stop();
        this->countdownLabel_->hide();
        return;
    }

    this->countdownLabel_->setText(
        u"locks in %1"_s.arg(formatCountdown(remaining)));
    this->countdownLabel_->show();
}

void KickPredictionWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    auto *theme = getTheme();

    painter.fillRect(event->rect(), theme->splits.header.background);

    painter.setPen(theme->splits.header.border);
    painter.drawLine(0, 0, this->width() - 1, 0);
}

void KickPredictionWidget::scaleChangedEvent(float newScale)
{
    QFont headerFont = this->stateLabel_->font();
    headerFont.setPointSizeF(9.5F * newScale);
    this->stateLabel_->setFont(headerFont);
    this->countdownLabel_->setFont(headerFont);

    QFont titleFont = this->titleLabel_->font();
    titleFont.setPointSizeF(11.0F * newScale);
    this->titleLabel_->setFont(titleFont);

    for (auto *bar : this->outcomeBars_)
    {
        bar->setScale(newScale);
    }
}

void KickPredictionWidget::mousePressEvent(QMouseEvent * /*event*/)
{
    // ignore to disable the parent's right click menu
}

}  // namespace chatterino
