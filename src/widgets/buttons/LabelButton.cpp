// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/buttons/LabelButton.hpp"

#include <algorithm>

namespace chatterino {

LabelButton::LabelButton(const QString &text, BaseWidget *parent, QSize padding)
    : Button(parent)
    , layout_(this)
    , label_(text)
    , padding_(padding)
    , text_(text)
{
    this->layout_.setContentsMargins(0, 0, 0, 0);
    this->layout_.addWidget(&this->label_);
    this->label_.setAttribute(Qt::WA_TransparentForMouseEvents);
    this->label_.setAlignment(Qt::AlignCenter);

    this->updatePadding();
}

void LabelButton::setText(const QString &text)
{
    this->text_ = text;
    this->updateDisplayedText();
}

QString LabelButton::text() const
{
    return this->text_;
}

QSize LabelButton::padding() const noexcept
{
    return this->padding_;
}

void LabelButton::setPadding(QSize padding)
{
    if (this->padding_ == padding)
    {
        return;
    }

    this->padding_ = padding;
    this->updatePadding();
}

void LabelButton::enableRichText()
{
    this->elideMode_ = Qt::ElideNone;
    this->label_.setTextFormat(Qt::RichText);
    this->updateDisplayedText();
}

void LabelButton::setTextElideMode(Qt::TextElideMode mode)
{
    this->elideMode_ = mode;
    this->label_.setTextFormat(Qt::PlainText);
    this->updateDisplayedText();
}

void LabelButton::paintContent(QPainter &painter)
{
}

void LabelButton::resizeEvent(QResizeEvent *event)
{
    Button::resizeEvent(event);
    this->updateDisplayedText();
}

void LabelButton::updateDisplayedText()
{
    if (this->elideMode_ == Qt::ElideNone)
    {
        this->label_.setText(this->text_);
        return;
    }

    const auto availableWidth =
        std::max(0, this->width() - 2 * this->padding_.width());
    this->label_.setText(this->label_.fontMetrics().elidedText(
        this->text_, this->elideMode_, availableWidth));
}

void LabelButton::updatePadding()
{
    auto x = this->padding_.width();
    auto y = this->padding_.height();
    this->label_.setContentsMargins(x, y, x, y);
    this->updateDisplayedText();
}

}  // namespace chatterino
