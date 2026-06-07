// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/NotebookTab.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/Common.hpp"
#include "common/QLogging.hpp"
#include "controllers/hotkeys/HotkeyCategory.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Helpers.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/DraggedSplit.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "messages/Image.hpp"
#include "common/Aliases.hpp"

#include <boost/bind/bind.hpp>
#include <boost/container_hash/hash.hpp>
#include <QAbstractAnimation>
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace chatterino {
namespace {
// Translates the given rectangle by an amount in the direction to appear like the tab is selected.
// For example, if location is Top, the rectangle will be translated in the negative Y direction,
// or "up" on the screen, by amount.
void translateRectForLocation(QRect &rect, NotebookTabLocation location,
                              int amount)
{
    switch (location)
    {
        case NotebookTabLocation::Top:
            rect.translate(0, -amount);
            break;
        case NotebookTabLocation::Left:
            rect.translate(-amount, 0);
            break;
        case NotebookTabLocation::Right:
            rect.translate(amount, 0);
            break;
        case NotebookTabLocation::Bottom:
            rect.translate(0, amount);
            break;
    }
}

float getCompactDivider(TabStyle tabStyle)
{
    switch (tabStyle)
    {
        case TabStyle::Compact:
            return 1.5;
        case TabStyle::Normal:
        default:
            return 1.0;
    }
}

float getCompactReducer(TabStyle tabStyle)
{
    switch (tabStyle)
    {
        case TabStyle::Compact:
            return 4.0;
        case TabStyle::Normal:
        default:
            return 0.0;
    }
}
}  // namespace

NotebookTab::NotebookTab(Notebook *notebook)
    : Button(notebook)
    , positionChangedAnimation_(this, "pos")
    , notebook_(notebook)
    , menu_(this)
{
    this->setContentCacheEnabled(false);
    this->setAcceptDrops(true);

    this->positionChangedAnimation_.setEasingCurve(
        QEasingCurve(QEasingCurve::InCubic));

    getSettings()->showTabCloseButton.connect(
        [this] {
            this->tabSizeChanged();
        },
        this->managedConnections_);
    getSettings()->tabStyle.connect(
        [this] {
            this->tabSizeChanged();
        },
        this->managedConnections_);
    getSettings()->showTabLive.connect(
        [this](auto, auto) {
            this->update();
        },
        this->managedConnections_);

    this->setMouseTracking(true);

    this->tooltipWidget_ = new TooltipWidget(this);

    this->menu_.addAction("Rename Tab", [this]() {
        this->showRenameDialog();
    });

    // XXX: this doesn't update after changing hotkeys

    this->menu_.addAction("Close Tab",
                          getApp()->getHotkeys()->getDisplaySequence(
                              HotkeyCategory::Window, "removeTab"),
                          [this]() {
                              this->notebook_->removePage(this->page);
                          });

    this->closeMultipleTabsMenu_ = new QMenu("Close Multiple Tabs", this);

    this->menu_.addMenu(this->closeMultipleTabsMenu_);
    getSettings()->tabDirection.connect(
        [this](int val) {
            this->recreateCloseMultipleTabsMenu(
                static_cast<NotebookTabLocation>(val));
        },
        this->signalHolder_);

    this->menu_.addAction(
        "Popup Tab",
        getApp()->getHotkeys()->getDisplaySequence(HotkeyCategory::Window,
                                                   "popup", {{"window"}}),
        [this]() {
            if (auto *container = dynamic_cast<SplitContainer *>(this->page))
            {
                container->popup();
            }
        });

    this->menu_.addAction("Duplicate Tab", [this]() {
        this->notebook_->duplicatePage(this->page);
    });

    this->highlightNewMessagesAction_ =
        new QAction("Mark Tab as Unread on New Messages", &this->menu_);
    this->highlightNewMessagesAction_->setCheckable(true);
    this->highlightNewMessagesAction_->setChecked(this->highlightEnabled_);
    QObject::connect(this->highlightNewMessagesAction_, &QAction::triggered,
                     [this](bool checked) {
                         this->highlightEnabled_ = checked;
                     });
    this->menu_.addAction(this->highlightNewMessagesAction_);

    this->menu_.addSeparator();

    this->notebook_->addNotebookActionsToMenu(&this->menu_);

    // Add split-specific actions dynamically before the menu shows
    this->splitMenuSeparator_ = this->menu_.addSeparator();
    this->splitMenuSeparator_->setVisible(false);
    QObject::connect(&this->menu_, &QMenu::aboutToShow, this, [this] {
        // Remove previously-added dynamic split actions
        for (auto *action : this->dynamicSplitActions_)
        {
            this->menu_.removeAction(action);
        }
        this->dynamicSplitActions_.clear();
        this->splitMenuSeparator_->setVisible(false);

        auto *container = dynamic_cast<SplitContainer *>(this->page);
        if (!container)
        {
            return;
        }
        auto *split = container->getSelectedSplit();
        if (!split)
        {
            return;
        }

        this->splitMenuSeparator_->setVisible(true);

        auto *changeChannel = this->menu_.addAction(
            "Change channel",
            getApp()->getHotkeys()->getDisplaySequence(
                HotkeyCategory::Split, "changeChannel"),
            split, &Split::changeChannel);
        this->dynamicSplitActions_.append(changeChannel);

        auto *closeSplit = this->menu_.addAction(
            "Close split",
            getApp()->getHotkeys()->getDisplaySequence(
                HotkeyCategory::Split, "delete"),
            split, &Split::deleteFromContainer);
        this->dynamicSplitActions_.append(closeSplit);

        auto *popup = this->menu_.addAction(
            "Popup split",
            getApp()->getHotkeys()->getDisplaySequence(
                HotkeyCategory::Window, "popup", {{"split"}}),
            split, &Split::popup);
        this->dynamicSplitActions_.append(popup);

        auto *search = this->menu_.addAction(
            "Search",
            getApp()->getHotkeys()->getDisplaySequence(
                HotkeyCategory::Split, "showSearch"),
            this, [split] {
                split->showSearch(true);
            });
        this->dynamicSplitActions_.append(search);
    });
}

void NotebookTab::recreateCloseMultipleTabsMenu(
    const NotebookTabLocation tabLocation)
{
    this->closeMultipleTabsMenu_->clear();

    this->closeMultipleTabsMenu_->addAction("Close All Visible Tabs", [this]() {
        auto reply = QMessageBox::question(
            this, "Close All Visible Tabs",
            "Are you sure you want to close all visible tabs?",
            QMessageBox::Yes | QMessageBox::Cancel);

        if (reply != QMessageBox::Yes)
        {
            return;
        }

        for (int i = this->notebook_->getPageCount() - 1; i >= 0; --i)
        {
            auto *page = this->notebook_->getPageAt(i);

            auto *container = dynamic_cast<SplitContainer *>(page);
            if (!container)
            {
                continue;
            }

            auto *tab = container->getTab();
            if (!tab || !tab->isVisible())
            {
                continue;
            }

            this->notebook_->removePage(page);
        }
    });

    QString beforeSelectedName;
    QString afterSelectedName;
    switch (tabLocation)
    {
        case Top:
        case Bottom:
            beforeSelectedName = "Left";
            afterSelectedName = "Right";
            break;
        case Left:
        case Right:
            beforeSelectedName = "Top";
            afterSelectedName = "Bottom";
            break;
    }

    this->closeTabsBeforeSelectedAction_ =
        this->closeMultipleTabsMenu_->addAction(
            "Close Visible Tabs to " + beforeSelectedName,
            [this, beforeSelectedName]() {
                auto reply = QMessageBox::question(
                    this, "Close Visible Tabs to " + beforeSelectedName,
                    "Are you sure you want to close all visible tabs to the " +
                        beforeSelectedName.toLower() + "?",
                    QMessageBox::Yes | QMessageBox::Cancel);

                if (reply != QMessageBox::Yes)
                {
                    return;
                }

                std::vector<QWidget *> pagesToRemove;
                for (int i = 0; i < this->notebook_->getPageCount(); ++i)
                {
                    auto *page = this->notebook_->getPageAt(i);
                    if (page == this->page)
                    {
                        break;
                    }

                    auto *container = dynamic_cast<SplitContainer *>(page);
                    if (!container)
                    {
                        continue;
                    }

                    auto *tab = container->getTab();
                    if (!tab || !tab->isVisible())
                    {
                        continue;
                    }

                    pagesToRemove.push_back(page);
                }

                for (auto *page : pagesToRemove)
                {
                    this->notebook_->removePage(page);
                }
            });

    this->closeTabsAfterSelectedAction_ =
        this->closeMultipleTabsMenu_->addAction(
            "Close Visible Tabs to " + afterSelectedName,
            [this, afterSelectedName]() {
                auto reply = QMessageBox::question(
                    this, "Close Visible Tabs to " + afterSelectedName,
                    "Are you sure you want to close all visible tabs to the " +
                        afterSelectedName + "?",
                    QMessageBox::Yes | QMessageBox::Cancel);

                if (reply != QMessageBox::Yes)
                {
                    return;
                }

                for (int i = this->notebook_->getPageCount() - 1; i >= 0; --i)
                {
                    auto *p = this->notebook_->getPageAt(i);
                    if (p == this->page)
                    {
                        break;
                    }

                    auto *container = dynamic_cast<SplitContainer *>(p);
                    if (!container)
                    {
                        continue;
                    }

                    auto *tab = container->getTab();
                    if (!tab || !tab->isVisible())
                    {
                        continue;
                    }

                    this->notebook_->removePage(p);
                }
            });

    this->closeMultipleTabsMenu_->addAction(
        "Close Other Visible Tabs", [this]() {
            auto reply = QMessageBox::question(
                this, "Close Other Visible Tabs",
                "Are you sure you want to close all other visible tabs?",
                QMessageBox::Yes | QMessageBox::Cancel);

            if (reply != QMessageBox::Yes)
            {
                return;
            }

            for (int i = this->notebook_->getPageCount() - 1; i >= 0; --i)
            {
                auto *p = this->notebook_->getPageAt(i);
                if (p == this->page)
                {
                    continue;
                }

                auto *container = dynamic_cast<SplitContainer *>(p);
                if (!container)
                {
                    continue;
                }

                auto *tab = container->getTab();
                if (!tab || !tab->isVisible())
                {
                    continue;
                }

                this->notebook_->removePage(p);
            }
        });
}

void NotebookTab::showRenameDialog()
{
    auto *dialog = new QDialog(this);

    auto *vbox = new QVBoxLayout;

    auto *lineEdit = new QLineEdit;
    lineEdit->setText(this->getCustomTitle());
    lineEdit->setPlaceholderText(this->getDefaultTitle());
    lineEdit->selectAll();

    vbox->addWidget(new QLabel("Name:"));
    vbox->addWidget(lineEdit);
    vbox->addStretch(1);

    auto *buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    vbox->addWidget(buttonBox);
    dialog->setLayout(vbox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, [dialog] {
        dialog->accept();
        dialog->close();
    });

    QObject::connect(buttonBox, &QDialogButtonBox::rejected, [dialog] {
        dialog->reject();
        dialog->close();
    });

    dialog->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    dialog->setMinimumSize(dialog->minimumSizeHint().width() + 50,
                           dialog->minimumSizeHint().height() + 10);

    dialog->setWindowFlags(
        (dialog->windowFlags() & ~(Qt::WindowContextHelpButtonHint)) |
        Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);

    dialog->setWindowTitle("Rename Tab");

    if (dialog->exec() == QDialog::Accepted)
    {
        QString newTitle = lineEdit->text();
        this->setCustomTitle(newTitle);
    }
}

void NotebookTab::themeChangedEvent()
{
    this->update();

    //    this->setMouseEffectColor(QColor("#999"));
    this->setMouseEffectColor(this->theme->tabs.regular.text);
}

void NotebookTab::growWidth(int width)
{
    if (this->growWidth_ != width)
    {
        this->growWidth_ = width;
        this->updateSize();
    }
    else
    {
        this->growWidth_ = width;
    }
}

int NotebookTab::normalTabWidth() const
{
    return this->normalTabWidthForHeight(this->height());
}

int NotebookTab::normalTabWidthForHeight(int height) const
{
    float scale = this->scale();
    int width = 0;

    auto metrics =
        getApp()->getFonts()->getFontMetrics(FontStyle::UiTabs, scale);

    float compactDivider = getCompactDivider(getSettings()->tabStyle);
    if (this->hasXButton())
    {
        width = static_cast<int>(metrics.horizontalAdvance(this->getTitle()) +
                                 (40 / compactDivider * scale));
    }
    else
    {
        width = static_cast<int>(metrics.horizontalAdvance(this->getTitle()) +
                                 (24 / compactDivider * scale));
    }

    if (static_cast<float>(height) > 150 * scale)
    {
        width = height;
    }
    else
    {
        width = std::clamp(width, height, static_cast<int>(150 * scale));
    }

    return width;
}

void NotebookTab::updateSize()
{
    float scale = this->scale();
    auto height = static_cast<int>(NOTEBOOK_TAB_HEIGHT * scale);
    int width = this->normalTabWidthForHeight(height);

    if (width < this->growWidth_)
    {
        width = this->growWidth_;
    }

    if (this->width() != width || this->height() != height)
    {
        this->resize(width, height);
        this->notebook_->refresh();
    }
}

const QString &NotebookTab::getCustomTitle() const
{
    return this->customTitle_;
}

void NotebookTab::setCustomTitle(const QString &newTitle)
{
    if (this->customTitle_ != newTitle)
    {
        this->customTitle_ = newTitle;
        this->titleUpdated();
    }
}

void NotebookTab::resetCustomTitle()
{
    this->setCustomTitle(QString());
}

bool NotebookTab::hasCustomTitle() const
{
    return !this->customTitle_.isEmpty();
}

void NotebookTab::setDefaultTitle(const QString &title)
{
    if (this->defaultTitle_ != title)
    {
        this->defaultTitle_ = title;

        if (this->customTitle_.isEmpty())
        {
            this->titleUpdated();
        }
    }
}

const QString &NotebookTab::getDefaultTitle() const
{
    return this->defaultTitle_;
}

const QString &NotebookTab::getTitle() const
{
    return this->customTitle_.isEmpty() ? this->defaultTitle_
                                        : this->customTitle_;
}

void NotebookTab::titleUpdated()
{
    // Queue up save because: Tab title changed
    getApp()->getWindows()->queueSave();
    this->notebook_->refresh();
    this->updateSize();
    this->update();
}

bool NotebookTab::isSelected() const
{
    return this->selected_;
}

void NotebookTab::removeHighlightStateChangeSources(
    const HighlightSources &toRemove)
{
    for (const auto &[source, _] : toRemove)
    {
        this->removeHighlightSource(source);
    }
}

void NotebookTab::removeHighlightSource(
    const ChannelView::ChannelViewID &source)
{
    this->highlightSources_.erase(source);
}

void NotebookTab::newHighlightSourceAdded(const ChannelView &channelViewSource)
{
    auto channelViewId = channelViewSource.getID();
    this->removeHighlightSource(channelViewId);
    this->updateHighlightStateDueSourcesChange();

    auto *splitNotebook = dynamic_cast<SplitNotebook *>(this->notebook_);
    if (splitNotebook)
    {
        for (int i = 0; i < splitNotebook->getPageCount(); ++i)
        {
            auto *splitContainer =
                dynamic_cast<SplitContainer *>(splitNotebook->getPageAt(i));
            if (splitContainer)
            {
                auto *tab = splitContainer->getTab();
                if (tab && tab != this)
                {
                    tab->removeHighlightSource(channelViewId);
                    tab->updateHighlightStateDueSourcesChange();
                }
            }
        }
    }
}

void NotebookTab::updateHighlightStateDueSourcesChange()
{
    if (std::ranges::any_of(this->highlightSources_, [](const auto &keyval) {
            return keyval.second == HighlightState::Highlighted;
        }))
    {
        assert(this->highlightState_ == HighlightState::Highlighted);
        return;
    }

    if (std::ranges::any_of(this->highlightSources_, [](const auto &keyval) {
            return keyval.second == HighlightState::NewMessage;
        }))
    {
        if (this->highlightState_ != HighlightState::NewMessage)
        {
            this->highlightState_ = HighlightState::NewMessage;
            this->update();
            this->notebook_->updateBadgeCount();
        }
    }
    else
    {
        if (this->highlightState_ != HighlightState::None)
        {
            this->highlightState_ = HighlightState::None;
            this->update();
            this->notebook_->updateBadgeCount();
        }
    }

    assert(this->highlightState_ != HighlightState::Highlighted);
}

void NotebookTab::copyHighlightStateAndSourcesFrom(const NotebookTab *sourceTab)
{
    if (this->isSelected())
    {
        assert(this->highlightSources_.empty());
        assert(this->highlightState_ == HighlightState::None);
        return;
    }

    this->highlightSources_ = sourceTab->highlightSources_;

    if (!this->highlightEnabled_ &&
        sourceTab->highlightState_ == HighlightState::NewMessage)
    {
        return;
    }

    if (this->highlightState_ == sourceTab->highlightState_ ||
        this->highlightState_ == HighlightState::Highlighted)
    {
        return;
    }

    this->highlightState_ = sourceTab->highlightState_;
    this->update();
}

void NotebookTab::setSelected(bool value)
{
    this->selected_ = value;

    if (value)
    {
        auto *splitNotebook = dynamic_cast<SplitNotebook *>(this->notebook_);
        if (splitNotebook)
        {
            for (int i = 0; i < splitNotebook->getPageCount(); ++i)
            {
                auto *splitContainer =
                    dynamic_cast<SplitContainer *>(splitNotebook->getPageAt(i));
                if (splitContainer)
                {
                    auto *tab = splitContainer->getTab();
                    if (tab && tab != this)
                    {
                        tab->removeHighlightStateChangeSources(
                            this->highlightSources_);
                        tab->updateHighlightStateDueSourcesChange();
                    }
                }
            }
        }
    }

    this->highlightSources_.clear();
    this->highlightState_ = HighlightState::None;

    this->update();
}

void NotebookTab::setInLastRow(bool value)
{
    if (this->isInLastRow_ != value)
    {
        this->isInLastRow_ = value;
        this->update();
    }
}

void NotebookTab::setTabLocation(NotebookTabLocation location)
{
    if (this->tabLocation_ != location)
    {
        this->tabLocation_ = location;
        this->update();
    }
}

bool NotebookTab::setRerun(bool isRerun)
{
    if (this->isRerun_ != isRerun)
    {
        this->isRerun_ = isRerun;
        this->update();
        return true;
    }

    return false;
}

bool NotebookTab::setLive(bool isLive)
{
    if (this->isLive_ != isLive)
    {
        this->isLive_ = isLive;
        this->update();
        return true;
    }

    return false;
}

bool NotebookTab::isLive() const
{
    return this->isLive_;
}

HighlightState NotebookTab::highlightState() const
{
    return this->highlightState_;
}

void NotebookTab::setHighlightState(HighlightState newHighlightStyle)
{
    if (this->isSelected())
    {
        assert(this->highlightSources_.empty());
        assert(this->highlightState_ == HighlightState::None);
        return;
    }

    this->highlightSources_.clear();

    if (!this->highlightEnabled_ &&
        newHighlightStyle == HighlightState::NewMessage)
    {
        return;
    }

    if (this->highlightState_ == newHighlightStyle ||
        this->highlightState_ == HighlightState::Highlighted)
    {
        return;
    }

    this->highlightState_ = newHighlightStyle;
    this->update();
    this->notebook_->updateBadgeCount();
}

void NotebookTab::updateHighlightState(HighlightState newHighlightStyle,
                                       const ChannelView &channelViewSource)
{
    if (this->isSelected())
    {
        assert(this->highlightSources_.empty());
        assert(this->highlightState_ == HighlightState::None);
        return;
    }

    if (!this->shouldMessageHighlight(channelViewSource))
    {
        return;
    }

    if (!this->highlightEnabled_ &&
        newHighlightStyle == HighlightState::NewMessage)
    {
        return;
    }

    // message is highlighting unvisible tab

    auto channelViewId = channelViewSource.getID();

    switch (newHighlightStyle)
    {
        case HighlightState::Highlighted:
            // override lower states
            this->highlightSources_.insert_or_assign(channelViewId,
                                                     newHighlightStyle);
        case HighlightState::NewMessage: {
            // only insert if no state already there to avoid overriding
            if (!this->highlightSources_.contains(channelViewId))
            {
                this->highlightSources_.emplace(channelViewId,
                                                newHighlightStyle);
            }
            break;
        }
        case HighlightState::None:
            break;
    }

    if (this->highlightState_ == newHighlightStyle ||
        this->highlightState_ == HighlightState::Highlighted)
    {
        return;
    }

    this->highlightState_ = newHighlightStyle;
    this->update();
}

bool NotebookTab::shouldMessageHighlight(
    const ChannelView &channelViewSource) const
{
    auto *visibleSplitContainer =
        dynamic_cast<SplitContainer *>(this->notebook_->getSelectedPage());
    if (visibleSplitContainer != nullptr)
    {
        const auto &visibleSplits = visibleSplitContainer->getSplits();
        for (const auto &visibleSplit : visibleSplits)
        {
            if (channelViewSource.getID() ==
                visibleSplit->getChannelView().getID())
            {
                return false;
            }
        }
    }

    return true;
}

void NotebookTab::setHighlightsEnabled(const bool &newVal)
{
    this->highlightNewMessagesAction_->setChecked(newVal);
    this->highlightEnabled_ = newVal;
}

bool NotebookTab::hasHighlightsEnabled() const
{
    return this->highlightEnabled_;
}

QRect NotebookTab::getDesiredRect() const
{
    return QRect(this->positionAnimationDesiredPoint_, this->size());
}

void NotebookTab::tabSizeChanged()
{
    this->updateSize();
    this->update();
}

void NotebookTab::moveAnimated(QPoint targetPos, bool animated)
{
    this->positionAnimationDesiredPoint_ = targetPos;

    if (!animated || !this->notebook_->isVisible())
    {
        this->move(targetPos);
        return;
    }

    if (this->positionChangedAnimation_.state() ==
            QAbstractAnimation::Running &&
        this->positionChangedAnimation_.endValue() == targetPos)
    {
        return;
    }

    this->positionChangedAnimation_.stop();
    this->positionChangedAnimation_.setDuration(75);
    this->positionChangedAnimation_.setStartValue(this->pos());
    this->positionChangedAnimation_.setEndValue(targetPos);
    this->positionChangedAnimation_.start();
}

void NotebookTab::paintEvent(QPaintEvent *)
{
    auto *app = getApp();
    QPainter painter(this);
    float scale = this->scale();

    painter.setFont(app->getFonts()->getFont(FontStyle::UiTabs, scale));
    auto metrics = app->getFonts()->getFontMetrics(FontStyle::UiTabs, scale);

    int height = int(scale * NOTEBOOK_TAB_HEIGHT);

    // select the right tab colors
    Theme::TabColors colors;

    if (this->selected_)
    {
        colors = this->theme->tabs.selected;
    }
    else if (this->highlightState_ == HighlightState::Highlighted)
    {
        colors = this->theme->tabs.highlighted;
    }
    else if (this->highlightState_ == HighlightState::NewMessage)
    {
        colors = this->theme->tabs.newMessage;
    }
    else
    {
        colors = this->theme->tabs.regular;
    }

    bool windowFocused = this->window() == QApplication::activeWindow();
    float compactDivider = getCompactDivider(getSettings()->tabStyle);

    int insetH = std::max(3, int(3 * scale));
    int insetV = std::max(2, int(2 * scale));
    QRect pillRect = this->rect().adjusted(insetH, insetV, -insetH, -insetV);
    int radius = std::max(4, int(8 * scale));

    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath pillPath;
    pillPath.addRoundedRect(pillRect, radius, radius);

    if (this->selected_)
    {
        painter.fillPath(pillPath,
                         windowFocused ? colors.backgrounds.regular
                                       : colors.backgrounds.unfocused);
    }
    else if (this->highlightState_ == HighlightState::Highlighted)
    {
        QColor bg = this->theme->tabs.highlighted.backgrounds.regular;
        bg.setAlpha(100);
        painter.fillPath(pillPath, bg);
    }
    else if (this->highlightState_ == HighlightState::NewMessage)
    {
        QColor bg = this->theme->tabs.newMessage.backgrounds.regular;
        bg.setAlpha(80);
        painter.fillPath(pillPath, bg);
    }
    else if (this->mouseOver_)
    {
        QColor bg = colors.backgrounds.regular;
        bg.setAlpha(70);
        painter.fillPath(pillPath, bg);
    }
    if (!this->selected_ &&
        this->highlightState_ != HighlightState::None)
    {
        QColor lineColor = (this->highlightState_ == HighlightState::Highlighted)
                               ? this->theme->tabs.highlighted.line.regular
                               : this->theme->tabs.newMessage.line.regular;
        int lineH = std::max(2, int(2 * scale));
        QRect lineRect(pillRect.left() + radius, pillRect.bottom() - lineH,
                       pillRect.width() - 2 * radius, lineH);

        QPainterPath clipPath;
        clipPath.addRoundedRect(pillRect, radius, radius);
        clipPath.setCachingEnabled(true);
        painter.save();
        painter.setClipPath(clipPath);
        painter.fillRect(lineRect, lineColor);
        painter.restore();
    }

    // draw live indicator
    if ((this->isLive_ || this->isRerun_) && getSettings()->showTabLive)
    {
        QBrush b;
        if (this->isLive_)
        {
            painter.setPen(this->theme->tabs.liveIndicator);
            b.setColor(this->theme->tabs.liveIndicator);
        }
        else
        {
            painter.setPen(this->theme->tabs.rerunIndicator);
            b.setColor(this->theme->tabs.rerunIndicator);
        }

        b.setStyle(Qt::SolidPattern);
        painter.setBrush(b);

        auto x = this->width() - (7 * scale);
        auto y = 4 * scale;
        auto diameter = 4 * scale;
        QRect liveIndicatorRect(x, y, diameter, diameter);
        painter.drawEllipse(liveIndicatorRect);
    }

    // set the pen color
    painter.setPen(colors.text);

    // set area for text
    int rectW =
        (!getSettings()->showTabCloseButton ? 0
                                            : int(16 * scale / compactDivider));
    QRect rect(0, 0, this->width() - rectW, height);

    int offset = int(scale * 4 / compactDivider);
    QRect textRect(offset, -1, this->width() - offset - offset, height);

    if (this->shouldDrawXButton())
    {
        textRect.setRight(textRect.right() - this->height() / 2);
    }

    int width = metrics.horizontalAdvance(this->getTitle());
    Qt::Alignment alignment = width > textRect.width()
                                  ? Qt::AlignLeft | Qt::AlignVCenter
                                  : Qt::AlignHCenter | Qt::AlignVCenter;

    QTextOption option(alignment);
    option.setWrapMode(QTextOption::NoWrap);
    painter.drawText(textRect, this->getTitle(), option);

    // draw close x
    if (this->shouldDrawXButton())
    {
        painter.setRenderHint(QPainter::Antialiasing, false);

        QRect xRect = this->getXRect();
        if (!xRect.isNull())
        {
            painter.setBrush(QColor("#fff"));

            if (this->mouseOverX_)
            {
                painter.fillRect(xRect, QColor(0, 0, 0, 64));

                if (this->mouseDownX_)
                {
                    painter.fillRect(xRect, QColor(0, 0, 0, 64));
                }
            }

            int a = static_cast<int>(scale * 4);

            painter.drawLine(xRect.topLeft() + QPoint(a, a),
                             xRect.bottomRight() + QPoint(-a, -a));
            painter.drawLine(xRect.topRight() + QPoint(-a, a),
                             xRect.bottomLeft() + QPoint(a, -a));
        }
    }

    // draw mouse over effect
    if (!this->selected_)
    {
        this->fancyPaint(painter);
    }

}

bool NotebookTab::hasXButton() const
{
    return getSettings()->showTabCloseButton &&
           this->notebook_->getAllowUserTabManagement();
}

bool NotebookTab::shouldDrawXButton() const
{
    return this->hasXButton() && (this->mouseOver_ || this->selected_);
}

void NotebookTab::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        this->mouseDown_ = true;
        this->mouseDownX_ = this->getXRect().contains(event->pos());

        this->notebook_->select(this->page);
    }

    this->update();

    if (this->notebook_->getAllowUserTabManagement())
    {
        switch (event->button())
        {
            case Qt::RightButton: {
                this->menu_.popup(event->globalPosition().toPoint() +
                                  QPoint(0, 8));

                const int visibleTabCount =
                    this->notebook_->getVisibleTabCount();
                const int selectedTabIndex =
                    this->notebook_->visibleIndexOf(this->page);

                this->closeMultipleTabsMenu_->setEnabled(visibleTabCount > 1);

                this->closeTabsBeforeSelectedAction_->setEnabled(
                    selectedTabIndex > 0);
                this->closeTabsAfterSelectedAction_->setEnabled(
                    selectedTabIndex != -1 &&
                    selectedTabIndex < (visibleTabCount - 1));
            }
            break;
            default:;
        }
    }
}

void NotebookTab::mouseReleaseEvent(QMouseEvent *event)
{
    this->mouseDown_ = false;

    auto removeThisPage = [this] {
        auto reply = QMessageBox::question(
            this, "Remove this tab",
            "Are you sure that you want to remove this tab?",
            QMessageBox::Yes | QMessageBox::Cancel);

        if (reply == QMessageBox::Yes)
        {
            this->notebook_->removePage(this->page);
        }
    };

    if (event->button() == Qt::MiddleButton &&
        this->notebook_->getAllowUserTabManagement())
    {
        if (this->rect().contains(event->pos()))
        {
            removeThisPage();
        }
    }
    else
    {
        if (this->hasXButton() && this->mouseDownX_ &&
            this->getXRect().contains(event->pos()))
        {
            this->mouseDownX_ = false;

            removeThisPage();
        }
        else
        {
            this->update();
        }
    }
}

void NotebookTab::mouseDoubleClickEvent(QMouseEvent *event)
{
    const auto canRenameTab = this->notebook_->getAllowUserTabManagement() &&
                              getSettings()->disableTabRenamingOnClick == false;

    if (event->button() == Qt::LeftButton && canRenameTab)
    {
        this->showRenameDialog();
    }
}

void NotebookTab::enterEvent(QEnterEvent *event)
{
    this->mouseOver_ = true;

    this->update();

    if (!getSettings()->compactHeaders.getValue())
    {
        Button::enterEvent(event);
        return;
    }

    auto showTooltip = [this](ImagePtr image, const QString &text) {
        TooltipEntry entry;
        entry.image = image;
        entry.text = text;
        float s = this->scale();
        if (image)
        {
            entry.customWidth = static_cast<int>(200 * s);
            entry.customHeight = static_cast<int>(113 * s);
        }

        this->tooltipWidget_->setOne(entry);

        if (!image)
        {
            this->tooltipWidget_->capTextWidth(static_cast<int>(200 * s));
        }

        this->tooltipWidget_->adjustSize();

        auto pos = this->mapToGlobal(
            QPoint((this->width() - this->tooltipWidget_->width()) / 2,
                   this->height() + 4));
        this->tooltipWidget_->moveTo(pos,
                                     widgets::BoundsChecking::CursorPosition);
        this->tooltipWidget_->show();
    };

    if (auto *container = dynamic_cast<SplitContainer *>(this->page))
    {
        auto *split = container->getSelectedSplit();
        if (!split && !container->getSplits().empty())
        {
            split = container->getSplits().front();
        }

        if (split)
        {
            auto selectedChannel = split->getSelectedChannel();
            auto channelName = selectedChannel->getLocalizedName();
            ImagePtr previewImage;
            auto fsPx = [this](int base) -> QString {
                return QString::number(
                    std::max(8, static_cast<int>(base * this->scale())));
            };
            QString text =
                QString("<span style='font-size:%1px;'><b>%2</b></span>")
                    .arg(fsPx(12), channelName.toHtmlEscaped());

            if (auto *twitchChannel =
                    dynamic_cast<TwitchChannel *>(selectedChannel.get()))
            {
                const auto status = twitchChannel->accessStreamStatus();
                auto statusColor = status->rerun ? "#ff9800" : "#f44336";
                auto statusText = status->rerun ? "Rerun" : "Live";

                if (status->live)
                {
                    text += QString("<span style='color:%1;'> &middot; %2</span>")
                                .arg(statusColor, statusText);
                    if (getSettings()->headerStreamTitle &&
                        !status->title.isEmpty())
                    {
                        text += QString("<div style='margin-top:3px; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(11), status->title.toHtmlEscaped());
                    }
                    if (getSettings()->headerGame && !status->game.isEmpty())
                    {
                        text += QString("<div style='margin-top:1px; color:#aaa; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(10), status->game.toHtmlEscaped());
                    }
                    QString meta;
                    if (getSettings()->headerUptime)
                    {
                        meta = status->uptime;
                    }
                    if (getSettings()->headerViewerCount)
                    {
                        if (!meta.isEmpty())
                        {
                            meta += " &middot; ";
                        }
                        meta += localizeNumbers(status->viewerCount) +
                                " viewers";
                    }
                    if (!meta.isEmpty())
                    {
                        text += QString(
                                    "<div style='margin-top:2px; color:#888; "
                                    "font-size:%1px;'>%2</div>")
                                    .arg(fsPx(10), meta);
                    }

                    QString previewUrl =
                        QString("https://static-cdn.jtvnw.net/previews-ttv/"
                                "live_user_%1-320x180.jpg")
                            .arg(selectedChannel->getName().toLower());
                    previewImage =
                        Image::fromUrl(Url{previewUrl}, 1, QSize(320, 180));
                }
                else
                {
                    text += "<span style='color:#888;'> &middot; Offline</span>";
                    if (getSettings()->headerStreamTitle &&
                        !status->title.isEmpty())
                    {
                        text += QString("<div style='margin-top:3px; color:#aaa; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(11), status->title.toHtmlEscaped());
                    }
                }
                showTooltip(previewImage, text);
            }
            else if (auto *kickChannel =
                         dynamic_cast<KickChannel *>(selectedChannel.get()))
            {
                const auto &data = kickChannel->streamData();
                if (data.isLive)
                {
                    text += "<span style='color:#f44336;'> &middot; Live</span>";
                    if (getSettings()->headerStreamTitle &&
                        !data.title.isEmpty())
                    {
                        text += QString("<div style='margin-top:3px; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(11), data.title.toHtmlEscaped());
                    }
                    if (getSettings()->headerGame && !data.category.isEmpty())
                    {
                        text += QString("<div style='margin-top:1px; color:#aaa; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(10), data.category.toHtmlEscaped());
                    }
                    QString meta;
                    if (getSettings()->headerUptime)
                    {
                        meta = data.uptime;
                    }
                    if (getSettings()->headerViewerCount)
                    {
                        if (!meta.isEmpty())
                        {
                            meta += " &middot; ";
                        }
                        meta += localizeNumbers(data.viewerCount) +
                                " viewers";
                    }
                    if (!meta.isEmpty())
                    {
                        text += QString(
                                    "<div style='margin-top:2px; color:#888; "
                                    "font-size:%1px;'>%2</div>")
                                    .arg(fsPx(10), meta);
                    }
                }
                else
                {
                    text += "<span style='color:#888;'> &middot; Offline</span>";
                    if (getSettings()->headerStreamTitle &&
                        !data.title.isEmpty())
                    {
                        text += QString("<div style='margin-top:3px; color:#aaa; font-size:%1px;'>%2</div>")
                                    .arg(fsPx(11), data.title.toHtmlEscaped());
                    }
                }
                showTooltip(previewImage, text);
            }
            else
            {
                auto typeName = [](Channel::Type t) -> QString {
                    switch (t)
                    {
                        case Channel::Type::TwitchWhispers:
                            return "Whispers";
                        case Channel::Type::TwitchWatching:
                            return "Watching";
                        case Channel::Type::TwitchMentions:
                            return "Mentions";
                        case Channel::Type::TwitchLive:
                            return "Live";
                        case Channel::Type::TwitchAutomod:
                            return "Automod";
                        default:
                            return "";
                    }
                };
                auto extra = typeName(selectedChannel->getType());
                if (!extra.isEmpty())
                {
                    text += QString("<span style='color:#888;'> &middot; %1</span>")
                                .arg(extra);
                }
                showTooltip(nullptr, text);
            }
        }
        else
        {
            showTooltip(nullptr, this->getTitle());
        }
    }
    else
    {
        showTooltip(nullptr, this->getTitle());
    }

    Button::enterEvent(event);
}

void NotebookTab::leaveEvent(QEvent *event)
{
    this->mouseOverX_ = false;
    this->mouseOver_ = false;

    this->tooltipWidget_->hide();

    this->update();

    Button::leaveEvent(event);
}

void NotebookTab::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasFormat("chatterino/split"))
    {
        return;
    }

    if (!isDraggingSplit())
    {
        // Ensure dragging a split from a different Chatterino instance doesn't switch tabs around
        return;
    }

    event->acceptProposedAction();

    if (this->notebook_->getAllowUserTabManagement())
    {
        this->notebook_->select(this->page);
    }
}

void NotebookTab::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat("chatterino/split"))
    {
        return;
    }

    if (!isDraggingSplit())
    {
        // Ensure dragging a split from a different Chatterino instance doesn't switch tabs around
        return;
    }

    auto *draggedSplit = dynamic_cast<Split *>(event->source());
    if (!draggedSplit)
    {
        qCDebug(chatterinoWidget)
            << "Dropped something that wasn't a split onto a notebook button";
        return;
    }

    if (auto *container = dynamic_cast<SplitContainer *>(this->page))
    {
        event->acceptProposedAction();
        container->insertSplit(draggedSplit);
    }
}

void NotebookTab::mouseMoveEvent(QMouseEvent *event)
{
    if (getSettings()->showTabCloseButton &&
        this->notebook_->getAllowUserTabManagement())
    {
        bool overX = this->getXRect().contains(event->pos());

        if (overX != this->mouseOverX_)
        {
            // Over X state has been changed (we either left or entered it;
            this->mouseOverX_ = overX;

            this->update();
        }
    }

    QPoint relPoint = this->mapToParent(event->pos());

    if (this->mouseDown_ && !this->getDesiredRect().contains(relPoint) &&
        this->notebook_->getAllowUserTabManagement())
    {
        int index;
        QWidget *clickedPage =
            this->notebook_->tabAt(relPoint, index, this->width());

        if (clickedPage != nullptr && clickedPage != this->page)
        {
            this->notebook_->rearrangePage(this->page, index);
        }
    }

    Button::mouseMoveEvent(event);
}

void NotebookTab::wheelEvent(QWheelEvent *event)
{
    const auto defaultMouseDelta = 120;
    int delta = event->angleDelta().y();
    if (delta == 0)
    {
        delta = event->angleDelta().x();
    }
    const auto selectTab = [this](int d) {
        d > 0 ? this->notebook_->selectPreviousTab()
              : this->notebook_->selectNextTab();
    };
    if (std::abs(delta) < defaultMouseDelta)
    {
        this->mouseWheelDelta_ += delta;
        if (std::abs(this->mouseWheelDelta_) >= defaultMouseDelta)
        {
            selectTab(this->mouseWheelDelta_);
            this->mouseWheelDelta_ = 0;
        }
    }
    else
    {
        selectTab(delta);
    }
}

void NotebookTab::update()
{
    Button::update();
}

QRect NotebookTab::getXRect() const
{
    QRect rect = this->rect();
    float s = this->scale();
    int size = static_cast<int>(16 * s);

    int centerAdjustment = size / 2;  // true center for all tab locations

    int insetH = std::max(3, static_cast<int>(3 * s));
    int rightMargin = insetH + static_cast<int>(2 * s);

    QRect xRect(rect.right() - rightMargin - size,
                rect.center().y() - centerAdjustment, size, size);

    return xRect;
}

}  // namespace chatterino
