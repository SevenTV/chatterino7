// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/Window.hpp"

#include "Application.hpp"
#include "common/Args.hpp"
#include "common/Common.hpp"
#include "common/Credentials.hpp"
#include "common/Modes.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "util/FormatTime.hpp"
#include "util/Helpers.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Theme.hpp"
#include "singletons/Updates.hpp"
#include "singletons/WindowManager.hpp"
#include "util/RapidJsonSerializeQSize.hpp"
#include "widgets/AccountSwitchPopup.hpp"
#include "widgets/buttons/InitUpdateButton.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/buttons/TitlebarButton.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/buttons/DrawnButton.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/dialogs/switcher/QuickSwitcherPopup.hpp"
#include "widgets/dialogs/UpdateDialog.hpp"
#include "widgets/dialogs/WelcomeDialog.hpp"
#include "widgets/helper/NotebookTab.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/ClosedSplits.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/splits/SplitHeader.hpp"
#include "widgets/splits/RoomModeHelpers.hpp"
#include "util/MultiChannel.hpp"

#ifdef Q_OS_MACOS
#    include "util/MacOsTitlebarButtons.hpp"
#endif

#ifndef NDEBUG
#    include "providers/twitch/PubSubManager.hpp"
#    include "providers/twitch/PubSubMessages.hpp"
#    include "util/SampleData.hpp"

#    include <rapidjson/document.h>
#endif

#include <QApplication>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMenuBar>
#include <QObject>
#include <QPainter>
#include <QPalette>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace chatterino {

Window::Window(WindowType type, QWidget *parent)
    : BaseWindow(
          {BaseWindow::EnableCustomFrame, BaseWindow::ClearBuffersOnDpiChange},
          parent)
    , type_(type)
    , notebook_(new SplitNotebook(this))
{
    this->addCustomTitlebarButtons();
    this->addShortcuts();
    this->addLayout();

#ifdef Q_OS_MACOS
    this->addMenuBar();
#endif

    this->signalHolder_.managedConnect(
        getApp()->getAccounts()->twitch.currentUserChanged, [this] {
            this->onAccountSelected();
        });
    this->onAccountSelected();

    if (type == WindowType::Main)
    {
        this->resize(int(600 * this->scale()), int(500 * this->scale()));
#ifdef Q_OS_LINUX
        if (this->theme->window.background.alpha() != 255)
        {
            this->setAttribute(Qt::WA_TranslucentBackground);
        }
#endif
    }
    else
    {
        auto lastPopup = getSettings()->lastPopupSize.getValue();
        if (lastPopup.isEmpty())
        {
            // The size in the setting was invalid, use the default value
            lastPopup = getSettings()->lastPopupSize.getDefaultValue();
        }
        this->resize(lastPopup.width(), lastPopup.height());
    }

    this->signalHolder_.managedConnect(getApp()->getHotkeys()->onItemsUpdated,
                                       [this]() {
                                           this->clearShortcuts();
                                           this->addShortcuts();
                                       });
    if (type == WindowType::Main || type == WindowType::Popup)
    {
        getSettings()->tabDirection.connect(
            [this](int val) {
                this->notebook_->setTabLocation(NotebookTabLocation(val));
            },
            this->signalHolder_);
    }
}

WindowType Window::getType()
{
    return this->type_;
}

SplitNotebook &Window::getNotebook()
{
    return *this->notebook_;
}

bool Window::event(QEvent *event)
{
    switch (event->type())
    {
        case QEvent::WindowActivate: {
            getApp()->getWindows()->selectedWindow_ = this;
            break;
        }

        case QEvent::WindowDeactivate: {
            auto *page = this->notebook_->getSelectedPage();

            if (page != nullptr)
            {
                std::vector<Split *> splits = page->getSplits();
                for (Split *split : splits)
                {
                    split->unpause();
                    split->updateLastReadMessage();
                }

                page->hideResizeHandles();
            }
        }
        break;

        default:;
    }

    return BaseWindow::event(event);
}

void Window::closeEvent(QCloseEvent *)
{
    if (isAppAboutToQuit())
    {
        qCWarning(chatterinoWidget)
            << "Window closeEvent ran when Application is already dead";
        return;
    }

    auto *app = getApp();

    if (this->type_ == WindowType::Main)
    {
        app->getWindows()->save();
        app->getWindows()->closeAll();
    }
    else
    {
        QRect rect = this->getBounds();
        QSize newSize(rect.width(), rect.height());
        getSettings()->lastPopupSize.setValue(newSize);
    }
    // Ensure selectedWindow_ is never an invalid pointer.
    // WindowManager will return the main window if no window is pointed to by
    // `selectedWindow_`.
    app->getWindows()->selectedWindow_ = nullptr;

    this->closed.invoke();

    if (this->type_ == WindowType::Main)
    {
        QApplication::exit();
    }
}

void Window::addLayout()
{
    auto *layout = new QVBoxLayout();

    layout->addWidget(this->notebook_);
    this->getLayoutContainer()->setLayout(layout);

    // set margin
    layout->setContentsMargins(0, 0, 0, 0);

    this->notebook_->setAllowUserTabManagement(true);
    this->notebook_->setShowAddButton(true);
}

void Window::addCustomTitlebarButtons()
{
    if (this->type_ != WindowType::Main)
    {
        return;
    }

#ifdef Q_OS_MACOS
    // setupMacOsTitlebarButtons() is called in showEvent()
    return;
#endif

    if (this->hasCustomWindowFrame())
    {
        this->addTitleBarButton<TitleBarButton>(
            [this] { getApp()->getWindows()->showSettingsDialog(this); },
            TitleBarButtonStyle::Settings);

        auto *update = this->addTitleBarButton<PixmapButton>([] {});
        initUpdateButton(*update, [] {}, this->signalHolder_);

        this->userLabel_ = this->addTitleBarLabel([this] {
            getApp()->getWindows()->showAccountSelectPopup(
                this->userLabel_->mapToGlobal(
                    this->userLabel_->rect().bottomLeft()));
        });
        this->userLabel_->setMinimumWidth(20 * this->scale());

        this->streamerModeTitlebarIcon_ =
            this->addTitleBarButton<PixmapButton>([this] {
                getApp()->getWindows()->showSettingsDialog(
                    this, SettingsDialogPreference::StreamerMode);
            });
        QObject::connect(getApp()->getStreamerMode(), &IStreamerMode::changed,
                         this, &Window::updateStreamerModeIcon);
        this->updateStreamerModeIcon();

        this->compactHeaderLabel_ = this->addTitleBarLabel([] {});
        this->compactHeaderLabel_->setVisible(
            getSettings()->compactHeaders.getValue());
        this->compactHeaderLabel_->setMinimumWidth(120 * this->scale());
        // MinimumExpanding lets the label fill remaining space without pushing
        // titlebar buttons out (matches SplitHeader::titleLabel_ behaviour).
        this->compactHeaderLabel_->setSizePolicy(
            QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

        this->compactModeButton_ = this->addTitleBarButton<LabelButton>([this] {
            auto *page = this->notebook_->getSelectedPage();
            auto *split = page ? page->getSelectedSplit() : nullptr;
            if (split)
                split->showHeaderModeMenu(QCursor::pos());
        });
        this->compactModeButton_->setVisible(
            getSettings()->compactHeaders.getValue());
        this->compactModeButton_->setPadding(QSize(2, 0));

        this->compactModButton_ = this->addTitleBarButton<SvgButton>(
            [this] {
                if (auto *page = this->notebook_->getSelectedPage())
                    if (auto *split = page->getSelectedSplit())
                        split->setModerationMode(!split->getModerationMode());
            },
            SvgButton::Src{
                .dark = ":/buttons/moderationDisabled-darkMode.svg",
                .light = ":/buttons/moderationDisabled-lightMode.svg",
            });
        this->compactModButton_->setVisible(
            getSettings()->compactHeaders.getValue());

        this->compactChattersButton_ = this->addTitleBarButton<SvgButton>(
            [this] {
                if (auto *page = this->notebook_->getSelectedPage())
                    if (auto *split = page->getSelectedSplit())
                        split->openChatterList();
            },
            SvgButton::Src{
                .dark = ":/buttons/chatters-darkMode.svg",
                .light = ":/buttons/chatters-lightMode.svg",
            });
        this->compactChattersButton_->setVisible(
            getSettings()->compactHeaders.getValue());
    }
    else
    {
        bool compact = getSettings()->compactHeaders.getValue();

        this->compactModButton_ =
            this->notebook_->addCustomButton<SvgButton>(SvgButton::Src{
                .dark = ":/buttons/moderationDisabled-darkMode.svg",
                .light = ":/buttons/moderationDisabled-lightMode.svg",
            });
        QObject::connect(
            this->compactModButton_, &Button::leftClicked, this, [this] {
                if (auto *page = this->notebook_->getSelectedPage())
                    if (auto *split = page->getSelectedSplit())
                        split->setModerationMode(!split->getModerationMode());
            });
        this->compactModButton_->setVisible(false);

        this->compactChattersButton_ =
            this->notebook_->addCustomButton<SvgButton>(SvgButton::Src{
                .dark = ":/buttons/chatters-darkMode.svg",
                .light = ":/buttons/chatters-lightMode.svg",
            });
        QObject::connect(
            this->compactChattersButton_, &Button::leftClicked, this, [this] {
                if (auto *page = this->notebook_->getSelectedPage())
                    if (auto *split = page->getSelectedSplit())
                        split->openChatterList();
            });
        this->compactChattersButton_->setVisible(false);

        this->compactDropdownButton_ =
            this->notebook_->addCustomButton<DrawnButton>(
                DrawnButton::Symbol::Kebab, DrawnButton::Options{});
        QObject::connect(
            this->compactDropdownButton_, &Button::leftMousePress, this,
            [this] {
                auto *page = this->notebook_->getSelectedPage();
                auto *split = page ? page->getSelectedSplit() : nullptr;
                if (!split)
                    return;
                const auto &h = getApp()->getHotkeys();
                auto menu = std::make_unique<QMenu>();
                menu->addAction(
                    "Change channel",
                    h->getDisplaySequence(HotkeyCategory::Split,
                                          "changeChannel"),
                    split, &Split::changeChannel);
                menu->addAction(
                    "Close",
                    h->getDisplaySequence(HotkeyCategory::Split, "delete"),
                    split, &Split::deleteFromContainer);
                menu->addSeparator();
                menu->addAction(
                    "Popup",
                    h->getDisplaySequence(HotkeyCategory::Window, "popup",
                                          {{"split"}}),
                    split, &Split::popup);
                menu->addAction(
                    "Search",
                    h->getDisplaySequence(HotkeyCategory::Split, "showSearch"),
                    split, [split] { split->showSearch(true); });
                menu->addAction(
                    "Set filters",
                    h->getDisplaySequence(HotkeyCategory::Split, "pickFilters"),
                    split, &Split::setFiltersDialog);
                menu->addSeparator();
                auto selected = split->getSelectedChannel();
                if (auto *tc =
                        dynamic_cast<TwitchChannel *>(selected.get()))
                {
                    menu->addAction(
                        "Open in browser",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "openInBrowser"),
                        split, &Split::openInBrowser);
                    menu->addAction(
                        "Open player in browser",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "openPlayerInBrowser"),
                        split, &Split::openBrowserPlayer);
                    menu->addAction(
                        "Open in streamlink",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "openInStreamlink"),
                        split, &Split::openInStreamlink);
                    if (split->getChannel()->hasModRights())
                    {
                        menu->addAction(
                            "Open mod view",
                            h->getDisplaySequence(HotkeyCategory::Split,
                                                  "openModView"),
                            split, &Split::openModViewInBrowser);
                    }
                    if (tc->isLive())
                    {
                        menu->addAction(
                            "Create a clip",
                            h->getDisplaySequence(HotkeyCategory::Split,
                                                  "createClip"),
                            split, [tc] { tc->createClip({}, {}); });
                    }
                    menu->addSeparator();
                    menu->addAction(
                        "Reload channel emotes",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "reloadEmotes", {{"channel"}}),
                        split, [tc] {
                            tc->refreshFFZChannelEmotes(true);
                            tc->refreshBTTVChannelEmotes(true);
                            tc->refreshSevenTVChannelEmotes(true);
                        });
                    menu->addAction(
                        "Reload subscriber emotes",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "reloadEmotes", {{"subscriber"}}),
                        split, [tc] {
                            tc->refreshTwitchChannelEmotes(true);
                        });
                }
                if (split->getChannel()->canReconnect())
                {
                    menu->addAction(
                        "Reconnect",
                        h->getDisplaySequence(HotkeyCategory::Split,
                                              "reconnect"),
                        split, &Split::reconnect);
                }
                menu->addSeparator();
                menu->addAction(
                    "Clear messages",
                    h->getDisplaySequence(HotkeyCategory::Split,
                                          "clearMessages"),
                    split, &Split::clear);
                this->compactDropdownButton_->setMenu(std::move(menu));
            });
        this->compactDropdownButton_->setVisible(compact);
    }

    getSettings()->compactHeaders.connect(
        [this](bool compact) {
            if (!compact)
            {
                if (this->compactDropdownButton_)
                    this->compactDropdownButton_->setVisible(false);
                if (this->compactModButton_)
                    this->compactModButton_->setVisible(false);
                if (this->compactChattersButton_)
                    this->compactChattersButton_->setVisible(false);
                if (this->compactModeButton_)
                    this->compactModeButton_->setVisible(false);
                if (this->compactHeaderLabel_)
                    this->compactHeaderLabel_->setVisible(false);
#ifdef Q_OS_MACOS
                // Restore the base window title (removes channel info suffix).
                this->onAccountSelected();
#endif
            }
            else
            {
                if (this->compactDropdownButton_)
                    this->compactDropdownButton_->setVisible(true);
                if (this->compactHeaderLabel_)
                    this->compactHeaderLabel_->setVisible(true);
                this->updateCompactHeader();
                this->updateCompactHeaderButtons();
                this->updateCompactHeaderMode();
            }
            this->notebook_->performLayout();
        },
        this->signalHolder_, false);

    auto refreshHeader = [this](const auto &, const auto &) {
        this->updateCompactHeader();
    };
    getSettings()->headerViewerCount.connect(refreshHeader,
                                             this->signalHolder_);
    getSettings()->headerStreamTitle.connect(refreshHeader,
                                             this->signalHolder_);
    getSettings()->headerGame.connect(refreshHeader, this->signalHolder_);
    getSettings()->headerUptime.connect(refreshHeader, this->signalHolder_);
    getSettings()->appendOriginalAppTitle.connect(
        refreshHeader, this->signalHolder_);

    this->signalHolder_.managedConnect(
        getApp()->getAccounts()->twitch.currentUserChanged, [this] {
            this->updateCompactHeaderButtons();
        });

    this->signalHolder_.managedConnect(
        this->notebook_->pageSelected, [this] {
            this->setupCompactHeaderConnections();
            this->updateCompactHeader();
            this->updateCompactHeaderButtons();
        });

    this->setupCompactHeaderConnections();
    this->updateCompactHeader();
    this->updateCompactHeaderButtons();
    this->updateCompactHeaderMode();
}

void Window::showEvent(QShowEvent *event)
{
    BaseWindow::showEvent(event);

#ifdef Q_OS_MACOS
    if (!this->macTitlebarSetup_ && this->type_ == WindowType::Main)
    {
        this->macTitlebarSetup_ = true;
        setupMacOsTitlebarButtons(this, this->notebook_);

        this->signalHolder_.managedConnect(
            this->notebook_->pageSelected, [this] {
                this->setupCompactHeaderConnections();
                this->updateCompactHeader();
                this->updateCompactHeaderButtons();
            });
        this->setupCompactHeaderConnections();
        this->updateCompactHeaderButtons();

        getSettings()->compactHeaders.connect(
            [this](bool compact) {
                setMacOsTitlebarButtonsVisible(compact);
                if (compact)
                {
                    this->updateCompactHeaderButtons();
                }
            },
            this->signalHolder_, false);
    }
#endif
}

void Window::updateStreamerModeIcon()
{
    // A duplicate of this code is in SplitNotebook class (in Notebook.{c,h}pp)
    // That one is the one near splits (on linux and mac or non-main windows on Windows)
    // This copy handles the TitleBar icon in Window (main window on Windows)
    if (this->streamerModeTitlebarIcon_ == nullptr)
    {
        return;
    }
#ifdef Q_OS_WIN
    assert(this->getType() == WindowType::Main);
    if (getTheme()->isLightTheme())
    {
        this->streamerModeTitlebarIcon_->setPixmap(
            getResources().buttons.streamerModeEnabledLight);
    }
    else
    {
        this->streamerModeTitlebarIcon_->setPixmap(
            getResources().buttons.streamerModeEnabledDark);
    }
    this->streamerModeTitlebarIcon_->setVisible(
        getApp()->getStreamerMode()->isEnabled());
#else
    // clang-format off
    assert(false && "Streamer mode TitleBar icon should not exist on non-Windows OSes");
    // clang-format on
#endif
}

void Window::themeChangedEvent()
{
    this->updateStreamerModeIcon();
    BaseWindow::themeChangedEvent();
#ifdef Q_OS_MACOS
    this->updateCompactHeaderButtons();
#endif
}

void Window::setupCompactHeaderConnections()
{
    this->compactHeaderConnections_.clear();

    auto *page = this->notebook_->getSelectedPage();
    if (!page)
    {
        return;
    }

    for (auto *s : page->getSplits())
    {
        this->compactHeaderConnections_.managedConnect(
            s->focused, [this] {
                this->updateCompactHeader();
                this->updateCompactHeaderButtons();
            });
    }

    auto *split = page->getSelectedSplit();
    if (!split)
    {
        return;
    }

    this->compactHeaderConnections_.managedConnect(
        split->channelChanged, [this] {
            this->updateCompactHeader();
            this->updateCompactHeaderButtons();
        });

    auto channel = split->getChannel();
    if (auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        this->compactHeaderConnections_.managedConnect(
            twitchChannel->streamStatusChanged, [this] {
                this->updateCompactHeader();
            });
        this->compactHeaderConnections_.managedConnect(
            twitchChannel->roomModesChanged, [this] {
                this->updateCompactHeaderButtons();
            });
        this->compactHeaderConnections_.managedConnect(
            twitchChannel->userStateChanged, [this] {
                this->updateCompactHeaderButtons();
            });
    }
    else if (auto *kickChannel = dynamic_cast<KickChannel *>(channel.get()))
    {
        this->compactHeaderConnections_.managedConnect(
            kickChannel->streamDataChanged, [this] {
                this->updateCompactHeader();
            });
        this->compactHeaderConnections_.managedConnect(
            kickChannel->roomModesChanged, [this] {
                this->updateCompactHeaderButtons();
            });
    }
    else if (auto *multiChannel =
                 dynamic_cast<MultiChannel *>(channel.get()))
    {
        this->compactHeaderConnections_.managedConnect(
            multiChannel->activeChannelChanged, [this] {
                this->updateCompactHeader();
                this->updateCompactHeaderButtons();
            });
    }
}

void Window::updateCompactHeader()
{
    if (!getSettings()->compactHeaders.getValue())
    {
        return;
    }

    auto *page = this->notebook_->getSelectedPage();
    QString text;
    if (!page)
    {
        text = "No tab selected";
    }
    else if (auto *split = page->getSelectedSplit())
    {
        auto channel = split->getChannel();
        auto selectedChannel = split->getSelectedChannel();

        text = channel->getLocalizedName();
        if (channel->getType() == Channel::Type::TwitchWatching)
        {
            text = "watching: " + (text.isEmpty() ? "none" : text);
        }

        if (auto *twitchChannel =
                dynamic_cast<TwitchChannel *>(selectedChannel.get()))
        {
            const auto streamStatus = twitchChannel->accessStreamStatus();
            if (streamStatus->live)
            {
                if (streamStatus->rerun)
                {
                    text += " (rerun)";
                }
                else
                {
                    text += " (live)";
                }
                if (getSettings()->headerViewerCount)
                {
                    text += " - " + localizeNumbers(streamStatus->viewerCount);
                }
                if (getSettings()->headerUptime)
                {
                    text += " - " + streamStatus->uptime;
                }
                if (getSettings()->headerGame &&
                    !streamStatus->game.isEmpty())
                {
                    text += " - " + streamStatus->game;
                }
                if (getSettings()->headerStreamTitle &&
                    !streamStatus->title.isEmpty())
                {
                    text += " - " + streamStatus->title.simplified();
                }
            }
        }
        else if (auto *kickChannel =
                     dynamic_cast<KickChannel *>(selectedChannel.get()))
        {
            const auto &stream = kickChannel->streamData();
            if (stream.isLive)
            {
                text += " (live)";
                if (getSettings()->headerViewerCount)
                {
                    text += " - " + localizeNumbers(stream.viewerCount);
                }
                if (getSettings()->headerUptime)
                {
                    text += " - " + stream.uptime;
                }
                if (getSettings()->headerGame && !stream.category.isEmpty())
                {
                    text += " - " + stream.category;
                }
                if (getSettings()->headerStreamTitle &&
                    !stream.title.isEmpty())
                {
                    text += " - " + stream.title.simplified();
                }
            }
        }
    }
    else
    {
        text = page->getTab()->getTitle();
    }

    if (this->compactHeaderLabel_)
    {
        this->compactHeaderLabel_->setText(text.isEmpty() ? "<empty>" : text);
    }

#ifdef Q_OS_MACOS
    setMacOsTitlebarLabelText(text.isEmpty() ? "<empty>" : text);
#endif
}

void Window::updateCompactHeaderButtons()
{
    bool compact = getSettings()->compactHeaders.getValue();

    auto *page = this->notebook_->getSelectedPage();
    auto *split = page ? page->getSelectedSplit() : nullptr;
    auto channel = split ? split->getSelectedChannel() : nullptr;

    if (!channel || !channel->isTwitchOrKickChannel())
    {
        if (this->compactModButton_)
            this->compactModButton_->setVisible(false);
        if (this->compactChattersButton_)
            this->compactChattersButton_->setVisible(false);
        if (this->compactModeButton_)
            this->compactModeButton_->setVisible(false);
#ifdef Q_OS_MACOS
        updateMacOsTitlebarButtonsForSplit(nullptr);
#endif
        return;
    }

    bool hasMod = channel->hasModRights();
    bool moderationMode =
        split->getModerationMode() &&
        !getSettings()->moderationActions.empty();

    if (this->compactModButton_)
    {
        this->compactModButton_->setSource(
            moderationMode
                ? SvgButton::Src{
                      .dark = ":/buttons/moderationEnabled-darkMode.svg",
                      .light = ":/buttons/moderationEnabled-lightMode.svg",
                  }
                : SvgButton::Src{
                      .dark = ":/buttons/moderationDisabled-darkMode.svg",
                      .light = ":/buttons/moderationDisabled-lightMode.svg",
                  });
        this->compactModButton_->setVisible(compact && (hasMod || moderationMode));
    }
    if (this->compactChattersButton_)
    {
        this->compactChattersButton_->setVisible(
            compact && (hasMod && channel->isTwitchChannel()));
    }

    this->updateCompactHeaderMode();

#ifdef Q_OS_MACOS
    updateMacOsTitlebarButtonsForSplit(split);
#endif
}

void Window::updateCompactHeaderMode()
{
    if (!this->compactModeButton_)
    {
        return;
    }

    bool compact = getSettings()->compactHeaders.getValue();
    auto *page = this->notebook_->getSelectedPage();
    auto *split = page ? page->getSelectedSplit() : nullptr;
    if (!split)
    {
        this->compactModeButton_->setVisible(false);
        return;
    }

    auto channel = split->getSelectedChannel();
    QString text;
    bool visible = false;

    if (auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        auto roomModes = twitchChannel->accessRoomModes();
        text = formatRoomModeUnclean(*roomModes);
        cleanRoomModeText(text, twitchChannel->hasModRights());
        visible = !text.isEmpty();
    }
    else if (auto *kickChannel = dynamic_cast<KickChannel *>(channel.get()))
    {
        text = formatRoomModeUnclean(kickChannel->roomModes());
        cleanRoomModeText(text, false);
        visible = !text.isEmpty();
    }

    if (visible && compact)
    {
        this->compactModeButton_->setText(text);
        this->compactModeButton_->setVisible(true);
    }
    else
    {
        this->compactModeButton_->setVisible(false);
    }
}

void Window::addDebugStuff(HotkeyController::HotkeyMap &actions)
{
#ifndef NDEBUG
    actions.emplace("addMiscMessage", [=](std::vector<QString>) -> QString {
        const auto &messages = getSampleMiscMessages();
        static int index = 0;
        const auto &msg = messages[index++ % messages.size()];
        getApp()->getTwitch()->addFakeMessage(msg);
        return "";
    });

    actions.emplace("addCheerMessage", [=](std::vector<QString>) -> QString {
        const auto &messages = getSampleCheerMessages();
        static int index = 0;
        const auto &msg = messages[index++ % messages.size()];
        getApp()->getTwitch()->addFakeMessage(msg);
        return "";
    });

    actions.emplace("addLinkMessage", [=](std::vector<QString>) -> QString {
        const auto &messages = getSampleLinkMessages();
        static int index = 0;
        const auto &msg = messages[index++ % messages.size()];
        getApp()->getTwitch()->addFakeMessage(msg);
        return "";
    });

    actions.emplace("addRewardMessage", [=](std::vector<QString>) -> QString {
        rapidjson::Document doc;
        static bool alt = true;
        if (alt)
        {
            auto oMessage =
                parsePubSubBaseMessage(getSampleChannelRewardMessage());
            auto oInnerMessage =
                oMessage->toInner<PubSubMessageMessage>()
                    ->toInner<PubSubCommunityPointsChannelV1Message>();

            getApp()->getTwitch()->addFakeMessage(
                getSampleChannelRewardIRCMessage());
            getApp()->getTwitchPubSub()->pointReward.redeemed.invoke(
                oInnerMessage->data.value("redemption").toObject());
            alt = !alt;
        }
        else
        {
            auto oMessage =
                parsePubSubBaseMessage(getSampleChannelRewardMessage2());
            auto oInnerMessage =
                oMessage->toInner<PubSubMessageMessage>()
                    ->toInner<PubSubCommunityPointsChannelV1Message>();
            getApp()->getTwitchPubSub()->pointReward.redeemed.invoke(
                oInnerMessage->data.value("redemption").toObject());
            alt = !alt;
        }
        return "";
    });

    actions.emplace("addEmoteMessage", [=](std::vector<QString>) -> QString {
        const auto &messages = getSampleEmoteTestMessages();
        static int index = 0;
        const auto &msg = messages[index++ % messages.size()];
        getApp()->getTwitch()->addFakeMessage(msg);
        return "";
    });

    actions.emplace("addSubMessage", [=](std::vector<QString>) -> QString {
        const auto &messages = getSampleSubMessages();
        static int index = 0;
        const auto &msg = messages[index++ % messages.size()];
        getApp()->getTwitch()->addFakeMessage(msg);
        return "";
    });
#endif
}

void Window::addShortcuts()
{
    HotkeyController::HotkeyMap actions{
        {"openSettings",  // Open settings
         [this](std::vector<QString>) -> QString {
             SettingsDialog::showDialog(this);
             return "";
         }},
        {"openAccountSelector",  // Open account selector
         [](const std::vector<QString> &) -> QString {
             getApp()->getWindows()->showAccountSelectPopup({0, 0});
             return "";
         }},
        {"newSplit",  // Create a new split
         [this](std::vector<QString>) -> QString {
             this->notebook_->getOrAddSelectedPage()->appendNewSplit(true);
             return "";
         }},
        {"openTab",  // CTRL + 1-8 to open corresponding tab.
         [this](std::vector<QString> arguments) -> QString {
             if (arguments.size() == 0)
             {
                 qCWarning(chatterinoHotkeys)
                     << "openTab shortcut called without arguments. "
                        "Takes only "
                        "one argument: tab specifier";
                 return "openTab shortcut called without arguments. "
                        "Takes only "
                        "one argument: tab specifier";
             }
             auto target = arguments.at(0);
             if (target == "last")
             {
                 this->notebook_->selectLastTab();
             }
             else if (target == "next")
             {
                 this->notebook_->selectNextTab();
             }
             else if (target == "previous")
             {
                 this->notebook_->selectPreviousTab();
             }
             else
             {
                 bool ok;
                 int result = target.toInt(&ok);
                 if (ok)
                 {
                     this->notebook_->selectVisibleIndex(result);
                 }
                 else
                 {
                     qCWarning(chatterinoHotkeys)
                         << "Invalid argument for openTab shortcut";
                     return QString("Invalid argument for openTab "
                                    "shortcut: \"%1\". Use \"last\", "
                                    "\"next\", \"previous\" or an integer.")
                         .arg(target);
                 }
             }
             return "";
         }},
        {"popup",
         [this](std::vector<QString> arguments) -> QString {
             if (arguments.size() == 0)
             {
                 return "popup action called without arguments. Takes only "
                        "one: \"split\" or \"window\".";
             }
             if (arguments.at(0) == "split")
             {
                 if (auto *page = dynamic_cast<SplitContainer *>(
                         this->notebook_->getSelectedPage()))
                 {
                     if (auto *split = page->getSelectedSplit())
                     {
                         split->popup();
                     }
                 }
             }
             else if (arguments.at(0) == "window")
             {
                 if (auto *page = dynamic_cast<SplitContainer *>(
                         this->notebook_->getSelectedPage()))
                 {
                     page->popup();
                 }
             }
             else
             {
                 return R"(Invalid popup target. Use "split" or "window".)";
             }
             return "";
         }},
        {"zoom",
         [](std::vector<QString> arguments) -> QString {
             if (arguments.size() == 0)
             {
                 qCWarning(chatterinoHotkeys)
                     << "zoom shortcut called without arguments. Takes "
                        "only "
                        "one argument: \"in\", \"out\", or \"reset\"";
                 return "zoom shortcut called without arguments. Takes "
                        "only "
                        "one argument: \"in\", \"out\", or \"reset\"";
             }
             auto change = 0.0f;
             auto direction = arguments.at(0);
             if (direction == "reset")
             {
                 getSettings()->uiScale.setValue(1);
                 return "";
             }

             if (direction == "in")
             {
                 change = 0.1f;
             }
             else if (direction == "out")
             {
                 change = -0.1f;
             }
             else
             {
                 qCWarning(chatterinoHotkeys)
                     << "Invalid zoom direction, use \"in\", \"out\", or "
                        "\"reset\"";
                 return "Invalid zoom direction, use \"in\", \"out\", or "
                        "\"reset\"";
             }
             getSettings()->setClampedUiScale(
                 getSettings()->getClampedUiScale() + change);
             return "";
         }},
        {"newTab",
         [this](std::vector<QString>) -> QString {
             this->notebook_->addPage(true);
             return "";
         }},
        {"removeTab",
         [this](std::vector<QString>) -> QString {
             this->notebook_->removeCurrentPage();
             return "";
         }},
        {"reopenSplit",
         [this](std::vector<QString>) -> QString {
             if (ClosedSplits::empty())
             {
                 return "";
             }
             ClosedSplits::SplitInfo si = ClosedSplits::pop();
             SplitContainer *splitContainer{nullptr};
             if (si.tab)
             {
                 splitContainer = dynamic_cast<SplitContainer *>(si.tab->page);
             }
             if (!splitContainer)
             {
                 splitContainer = this->notebook_->getOrAddSelectedPage();
             }
             Split *split = new Split(splitContainer);
             split->setChannel(
                 getApp()->getTwitch()->getOrAddChannel(si.channelName));
             split->setFilters(si.filters);
             splitContainer->insertSplit(split);
             splitContainer->setSelected(split);
             this->notebook_->select(splitContainer);
             return "";
         }},
        {"toggleLocalR9K",
         [](std::vector<QString>) -> QString {
             getSettings()->hideSimilar.setValue(!getSettings()->hideSimilar);
             getApp()->getWindows()->forceLayoutChannelViews();
             return "";
         }},
        {"openQuickSwitcher",
         [this](std::vector<QString>) -> QString {
             auto *quickSwitcher = new QuickSwitcherPopup(this);
             quickSwitcher->show();
             return "";
         }},
        {"quit",
         [](std::vector<QString>) -> QString {
             QApplication::exit();
             return "";
         }},
        {"moveTab",
         [this](std::vector<QString> arguments) -> QString {
             if (arguments.size() == 0)
             {
                 qCWarning(chatterinoHotkeys)
                     << "moveTab shortcut called without arguments. "
                        "Takes only one argument: new index (number, "
                        "\"next\" "
                        "or \"previous\")";
                 return "moveTab shortcut called without arguments. "
                        "Takes only one argument: new index (number, "
                        "\"next\" "
                        "or \"previous\")";
             }
             int newIndex = -1;
             bool indexIsGenerated =
                 false;  // indicates if `newIndex` was generated using target="next" or target="previous"

             auto target = arguments.at(0);
             qCDebug(chatterinoHotkeys) << target;
             if (target == "next")
             {
                 newIndex = this->notebook_->getSelectedIndex() + 1;
                 indexIsGenerated = true;
             }
             else if (target == "previous")
             {
                 newIndex = this->notebook_->getSelectedIndex() - 1;
                 indexIsGenerated = true;
             }
             else
             {
                 bool ok;
                 int result = target.toInt(&ok);
                 if (!ok)
                 {
                     qCWarning(chatterinoHotkeys)
                         << "Invalid argument for moveTab shortcut";
                     return QString("Invalid argument for moveTab shortcut: "
                                    "%1. Use \"next\" or \"previous\" or an "
                                    "integer.")
                         .arg(target);
                 }
                 newIndex = result;
             }
             if (newIndex >= this->notebook_->getPageCount() || 0 > newIndex)
             {
                 if (indexIsGenerated)
                 {
                     return "";  // don't error out on generated indexes, ie move tab right
                 }
                 qCWarning(chatterinoHotkeys)
                     << "Invalid index for moveTab shortcut:" << newIndex;
                 return QString("Invalid index for moveTab shortcut: %1.")
                     .arg(newIndex);
             }
             this->notebook_->rearrangePage(this->notebook_->getSelectedPage(),
                                            newIndex);
             return "";
         }},
        {"setStreamerMode",
         [](std::vector<QString> arguments) -> QString {
             auto mode = 2;
             if (arguments.size() != 0)
             {
                 auto arg = arguments.at(0);
                 if (arg == "off")
                 {
                     mode = 0;
                 }
                 else if (arg == "on")
                 {
                     mode = 1;
                 }
                 else if (arg == "toggle")
                 {
                     mode = 2;
                 }
                 else if (arg == "auto")
                 {
                     mode = 3;
                 }
                 else
                 {
                     qCWarning(chatterinoHotkeys)
                         << "Invalid argument for setStreamerMode hotkey: "
                         << arg;
                     return QString("Invalid argument for setStreamerMode "
                                    "hotkey: %1. Use \"on\", \"off\", "
                                    "\"toggle\" or \"auto\".")
                         .arg(arg);
                 }
             }

             if (mode == 0)
             {
                 getSettings()->enableStreamerMode.setValue(
                     StreamerModeSetting::Disabled);
             }
             else if (mode == 1)
             {
                 getSettings()->enableStreamerMode.setValue(
                     StreamerModeSetting::Enabled);
             }
             else if (mode == 2)
             {
                 if (getApp()->getStreamerMode()->isEnabled())
                 {
                     getSettings()->enableStreamerMode.setValue(
                         StreamerModeSetting::Disabled);
                 }
                 else
                 {
                     getSettings()->enableStreamerMode.setValue(
                         StreamerModeSetting::Enabled);
                 }
             }
             else if (mode == 3)
             {
                 getSettings()->enableStreamerMode.setValue(
                     StreamerModeSetting::DetectStreamingSoftware);
             }
             return "";
         }},
        {"setTabVisibility",
         [this](std::vector<QString> arguments) -> QString {
             QString arg = arguments.empty() ? "toggle" : arguments.front();

             if (arg == "off")
             {
                 this->notebook_->hideAllTabsAction->trigger();
             }
             else if (arg == "on")
             {
                 this->notebook_->showAllTabsAction->trigger();
             }
             else if (arg == "toggle")
             {
                 this->notebook_->toggleTabVisibility();
             }
             else if (arg == "liveOnly")
             {
                 this->notebook_->onlyShowLiveTabsAction->trigger();
             }
             else if (arg == "toggleLiveOnly")
             {
                 // NOOP: Removed 2024-08-04 https://github.com/Chatterino/chatterino2/pull/5530
                 return "toggleLiveOnly is no longer a valid argument for "
                        "setTabVisibility";
             }
             else
             {
                 qCWarning(chatterinoHotkeys)
                     << "Invalid argument for setTabVisibility hotkey: " << arg;
                 return QString("Invalid argument for setTabVisibility hotkey: "
                                "%1. Use \"on\", \"off\", \"toggle\", or "
                                "\"liveOnly\".")
                     .arg(arg);
             }

             return "";
         }},
    };

    this->addDebugStuff(actions);

    this->shortcuts_ = getApp()->getHotkeys()->shortcutsForCategory(
        HotkeyCategory::Window, actions, this);
}

void Window::addMenuBar()
{
    auto *menuBar = new QMenuBar();
    menuBar->setNativeMenuBar(true);

    QMenu *appMenu = menuBar->addMenu(QString());
    {
        auto *about = appMenu->addAction(QString());
        about->setMenuRole(QAction::AboutRole);
        connect(about, &QAction::triggered, this, [this] {
            SettingsDialog::showDialog(
                this, SettingsDialogPreference::About);
        });

        appMenu->addSeparator();

        auto *prefs = appMenu->addAction(QString());
        prefs->setMenuRole(QAction::PreferencesRole);
        prefs->setShortcut(QKeySequence::Preferences);
        connect(prefs, &QAction::triggered, this, [this] {
            SettingsDialog::showDialog(this);
        });
    }

    QMenu *fileMenu = menuBar->addMenu("File");
    {
        auto *newTab = fileMenu->addAction("New Tab");
        newTab->setShortcut(QKeySequence::New);
        connect(newTab, &QAction::triggered, this, [this] {
            this->notebook_->addPage(true);
        });

        auto *newSplit = fileMenu->addAction("New Split");
        newSplit->setShortcut(QKeySequence("Ctrl+Shift+N"));
        connect(newSplit, &QAction::triggered, this, [this] {
            if (auto *page = this->notebook_->getSelectedPage())
            {
                page->appendNewSplit(true);
            }
        });

        fileMenu->addSeparator();

        auto *closeTab = fileMenu->addAction("Close Tab");
        closeTab->setShortcut(QKeySequence::Close);
        connect(closeTab, &QAction::triggered, this, [this] {
            this->notebook_->removeCurrentPage();
        });

        fileMenu->addSeparator();

        auto *changeChannel = fileMenu->addAction("Change Channel...");
        changeChannel->setShortcut(QKeySequence("Ctrl+K"));
        connect(changeChannel, &QAction::triggered, this, [this] {
            if (auto *page = this->notebook_->getSelectedPage())
            {
                if (auto *split = page->getSelectedSplit())
                {
                    split->changeChannel();
                }
            }
        });

        auto *popupSplit = fileMenu->addAction("Popout Split");
        connect(popupSplit, &QAction::triggered, this, [this] {
            if (auto *page = this->notebook_->getSelectedPage())
            {
                if (auto *split = page->getSelectedSplit())
                {
                    split->popup();
                }
            }
        });
    }

    QMenu *editMenu = menuBar->addMenu("Edit");
    {
        auto *undo = editMenu->addAction("Undo");
        undo->setShortcut(QKeySequence::Undo);
        connect(undo, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "undo");
        });

        auto *redo = editMenu->addAction("Redo");
        redo->setShortcut(QKeySequence::Redo);
        connect(redo, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "redo");
        });

        editMenu->addSeparator();

        auto *cut = editMenu->addAction("Cut");
        cut->setShortcut(QKeySequence::Cut);
        connect(cut, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "cut");
        });

        auto *copy = editMenu->addAction("Copy");
        copy->setShortcut(QKeySequence::Copy);
        connect(copy, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "copy");
        });

        auto *paste = editMenu->addAction("Paste");
        paste->setShortcut(QKeySequence::Paste);
        connect(paste, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "paste");
        });

        editMenu->addSeparator();

        auto *selectAll = editMenu->addAction("Select All");
        selectAll->setShortcut(QKeySequence::SelectAll);
        connect(selectAll, &QAction::triggered, this, [] {
            if (auto *w = qApp->focusWidget())
                QMetaObject::invokeMethod(w, "selectAll");
        });

        editMenu->addSeparator();

        auto *find = editMenu->addAction("Find...");
        find->setShortcut(QKeySequence::Find);
        editMenu->addSeparator();

        auto *clearChat = editMenu->addAction("Clear Messages");
        connect(clearChat, &QAction::triggered, this, [this] {
            if (auto *page = this->notebook_->getSelectedPage())
            {
                if (auto *split = page->getSelectedSplit())
                {
                    split->getChannelView().clearMessages();
                }
            }
        });
    }

    QMenu *viewMenu = menuBar->addMenu("View");
    {
        auto *toggleTopMost = viewMenu->addAction("Always on Top");
        toggleTopMost->setCheckable(true);
        toggleTopMost->setChecked(
            getSettings()->windowTopMost.getValue());
        connect(toggleTopMost, &QAction::triggered, [](bool checked) {
            getSettings()->windowTopMost.setValue(checked);
        });

        viewMenu->addSeparator();

        auto *zoomIn = viewMenu->addAction("Zoom In");
        zoomIn->setShortcut(QKeySequence::ZoomIn);
        connect(zoomIn, &QAction::triggered, [] {
            auto s = getSettings()->getClampedUiScale() + 0.1f;
            getSettings()->setClampedUiScale(std::min(s, 5.0f));
        });

        auto *zoomOut = viewMenu->addAction("Zoom Out");
        zoomOut->setShortcut(QKeySequence::ZoomOut);
        connect(zoomOut, &QAction::triggered, [] {
            auto s = getSettings()->getClampedUiScale() - 0.1f;
            getSettings()->setClampedUiScale(std::max(s, 0.2f));
        });

        auto *resetZoom = viewMenu->addAction("Reset Zoom");
        resetZoom->setShortcut(QKeySequence("Ctrl+0"));
        connect(resetZoom, &QAction::triggered, [] {
            getSettings()->setClampedUiScale(1.0f);
        });

        viewMenu->addSeparator();

        auto *fullScreen = viewMenu->addAction("Enter Full Screen");
        fullScreen->setShortcut(QKeySequence("Ctrl+Meta+F"));
        fullScreen->setCheckable(true);
        connect(fullScreen, &QAction::triggered, this, [this] {
            this->setWindowState(
                this->windowState() ^ Qt::WindowFullScreen);
        });
    }

    auto *profileMenu = menuBar->addMenu("Profile");
    {
        connect(profileMenu, &QMenu::aboutToShow, this,
                [this, profileMenu] {
                    profileMenu->clear();

                    auto &accounts =
                        getApp()->getAccounts()->twitch.accounts;
                    auto current =
                        getApp()->getAccounts()->twitch.getCurrent();

                    for (auto &account : accounts)
                    {
                        if (account->isAnon())
                        {
                            continue;
                        }
                        QString name = account->getUserName();
                        auto *action =
                            profileMenu->addAction(name);
                        action->setCheckable(true);
                        action->setChecked(
                            account.get() == current.get());
                        connect(action, &QAction::triggered,
                                [name] {
                                    getApp()
                                        ->getAccounts()
                                        ->twitch.currentUsername =
                                        name;
                                });
                    }

                    if (accounts.empty())
                    {
                        profileMenu->addAction(
                            "No accounts logged in")
                            ->setEnabled(false);
                    }

                    profileMenu->addSeparator();

                    auto *addAccount =
                        profileMenu->addAction("Add Account...");
                    connect(addAccount, &QAction::triggered, this,
                            [this] {
                                SettingsDialog::showDialog(
                                    this,
                                    SettingsDialogPreference::
                                        Accounts);
                            });

                    auto *manage =
                        profileMenu->addAction("Manage Accounts...");
                    connect(manage, &QAction::triggered, this,
                            [this] {
                                SettingsDialog::showDialog(
                                    this,
                                    SettingsDialogPreference::
                                        Accounts);
                            });
                });
    }

    QMenu *windowMenu = menuBar->addMenu("Window");
    {
        auto *minimize = windowMenu->addAction("Minimize");
        minimize->setShortcut(QKeySequence("Ctrl+M"));
        connect(minimize, &QAction::triggered, this, [this] {
            this->setWindowState(Qt::WindowMinimized);
        });

        auto *zoom = windowMenu->addAction("Zoom");
        connect(zoom, &QAction::triggered, this, [this] {
            if (this->isMaximized())
                this->showNormal();
            else
                this->showMaximized();
        });

        windowMenu->addSeparator();

        auto *bringAll = windowMenu->addAction("Bring All to Front");
        connect(bringAll, &QAction::triggered, this, [] {
            for (auto *w : qApp->topLevelWidgets())
            {
                if (w->isWindow())
                {
                    w->raise();
                    w->activateWindow();
                }
            }
        });

        windowMenu->addSeparator();

        auto *nextSplit = windowMenu->addAction("Focus Next Split");
        nextSplit->setShortcut(QKeySequence("Alt+Right"));
        auto *prevSplit = windowMenu->addAction(
            "Focus Previous Split");
        prevSplit->setShortcut(QKeySequence("Alt+Left"));
    }

    QMenu *helpMenu = menuBar->addMenu("Help");
    {
        auto *helpWiki = helpMenu->addAction("YaseenChat Wiki");
        connect(helpWiki, &QAction::triggered, this, [] {
            QDesktopServices::openUrl(
                QUrl(LINK_CHATTERINO_WIKI.toString()));
        });

        auto *helpGithub = helpMenu->addAction("YaseenChat GitHub");
        connect(helpGithub, &QAction::triggered, this, [] {
            QDesktopServices::openUrl(
                QUrl(LINK_CHATTERINO_SOURCE.toString()));
        });

        auto *helpDiscord = helpMenu->addAction("Discord");
        connect(helpDiscord, &QAction::triggered, this, [] {
            QDesktopServices::openUrl(
                QUrl(LINK_CHATTERINO_DISCORD.toString()));
        });
    }
}

void Window::onAccountSelected()
{
    auto user = getApp()->getAccounts()->twitch.getCurrent();

    // update title (also append username on Linux and MacOS)
    QString windowTitle = Version::instance().fullVersion();

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    if (user->isAnon())
    {
        windowTitle += " - not logged in";
    }
    else
    {
        windowTitle += " - " + user->getUserName();
    }
#endif

    if (getApp()->getArgs().safeMode)
    {
        windowTitle += " (safe mode)";
    }

    this->setWindowTitle(windowTitle);

    // update user
    if (this->userLabel_)
    {
        if (user->isAnon())
        {
            this->userLabel_->setText("anonymous");
        }
        else
        {
            this->userLabel_->setText(user->getUserName());
        }
    }
}

}  // namespace chatterino
