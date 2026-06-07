// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/SettingsDialogTab.hpp"

#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/settingspages/SettingsPage.hpp"

#include <QPainter>

namespace chatterino {

SettingsDialogTab::SettingsDialogTab(SettingsDialog *_dialog,
                                     std::function<SettingsPage *()> _lazyPage,
                                     const QString &name, QString imageFileName,
                                     SettingsTabId id)
    : BaseWidget(_dialog)
    , dialog_(_dialog)
    , lazyPage_(std::move(_lazyPage))
    , id_(id)
    , name_(name)
{
    this->ui_.labelText = name;
    this->ui_.icon.addFile(imageFileName);

    this->setCursor(QCursor(Qt::PointingHandCursor));
    this->setMouseTracking(true);
}

void SettingsDialogTab::setSelected(bool _selected)
{
    if (this->selected_ == _selected)
    {
        return;
    }

    this->selected_ = _selected;
    this->update();
    this->selectedChanged(this->selected_);
}

SettingsPage *SettingsDialogTab::page()
{
    if (this->page_)
    {
        return this->page_;
    }

    this->page_ = this->lazyPage_();
    this->page_->setTab(this);
    return this->page_;
}

void SettingsDialogTab::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.fillRect(rect(), palette().color(QPalette::Window));

    if (this->selected_)
    {
        painter.fillRect(rect(), QColor(255, 255, 255, 22));
        painter.fillRect(0, 0, 3, this->height(), QColor(79, 195, 247));
    }
    else if (this->hovered_)
    {
        painter.fillRect(rect(), QColor(255, 255, 255, 12));
    }

    const int iconSize = 16;
    const int pad = (this->height() - iconSize) / 2;
    painter.setOpacity(this->selected_ ? 1.0 : 0.6);
    painter.drawPixmap(pad + 2, pad,
                       this->ui_.icon.pixmap(QSize(iconSize, iconSize)));
    painter.setOpacity(1.0);

    const int textX = pad + 2 + iconSize + 8;
    painter.setPen(this->selected_ ? QColor(0xee, 0xee, 0xee)
                                   : QColor(0xaa, 0xaa, 0xaa));
    painter.drawText(QRect(textX, 0, this->width() - textX - 4, this->height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     this->ui_.labelText);
}

void SettingsDialogTab::enterEvent(QEnterEvent *event)
{
    this->hovered_ = true;
    this->update();
    BaseWidget::enterEvent(event);
}

void SettingsDialogTab::leaveEvent(QEvent *event)
{
    this->hovered_ = false;
    this->update();
    BaseWidget::leaveEvent(event);
}

void SettingsDialogTab::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    this->dialog_->selectTab(this);

    this->setFocus();
}

const QString &SettingsDialogTab::name() const
{
    return this->name_;
}

SettingsTabId SettingsDialogTab::id() const
{
    return this->id_;
}

}  // namespace chatterino
