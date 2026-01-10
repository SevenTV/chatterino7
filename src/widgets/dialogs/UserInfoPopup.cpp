// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/UserInfoPopup.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/highlights/HighlightBlacklistUser.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/userdata/UserDataController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/IvrApi.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/pronouns/Pronouns.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Clipboard.hpp"
#include "util/FormatTime.hpp"
#include "util/Helpers.hpp"
#include "util/LayoutCreator.hpp"
#include "util/PostToThread.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/dialogs/EditUserNotesDialog.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/helper/InvisibleSizeGrip.hpp"
#include "widgets/helper/Line.hpp"
#include "widgets/helper/LiveIndicator.hpp"
#include "widgets/helper/ScalingSpacerItem.hpp"
#include "widgets/Label.hpp"
#include "widgets/MarkdownLabel.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/Scrollbar.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/Window.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFile>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QStringBuilder>

namespace {
constexpr QStringView TEXT_FOLLOWERS = u"Followers: %1";
constexpr QStringView TEXT_CREATED = u"Created: %1";
constexpr QStringView TEXT_TITLE = u"%1's Usercard - #%2";
constexpr QStringView TEXT_USER_ID = u"ID: ";
constexpr QStringView TEXT_UNAVAILABLE = u"(not available)";
constexpr QStringView TEXT_PRONOUNS = u"Pronouns: %1";
constexpr QStringView TEXT_UNSPECIFIED = u"(unspecified)";
constexpr QStringView TEXT_LOADING = u"(loading...)";

constexpr QStringView SEVENTV_TWITCH_USER_API =
    u"https://7tv.io/v3/users/twitch/%1";
constexpr QStringView SEVENTV_KICK_USER_API =
    u"https://7tv.io/v3/users/kick/%1";

using namespace chatterino;

Label *addCopyableLabel(LayoutCreator<QHBoxLayout> box, const char *tooltip,
                        PixmapButton **copyButton = nullptr)
{
    auto label = box.emplace<Label>();
    auto button = box.emplace<PixmapButton>();
    if (copyButton != nullptr)
    {
        button.assign(copyButton);
    }
    button->setPixmap(getApp()->getThemes()->buttons.copy);
    button->setScaleIndependentSize(18, 18);
    button->setDim(DimButton::Dim::Lots);
    button->setToolTip(tooltip);
    QObject::connect(
        button.getElement(), &Button::leftClicked,
        [label = label.getElement()] {
            auto copyText = label->property("copy-text").toString();

            crossPlatformCopy(copyText.isEmpty() ? label->getText() : copyText);
        });

    return label.getElement();
};

bool checkMessageUserName(const QString &userName, MessagePtr message)
{
    if (message->flags.has(MessageFlag::Whisper))
    {
        return false;
    }

    bool isSubscription = message->flags.has(MessageFlag::Subscription) &&
                          message->loginName.isEmpty() &&
                          message->messageText.split(" ").at(0).compare(
                              userName, Qt::CaseInsensitive) == 0;

    bool isModAction =
        message->timeoutUser.compare(userName, Qt::CaseInsensitive) == 0;
    bool isSelectedUser =
        message->loginName.compare(userName, Qt::CaseInsensitive) == 0;

    return (isSubscription || isModAction || isSelectedUser);
}

ChannelPtr filterMessages(const QString &userName, ChannelPtr channel)
{
    std::vector<MessagePtr> snapshot = channel->getMessageSnapshot();

    ChannelPtr channelPtr;
    if (channel->isTwitchChannel())
    {
        channelPtr = std::make_shared<TwitchChannel>(channel->getName());
    }
    else
    {
        channelPtr =
            std::make_shared<Channel>(channel->getName(), Channel::Type::None);
    }

    for (const auto &message : snapshot)
    {
        if (checkMessageUserName(userName, message))
        {
            channelPtr->addMessage(message, MessageContext::Repost);
        }
    }

    return channelPtr;
};

const auto borderColor = QColor(255, 255, 255, 80);

int calculateTimeoutDuration(TimeoutButton timeout)
{
    static const QMap<QString, int> durations{
        {"s", 1}, {"m", 60}, {"h", 3600}, {"d", 86400}, {"w", 604800},
    };
    return timeout.second * durations[timeout.first];
}

QString hashUrl(const QString &url)
{
    QByteArray bytes;

    bytes.append(url.toUtf8());
    QByteArray hashBytes(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));

    return hashBytes.toHex();
}

}  // namespace

namespace chatterino {

using namespace literals;

UserInfoPopup::UserInfoPopup(bool closeAutomatically, Split *split)
    : DraggablePopup(closeAutomatically, split)
    , split_(split)
    , closeAutomatically_(closeAutomatically)
{
    assert(split != nullptr &&
           "split being nullptr causes lots of bugs down the road");
    this->setWindowTitle("Usercard");

    HotkeyController::HotkeyMap actions{
        {"delete",
         [this](std::vector<QString>) -> QString {
             this->deleteLater();
             return "";
         }},
        {"scrollPage",
         [this](std::vector<QString> arguments) -> QString {
             if (arguments.size() == 0)
             {
                 qCWarning(chatterinoHotkeys)
                     << "scrollPage hotkey called without arguments!";
                 return "scrollPage hotkey called without arguments!";
             }
             auto direction = arguments.at(0);

             auto &scrollbar = this->ui_.latestMessages->getScrollBar();
             if (direction == "up")
             {
                 scrollbar.offset(-scrollbar.getPageSize());
             }
             else if (direction == "down")
             {
                 scrollbar.offset(scrollbar.getPageSize());
             }
             else
             {
                 qCWarning(chatterinoHotkeys) << "Unknown scroll direction";
             }
             return "";
         }},
        {"execModeratorAction",
         [this](std::vector<QString> arguments) -> QString {
             if (arguments.empty())
             {
                 return "execModeratorAction action needs an argument, which "
                        "moderation action to execute, see description in the "
                        "editor";
             }
             auto target = arguments.at(0);
             QString msg;

             // these can't have /timeout/ buttons because they are not timeouts
             if (target == "ban")
             {
                 msg = QString("/ban %1").arg(this->userName_);
             }
             else if (target == "unban")
             {
                 msg = QString("/unban %1").arg(this->userName_);
             }
             else
             {
                 // find and execute timeout button #TARGET

                 bool ok;
                 int buttonNum = target.toInt(&ok);
                 if (!ok)
                 {
                     return QString("Invalid argument for execModeratorAction: "
                                    "%1. Use "
                                    "\"ban\", \"unban\" or the number of the "
                                    "timeout "
                                    "button to execute")
                         .arg(target);
                 }

                 const auto &timeoutButtons =
                     getSettings()->timeoutButtons.getValue();
                 if (static_cast<int>(timeoutButtons.size()) < buttonNum ||
                     0 >= buttonNum)
                 {
                     return QString("Invalid argument for execModeratorAction: "
                                    "%1. Integer out of usable range: [1, %2]")
                         .arg(buttonNum,
                              static_cast<int>(timeoutButtons.size()) - 1);
                 }
                 const auto &button = timeoutButtons.at(buttonNum - 1);
                 msg = QString("/timeout %1 %2")
                           .arg(this->userName_)
                           .arg(calculateTimeoutDuration(button));
             }

             msg = getApp()->getCommands()->execCommand(
                 msg, this->underlyingChannel_, false);

             this->underlyingChannel_->sendMessage(msg);
             return "";
         }},
        {"pin",
         [this](std::vector<QString> /*arguments*/) -> QString {
             this->togglePinned();
             return "";
         }},

        // these actions make no sense in the context of a usercard, so they aren't implemented
        {"reject", nullptr},
        {"accept", nullptr},
        {"openTab", nullptr},
        {"search", nullptr},
    };

    this->shortcuts_ = getApp()->getHotkeys()->shortcutsForCategory(
        HotkeyCategory::PopupWindow, actions, this);

    auto layers = LayoutCreator<QWidget>(this->getLayoutContainer())
                      .setLayoutType<QGridLayout>()
                      .withoutMargin();
    auto layout = layers.emplace<QVBoxLayout>();

    // first line
    auto head = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        auto avatarBox = head.emplace<QVBoxLayout>().withoutMargin();
        // avatar
        auto avatar = avatarBox.emplace<PixmapButton>(nullptr).assign(
            &this->ui_.avatarButton);
        avatar->setScaleIndependentSize(100, 100);
        avatar->setDim(DimButton::Dim::None);
        QObject::connect(
            avatar.getElement(), &Button::clicked,
            [this](Qt::MouseButton button) {
                if (this->isKick_)
                {
                    this->onKickProfilePictureClick(button);
                    return;
                }

                switch (button)
                {
                    case Qt::LeftButton: {
                        QDesktopServices::openUrl(
                            QUrl("https://www.twitch.tv/" +
                                 this->userName_.toLower()));
                    }
                    break;

                    case Qt::RightButton: {
                        // don't raise open context menu if there's no avatar (probably in cases when invalid user's usercard was opened)
                        if (this->avatarUrl_.isEmpty())
                        {
                            return;
                        }

                        auto *menu = new QMenu(this);
                        menu->setAttribute(Qt::WA_DeleteOnClose);

                        auto avatarUrl = this->avatarUrl_;

                        // add context menu actions
                        menu->addAction("Open avatar in browser", [avatarUrl] {
                            QDesktopServices::openUrl(QUrl(avatarUrl));
                        });

                        menu->addAction("Copy avatar link", [avatarUrl] {
                            crossPlatformCopy(avatarUrl);
                        });

                        // we need to assign login name for msvc compilation
                        auto loginName = this->userName_.toLower();
                        menu->addAction(
                            "Open channel in a new popup window", this,
                            [loginName] {
                                auto *app = getApp();
                                auto &window = app->getWindows()->createWindow(
                                    WindowType::Popup, true);
                                auto *split = window.getNotebook()
                                                  .getOrAddSelectedPage()
                                                  ->appendNewSplit(false);
                                split->setChannel(
                                    app->getTwitch()->getOrAddChannel(
                                        loginName.toLower()));
                            });

                        menu->addAction(
                            "Open channel in a new tab", this, [loginName] {
                                ChannelPtr channel =
                                    getApp()->getTwitch()->getOrAddChannel(
                                        loginName);
                                auto &nb = getApp()
                                               ->getWindows()
                                               ->getMainWindow()
                                               .getNotebook();
                                SplitContainer *container = nb.addPage(true);
                                Split *split = new Split(container);
                                split->setChannel(channel);
                                container->insertSplit(split);
                            });
                        menu->popup(QCursor::pos());
                        menu->raise();
                    }
                    break;

                    default:;
                }
            });
        auto switchAv =
            avatarBox.emplace<LabelButton>(QString{}, nullptr, QSize{2, 2})
                .assign(&this->ui_.switchAvatars);
        switchAv->hide();
        QObject::connect(
            switchAv.getElement(), &LabelButton::leftClicked, [this] {
                if (!this->seventvAvatar_)
                {
                    this->ui_.switchAvatars->hide();
                    return;
                }
                this->isTwitchAvatarShown_ = !this->isTwitchAvatarShown_;
                if (this->isTwitchAvatarShown_)
                {
                    this->seventvAvatar_->stop();
                    this->ui_.avatarButton->setPixmap(this->avatarPixmap_);
                    this->ui_.switchAvatars->setText("Show 7TV");
                }
                else
                {
                    this->ui_.avatarButton->setPixmap(
                        this->seventvAvatar_->currentPixmap());
                    this->seventvAvatar_->start();
                    this->ui_.switchAvatars->setText(u"Show " %
                                                     this->platformName());
                }
                this->updateAvatarUrl();
            });

        auto vbox = head.emplace<QVBoxLayout>();
        {
            // items on the right
            {
                auto box = vbox.emplace<QHBoxLayout>()
                               .withoutMargin()
                               .withoutSpacing();

                this->ui_.nameLabel = addCopyableLabel(box, "Copy name");
                this->ui_.nameLabel->setFontStyle(FontStyle::UiMediumBold);
                this->ui_.nameLabel->setPadding(QMargins(8, 0, 1, 0));
                this->ui_.liveIndicator = new LiveIndicator;
                this->ui_.liveIndicator->hide();
                // addCopyableLabel adds the copy button last -> add the indicator before that
                box->insertWidget(box->count() - 1, this->ui_.liveIndicator);
                box->insertItem(box->count() - 1,
                                ScalingSpacerItem::horizontal(7));
                box->addSpacing(5);
                box->addStretch(1);

                this->ui_.localizedNameLabel =
                    addCopyableLabel(box, "Copy localized name",
                                     &this->ui_.localizedNameCopyButton);
                this->ui_.localizedNameLabel->setFontStyle(
                    FontStyle::UiMediumBold);
                box->addSpacing(5);
                box->addStretch(1);

                auto palette = QPalette();
                palette.setColor(QPalette::WindowText, QColor("#aaa"));
                this->ui_.userIDLabel = addCopyableLabel(box, "Copy ID");
                this->ui_.userIDLabel->setPalette(palette);

                this->ui_.localizedNameLabel->setVisible(false);
                this->ui_.localizedNameCopyButton->setVisible(false);

                // button to pin the window (only if we close automatically)
                if (this->closeAutomatically_)
                {
                    box->addWidget(this->createPinButton());
                }
            }

            // items on the left
            if (getSettings()->showPronouns)
            {
                vbox.emplace<Label>(TEXT_PRONOUNS.arg(TEXT_LOADING))
                    .assign(&this->ui_.pronounsLabel);
            }
            vbox.emplace<Label>(TEXT_FOLLOWERS.arg(""))
                .assign(&this->ui_.followerCountLabel);
            vbox.emplace<Label>(TEXT_CREATED.arg(""))
                .assign(&this->ui_.createdDateLabel);
            vbox.emplace<Label>("").assign(&this->ui_.followageLabel);
            vbox.emplace<Label>("").assign(&this->ui_.subageLabel);
        }
    }

    layout.emplace<Line>(false);

    // second line
    auto user = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        user->addStretch(1);

        user.emplace<QCheckBox>("Block").assign(&this->ui_.block);
        user.emplace<QCheckBox>("Ignore highlights")
            .assign(&this->ui_.ignoreHighlights);
        // visibility of this is updated in setData

        user.emplace<LabelButton>("Add notes", this)
            .assign(&this->ui_.notesAdd);
        auto usercard = user.emplace<LabelButton>("Usercard", this)
                            .assign(&this->ui_.usercardLabel);
        auto mod = user.emplace<PixmapButton>(this);
        mod->setPixmap(getResources().buttons.mod);
        mod->setScaleIndependentSize(30, 30);
        auto unmod = user.emplace<PixmapButton>(this);
        unmod->setPixmap(getResources().buttons.unmod);
        unmod->setScaleIndependentSize(30, 30);
        auto vip = user.emplace<PixmapButton>(this);
        vip->setPixmap(getResources().buttons.vip);
        vip->setScaleIndependentSize(30, 30);
        auto unvip = user.emplace<PixmapButton>(this);
        unvip->setPixmap(getResources().buttons.unvip);
        unvip->setScaleIndependentSize(30, 30);

        user->addStretch(1);

        QObject::connect(usercard.getElement(), &Button::leftClicked, [this] {
            QDesktopServices::openUrl("https://www.twitch.tv/popout/" +
                                      this->underlyingChannel_->getName() +
                                      "/viewercard/" + this->userName_);
        });

        QObject::connect(mod.getElement(), &Button::leftClicked, [this] {
            QString value = "/mod " + this->userName_;
            value = getApp()->getCommands()->execCommand(
                value, this->underlyingChannel_, false);
            this->underlyingChannel_->sendMessage(value);
        });
        QObject::connect(unmod.getElement(), &Button::leftClicked, [this] {
            QString value = "/unmod " + this->userName_;
            value = getApp()->getCommands()->execCommand(
                value, this->underlyingChannel_, false);
            this->underlyingChannel_->sendMessage(value);
        });
        QObject::connect(vip.getElement(), &Button::leftClicked, [this] {
            QString value = "/vip " + this->userName_;
            value = getApp()->getCommands()->execCommand(
                value, this->underlyingChannel_, false);
            this->underlyingChannel_->sendMessage(value);
        });
        QObject::connect(unvip.getElement(), &Button::leftClicked, [this] {
            QString value = "/unvip " + this->userName_;
            value = getApp()->getCommands()->execCommand(
                value, this->underlyingChannel_, false);
            this->underlyingChannel_->sendMessage(value);
        });

        // userstate
        // We can safely ignore this signal connection since this is a private signal, and
        // we only connect once
        std::ignore = this->userStateChanged_.connect([this, mod, unmod, vip,
                                                       unvip]() mutable {
            TwitchChannel *twitchChannel =
                dynamic_cast<TwitchChannel *>(this->underlyingChannel_.get());

            bool visibilityModButtons = false;

            if (twitchChannel)
            {
                bool isMyself =
                    QString::compare(getApp()
                                         ->getAccounts()
                                         ->twitch.getCurrent()
                                         ->getUserName(),
                                     this->userName_, Qt::CaseInsensitive) == 0;

                visibilityModButtons =
                    twitchChannel->isBroadcaster() && !isMyself;
            }
            mod->setVisible(visibilityModButtons);
            unmod->setVisible(visibilityModButtons);
            vip->setVisible(visibilityModButtons);
            unvip->setVisible(visibilityModButtons);
        });
    }

    auto notesPreview = layout.emplace<MarkdownLabel>(this, QString())
                            .assign(&this->ui_.notesPreview);
    notesPreview->setVisible(false);
    notesPreview->setShouldElide(true);

    auto lineMod = layout.emplace<Line>(false);

    // third line
    auto moderation = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        auto timeout = moderation.emplace<TimeoutWidget>();

        // We can safely ignore this signal connection since this is a private signal, and
        // we only connect once
        std::ignore =
            this->userStateChanged_.connect([this, lineMod, timeout]() mutable {
                TwitchChannel *twitchChannel = dynamic_cast<TwitchChannel *>(
                    this->underlyingChannel_.get());

                bool visible = false;
                if (twitchChannel)
                {
                    bool isMyself =
                        getApp()
                            ->getAccounts()
                            ->twitch.getCurrent()
                            ->getUserName()
                            .compare(this->userName_, Qt::CaseInsensitive) == 0;
                    bool hasModRights = twitchChannel->hasModRights();
                    visible = hasModRights && !isMyself;
                }
                lineMod->setVisible(visible);
                timeout->setVisible(visible);
            });

        // We can safely ignore this signal connection since we own the button, and
        // the button will always be destroyed before the UserInfoPopup
        std::ignore = timeout->buttonClicked.connect([this](auto item) {
            TimeoutWidget::Action action;
            int arg;
            std::tie(action, arg) = item;

            switch (action)
            {
                case TimeoutWidget::Ban: {
                    if (this->underlyingChannel_)
                    {
                        QString value = "/ban " + this->userName_;
                        value = getApp()->getCommands()->execCommand(
                            value, this->underlyingChannel_, false);

                        this->underlyingChannel_->sendMessage(value);
                    }
                }
                break;
                case TimeoutWidget::Unban: {
                    if (this->underlyingChannel_)
                    {
                        QString value = "/unban " + this->userName_;
                        value = getApp()->getCommands()->execCommand(
                            value, this->underlyingChannel_, false);

                        this->underlyingChannel_->sendMessage(value);
                    }
                }
                break;
                case TimeoutWidget::Timeout: {
                    if (this->underlyingChannel_)
                    {
                        QString value = "/timeout " + this->userName_ + " " +
                                        QString::number(arg);

                        value = getApp()->getCommands()->execCommand(
                            value, this->underlyingChannel_, false);

                        this->underlyingChannel_->sendMessage(value);
                    }
                }
                break;
            }
        });
    }

    layout.emplace<Line>(false);

    // fourth line (last messages)
    auto logs = layout.emplace<QVBoxLayout>().withoutMargin();
    {
        this->ui_.noMessagesLabel = new Label("No recent messages");
        this->ui_.noMessagesLabel->setVisible(false);
        this->ui_.noMessagesLabel->setSizePolicy(QSizePolicy::Expanding,
                                                 QSizePolicy::Expanding);

        this->ui_.latestMessages =
            new ChannelView(this, this->split_, ChannelView::Context::UserCard,
                            getSettings()->scrollbackUsercardLimit);
        this->ui_.latestMessages->setMinimumSize(400, 275);
        this->ui_.latestMessages->setSizePolicy(QSizePolicy::Expanding,
                                                QSizePolicy::Expanding);

        logs->addWidget(this->ui_.noMessagesLabel);
        logs->addWidget(this->ui_.latestMessages);
        logs->setAlignment(this->ui_.noMessagesLabel, Qt::AlignHCenter);
    }

    // size grip
    if (closeAutomatically)
    {
        layers->addWidget(new InvisibleSizeGrip(this), 0, 0,
                          Qt::AlignRight | Qt::AlignBottom);
    }

    this->installEvents();
    this->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Policy::Ignored);
}

void UserInfoPopup::themeChangedEvent()
{
    BaseWindow::themeChangedEvent();

    for (auto &&child : this->findChildren<QCheckBox *>())
    {
        child->setFont(
            getApp()->getFonts()->getFont(FontStyle::UiMedium, this->scale()));
    }
}

void UserInfoPopup::scaleChangedEvent(float /*scale*/)
{
    this->themeChangedEvent();

    QTimer::singleShot(20, this, [this] {
        auto geo = this->geometry();
        geo.setWidth(10);
        geo.setHeight(10);

        this->setGeometry(geo);
    });
}

void UserInfoPopup::windowDeactivationEvent()
{
    if (this->editUserNotesDialog_.isNull() ||
        !this->editUserNotesDialog_->isVisible())
    {
        BaseWindow::windowDeactivationEvent();
    }
}

void UserInfoPopup::installEvents()
{
    std::shared_ptr<bool> ignoreNext = std::make_shared<bool>(false);

    // block
    QObject::connect(
        this->ui_.block, &QCheckBox::stateChanged,
        [this](int newState) mutable {
            if (this->isKick_)
            {
                return;
            }

            auto currentUser = getApp()->getAccounts()->twitch.getCurrent();

            const auto reenableBlockCheckbox = [this] {
                this->ui_.block->setEnabled(true);
            };

            if (!this->ui_.block->isEnabled())
            {
                reenableBlockCheckbox();
                return;
            }

            if (newState == Qt::Unchecked)
            {
                this->ui_.block->setEnabled(false);

                getApp()->getAccounts()->twitch.getCurrent()->unblockUser(
                    this->userId_, this->userName_, this,
                    [this, reenableBlockCheckbox, currentUser] {
                        this->channel_->addSystemMessage(
                            QString("You successfully unblocked user %1")
                                .arg(this->userName_));
                        reenableBlockCheckbox();
                    },
                    [this, reenableBlockCheckbox] {
                        this->channel_->addSystemMessage(
                            QString("User %1 couldn't be unblocked, an unknown "
                                    "error occurred!")
                                .arg(this->userName_));
                        reenableBlockCheckbox();
                    });
                return;
            }

            if (newState == Qt::Checked)
            {
                this->ui_.block->setEnabled(false);

                bool wasPinned = this->ensurePinned();
                auto btn = QMessageBox::warning(
                    this, u"Blocking " % this->userName_,
                    u"Blocking %1 can cause unintended side-effects like unfollowing.\n\n"_s
                    "Are you sure you want to block %1?".arg(this->userName_),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (wasPinned)
                {
                    this->togglePinned();
                }
                if (btn != QMessageBox::Yes)
                {
                    reenableBlockCheckbox();
                    QSignalBlocker blocker(this->ui_.block);
                    this->ui_.block->setCheckState(Qt::Unchecked);
                    return;
                }

                getApp()->getAccounts()->twitch.getCurrent()->blockUser(
                    this->userId_, this->userName_, this,
                    [this, reenableBlockCheckbox, currentUser] {
                        this->channel_->addSystemMessage(
                            QString("You successfully blocked user %1")
                                .arg(this->userName_));
                        reenableBlockCheckbox();
                    },
                    [this, reenableBlockCheckbox] {
                        this->channel_->addSystemMessage(
                            QString("User %1 couldn't be blocked, an "
                                    "unknown error occurred!")
                                .arg(this->userName_));
                        reenableBlockCheckbox();
                    });
                return;
            }

            qCWarning(chatterinoWidget)
                << "Unexpected check-state when blocking" << this->userName_
                << QMetaEnum::fromType<Qt::CheckState>().valueToKey(newState);
        });

    // ignore highlights
    QObject::connect(
        this->ui_.ignoreHighlights, &QCheckBox::clicked,
        [this](bool checked) mutable {
            this->ui_.ignoreHighlights->setEnabled(false);

            if (checked)
            {
                getSettings()->blacklistedUsers.insert(
                    HighlightBlacklistUser{this->userName_, false});
                this->ui_.ignoreHighlights->setEnabled(true);
            }
            else
            {
                const auto &vector = getSettings()->blacklistedUsers.raw();

                for (int i = 0; i < static_cast<int>(vector.size()); i++)
                {
                    if (this->userName_ ==
                        vector[static_cast<size_t>(i)].getPattern())
                    {
                        getSettings()->blacklistedUsers.removeAt(i);
                        i--;
                    }
                }
                if (getSettings()->isBlacklistedUser(this->userName_))
                {
                    this->ui_.ignoreHighlights->setToolTip(
                        "Name matched by regex");
                }
                else
                {
                    this->ui_.ignoreHighlights->setEnabled(true);
                }
            }
        });

    // user notes
    QObject::connect(
        this->ui_.notesAdd, &LabelButton::clicked, [this]() mutable {
            if (this->editUserNotesDialog_.isNull())
            {
                this->editUserNotesDialog_ = new EditUserNotesDialog(this);
                // ignoring since it the dialog is only used in this instance
                std::ignore = this->editUserNotesDialog_->onOk.connect(
                    [userId = this->userId_](const QString &newNotes) {
                        getApp()->getUserData()->setUserNotes(userId, newNotes);
                    });
            }

            auto userData = getApp()->getUserData()->getUser(this->userId_);
            auto initialNotes =
                userData.has_value() ? userData->notes : QString();

            this->editUserNotesDialog_->setNotes(initialNotes);
            this->editUserNotesDialog_->updateWindowTitle(this->userName_);
            this->editUserNotesDialog_->show();
        });

    // user data updated
    this->userDataUpdatedConnection_ =
        std::make_unique<pajlada::Signals::ScopedConnection>(
            getApp()->getUserData()->userDataUpdated().connect([this]() {
                this->updateNotes();
            }));
}

void UserInfoPopup::setData(const QString &name, const ChannelPtr &channel)
{
    this->setData(name, channel, channel);
}

void UserInfoPopup::setData(const QString &name,
                            const ChannelPtr &contextChannel,
                            const ChannelPtr &openingChannel)
{
    const QStringView idPrefix = u"id:";
    bool isId = name.startsWith(idPrefix);
    if (isId)
    {
        this->userId_ = name.mid(idPrefix.size());
        this->updateNotes();
        this->userName_ = "";
    }
    else
    {
        this->userName_ = name;
    }

    this->channel_ = openingChannel;

    if (!contextChannel->isEmpty())
    {
        this->underlyingChannel_ = contextChannel;
    }
    else
    {
        this->underlyingChannel_ = openingChannel;
    }

    this->setWindowTitle(
        TEXT_TITLE.arg(name, this->underlyingChannel_->getName()));
    this->isKick_ = this->underlyingChannel_->getType() == Channel::Type::Kick;

    this->ui_.nameLabel->setText(name);
    this->ui_.nameLabel->setProperty("copy-text", name);

    if (this->isKick_)
    {
        this->updateKickUserData();
    }
    else
    {
        this->updateUserData();
    }

    this->userStateChanged_.invoke();

    if (!isId)
    {
        this->updateLatestMessages();
    }
    // If we're opening by ID, this will be called as soon as we get the information from twitch

    auto type = this->channel_->getType();
    if (type == Channel::Type::TwitchLive ||
        type == Channel::Type::TwitchWhispers || type == Channel::Type::Misc ||
        type == Channel::Type::Kick)
    {
        // not a normal twitch channel, the url opened by the button will be invalid, so hide the button
        this->ui_.usercardLabel->hide();
    }
}

void UserInfoPopup::updateLatestMessages()
{
    auto filteredChannel =
        filterMessages(this->userName_, this->underlyingChannel_);
    this->ui_.latestMessages->setChannel(filteredChannel);
    this->ui_.latestMessages->setSourceChannel(this->underlyingChannel_);

    const bool hasMessages = filteredChannel->hasMessages();
    this->ui_.latestMessages->setVisible(hasMessages);
    this->ui_.noMessagesLabel->setVisible(!hasMessages);

    // shrink dialog in case ChannelView goes from visible to hidden
    this->adjustSize();

    this->refreshConnection_ =
        std::make_unique<pajlada::Signals::ScopedConnection>(
            this->underlyingChannel_->messageAppended.connect(
                [this, hasMessages](auto message, auto) {
                    if (!checkMessageUserName(this->userName_, message))
                    {
                        return;
                    }

                    if (hasMessages)
                    {
                        // display message in ChannelView
                        this->ui_.latestMessages->channel()->addMessage(
                            message, MessageContext::Repost);
                    }
                    else
                    {
                        // The ChannelView is currently hidden, so manually refresh
                        // and display the latest messages
                        this->updateLatestMessages();
                    }
                }));
}

void UserInfoPopup::updateUserData()
{
    std::weak_ptr<bool> hack = this->lifetimeHack_;
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();

    const auto onUserFetchFailed = [this, hack] {
        if (!hack.lock())
        {
            return;
        }

        // this can occur when the account doesn't exist.
        this->ui_.followerCountLabel->setText(
            TEXT_FOLLOWERS.arg(TEXT_UNAVAILABLE));
        this->ui_.createdDateLabel->setText(TEXT_CREATED.arg(TEXT_UNAVAILABLE));

        this->ui_.nameLabel->setText(this->userName_);

        this->ui_.userIDLabel->setText(u"ID " % TEXT_UNAVAILABLE);
        this->ui_.userIDLabel->setProperty("copy-text",
                                           TEXT_UNAVAILABLE.toString());
    };
    const auto onUserFetched = [this, hack,
                                currentUser](const HelixUser &user) {
        if (!hack.lock())
        {
            return;
        }

        // Correct for when being opened with ID
        if (this->userName_.isEmpty())
        {
            this->userName_ = user.login;
            this->ui_.nameLabel->setText(user.login);

            // Ensure recent messages are shown
            this->updateLatestMessages();
        }

        this->userId_ = user.id;
        this->helixAvatarUrl_ = user.profileImageUrl;
        this->updateAvatarUrl();
        this->updateNotes();

        // copyable button for login name of users with a localized username
        if (user.displayName.toLower() != user.login)
        {
            this->ui_.localizedNameLabel->setText(user.displayName);
            this->ui_.localizedNameLabel->setProperty("copy-text",
                                                      user.displayName);
            this->ui_.localizedNameLabel->setVisible(true);
            this->ui_.localizedNameCopyButton->setVisible(true);
        }
        else
        {
            this->ui_.nameLabel->setText(user.displayName);
            this->ui_.nameLabel->setProperty("copy-text", user.displayName);
        }

        this->setWindowTitle(TEXT_TITLE.arg(
            user.displayName, this->underlyingChannel_->getName()));
        this->ui_.createdDateLabel->setText(
            TEXT_CREATED.arg(user.createdAt.section("T", 0, 0)));
        this->ui_.createdDateLabel->setToolTip(
            formatLongFriendlyDuration(
                QDateTime::fromString(user.createdAt, Qt::ISODateWithMs),
                QDateTime::currentDateTimeUtc()) +
            u" ago"_s);
        this->ui_.createdDateLabel->setMouseTracking(true);
        this->ui_.userIDLabel->setText(TEXT_USER_ID % user.id);
        this->ui_.userIDLabel->setProperty("copy-text", user.id);

        if (getApp()->getStreamerMode()->isEnabled() &&
            getSettings()->streamerModeHideUsercardAvatars)
        {
            this->ui_.avatarButton->setPixmap(getResources().streamerMode);
        }
        else
        {
            this->loadAvatar(user.id, user.profileImageUrl, false);
        }

        getHelix()->getChannelFollowers(
            user.id,
            [this, hack](const auto &followers) {
                if (!hack.lock())
                {
                    return;
                }
                this->ui_.followerCountLabel->setText(
                    TEXT_FOLLOWERS.arg(localizeNumbers(followers.total)));
            },
            [](const auto &errorMessage) {
                qCWarning(chatterinoTwitch)
                    << "Error getting followers:" << errorMessage;
            });
        getHelix()->getStreamById(
            user.id,
            [this, hack](bool isLive, const auto &stream) {
                if (!hack.lock())
                {
                    return;
                }

                if (isLive)
                {
                    this->ui_.liveIndicator->setViewers(stream.viewerCount);
                    this->ui_.liveIndicator->show();
                }
                else
                {
                    this->ui_.liveIndicator->hide();
                }
            },
            [id{user.id}]() {
                qCWarning(chatterinoWidget)
                    << "Failed to get stream for user ID" << id;
            },
            []() {});

        // get ignore state
        bool isIgnoring = currentUser->blockedUserIds().contains(user.id);

        // get ignoreHighlights state
        bool isIgnoringHighlights = false;
        const auto &vector = getSettings()->blacklistedUsers.raw();
        for (const auto &blockedUser : vector)
        {
            if (this->userName_ == blockedUser.getPattern())
            {
                isIgnoringHighlights = true;
                break;
            }
        }
        if (getSettings()->isBlacklistedUser(this->userName_) &&
            !isIgnoringHighlights)
        {
            this->ui_.ignoreHighlights->setToolTip("Name matched by regex");
        }
        else
        {
            this->ui_.ignoreHighlights->setEnabled(true);
        }
        this->ui_.block->setChecked(isIgnoring);
        this->ui_.block->setEnabled(true);
        this->ui_.ignoreHighlights->setChecked(isIgnoringHighlights);
        this->ui_.notesAdd->setEnabled(true);

        auto type = this->underlyingChannel_->getType();

        if (type == Channel::Type::Twitch)
        {
            // get followage and subage
            getIvr()->getSubage(
                this->userName_, this->underlyingChannel_->getName(),
                [this, hack](const IvrSubage &subageInfo) {
                    if (!hack.lock())
                    {
                        return;
                    }

                    if (!subageInfo.followingSince.isEmpty())
                    {
                        QDateTime followedAt = QDateTime::fromString(
                            subageInfo.followingSince, Qt::ISODate);
                        QString followingSince =
                            followedAt.toString("yyyy-MM-dd");
                        this->ui_.followageLabel->setText("❤ Following since " +
                                                          followingSince);
                        this->ui_.followageLabel->setToolTip(
                            formatLongFriendlyDuration(
                                followedAt, QDateTime::currentDateTimeUtc()) +
                            u" ago"_s);
                        this->ui_.followageLabel->setMouseTracking(true);
                    }

                    if (subageInfo.isSubHidden)
                    {
                        this->ui_.subageLabel->setText(
                            "Subscription status hidden");
                    }
                    else if (subageInfo.isSubbed)
                    {
                        this->ui_.subageLabel->setText(
                            QString("★ Tier %1 - Subscribed for %2 months")
                                .arg(subageInfo.subTier)
                                .arg(subageInfo.totalSubMonths));
                    }
                    else if (subageInfo.totalSubMonths)
                    {
                        this->ui_.subageLabel->setText(
                            QString("★ Previously subscribed for %1 months")
                                .arg(subageInfo.totalSubMonths));
                    }
                },
                [] {});
        }

        // get pronouns
        if (getSettings()->showPronouns)
        {
            getApp()->getPronouns()->getUserPronoun(
                user.login,
                [this, hack](const auto userPronoun) {
                    runInGuiThread([this, hack,
                                    userPronoun = std::move(userPronoun)]() {
                        if (!hack.lock() || this->ui_.pronounsLabel == nullptr)
                        {
                            return;
                        }
                        if (!userPronoun.isUnspecified())
                        {
                            this->ui_.pronounsLabel->setText(
                                TEXT_PRONOUNS.arg(userPronoun.format()));
                        }
                        else
                        {
                            this->ui_.pronounsLabel->setText(
                                TEXT_PRONOUNS.arg(TEXT_UNSPECIFIED));
                        }
                    });
                },
                [this, hack]() {
                    runInGuiThread([this, hack]() {
                        qCWarning(chatterinoTwitch) << "Error getting pronouns";
                        if (!hack.lock())
                        {
                            return;
                        }
                        this->ui_.pronounsLabel->setText(
                            TEXT_PRONOUNS.arg(TEXT_UNSPECIFIED));
                    });
                });
        }
    };

    if (!this->userId_.isEmpty())
    {
        getHelix()->getUserById(this->userId_, onUserFetched,
                                onUserFetchFailed);
    }
    else
    {
        getHelix()->getUserByName(this->userName_, onUserFetched,
                                  onUserFetchFailed);
    }

    this->ui_.block->setEnabled(false);
    this->ui_.ignoreHighlights->setEnabled(false);
    this->ui_.notesAdd->setEnabled(false);

    bool isMyself =
        getApp()->getAccounts()->twitch.getCurrent()->getUserName().compare(
            this->userName_, Qt::CaseInsensitive) == 0;
    this->ui_.block->setVisible(!isMyself);
    this->ui_.ignoreHighlights->setVisible(!isMyself);
}

void UserInfoPopup::loadAvatar(const QString &userID, const QString &pictureURL,
                               bool isKick)
{
    auto filename =
        getApp()->getPaths().cacheDirectory() + "/" + hashUrl(pictureURL);
    QFile cacheFile(filename);
    if (cacheFile.exists())
    {
        cacheFile.open(QIODevice::ReadOnly);
        QPixmap avatar{};

        avatar.loadFromData(cacheFile.readAll());
        this->ui_.avatarButton->setPixmap(avatar);
        this->avatarPixmap_ = std::move(avatar);
    }
    else
    {
        QNetworkRequest req(pictureURL);
        req.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino");
        static auto *manager = new QNetworkAccessManager();
        auto *reply = manager->get(req);

        QObject::connect(reply, &QNetworkReply::finished, this,
                         [this, reply, filename] {
                             if (reply->error() == QNetworkReply::NoError)
                             {
                                 const auto data = reply->readAll();

                                 QPixmap avatar;
                                 avatar.loadFromData(data);
                                 this->ui_.avatarButton->setPixmap(avatar);
                                 this->saveCacheAvatar(data, filename);
                                 this->avatarPixmap_ = std::move(avatar);
                             }
                             else
                             {
                                 this->ui_.avatarButton->setPixmap(QPixmap());
                             }
                         });
    }

    this->helixAvatarUrl_ = pictureURL;
    this->updateAvatarUrl();

    if (getSettings()->displaySevenTVAnimatedProfile)
    {
        this->loadSevenTVAvatar(userID, isKick);
    }
}

void UserInfoPopup::loadSevenTVAvatar(const QString &userID, bool isKick)
{
    auto fmt = isKick ? SEVENTV_KICK_USER_API : SEVENTV_TWITCH_USER_API;
    NetworkRequest(fmt.arg(userID))
        .timeout(20000)
        .onSuccess([this, hack = std::weak_ptr<bool>(this->lifetimeHack_)](
                       const NetworkResult &result) {
            if (!hack.lock())
            {
                return;
            }

            auto root = result.parseJson();
            auto url = root["user"].toObject()["avatar_url"].toString();

            if (url.isEmpty())
            {
                return;
            }
            if (!url.startsWith(u"https:"))
            {
                url.prepend(u"https:");
            }
            this->seventvAvatarUrl_ = url;
            if (this->helixAvatarUrl_ == this->seventvAvatarUrl_)
            {
                return;
            }

            auto dotIdx = url.lastIndexOf('.') + 1;
            QByteArray format;
            if (dotIdx > 0)
            {
                auto end = url.size();
                auto queryIdx = url.lastIndexOf('?');
                if (queryIdx > dotIdx)
                {
                    end = queryIdx;
                }
                format = QStringView(url).sliced(dotIdx, end - dotIdx).toUtf8();
            }

            // We're implementing custom caching here,
            // because we need the cached file path.
            auto hash = hashUrl(url);
            auto filename = getApp()->getPaths().cacheDirectory() + "/" + hash;

            QFile cacheFile(filename);
            if (cacheFile.exists())
            {
                this->setSevenTVAvatar(filename, format);
                return;
            }

            QNetworkRequest req(url);

            // We're using this manager instead of the one provided
            // in NetworkManager, because we're on a different thread.
            static auto *manager = new QNetworkAccessManager();
            auto *reply = manager->get(req);

            QObject::connect(reply, &QNetworkReply::finished, this,
                             [this, reply, url, filename, format] {
                                 if (reply->error() == QNetworkReply::NoError)
                                 {
                                     this->saveCacheAvatar(reply->readAll(),
                                                           filename);
                                     this->setSevenTVAvatar(filename, format);
                                 }
                                 else
                                 {
                                     qCWarning(chatterinoSeventv)
                                         << "Error fetching Profile Picture:"
                                         << reply->error();
                                 }
                             });

            return;
        })
        .execute();
}

void UserInfoPopup::setSevenTVAvatar(const QString &filename,
                                     const QByteArray &format)
{
    auto *movie = new QMovie(filename, format, this);
    if (!movie->isValid())
    {
        qCWarning(chatterinoSeventv)
            << "Error reading Profile Picture, " << movie->lastErrorString();
        return;
    }

    QObject::connect(movie, &QMovie::frameChanged, this, [this, movie] {
        this->ui_.avatarButton->setPixmap(movie->currentPixmap());
    });

    movie->start();
    this->seventvAvatar_ = movie;
    this->ui_.switchAvatars->show();
    this->ui_.switchAvatars->setText(u"Show " % this->platformName());
    this->isTwitchAvatarShown_ = false;
    this->updateAvatarUrl();
}

void UserInfoPopup::saveCacheAvatar(const QByteArray &avatar,
                                    const QString &filename) const
{
    QFile outfile(filename);
    if (outfile.open(QIODevice::WriteOnly))
    {
        if (outfile.write(avatar) == -1)
        {
            qCWarning(chatterinoImage) << "Error writing to cache" << filename;
            this->ui_.avatarButton->setPixmap(QPixmap());
        }
    }
    else
    {
        qCWarning(chatterinoImage) << "Error writing to cache" << filename;
        this->ui_.avatarButton->setPixmap(QPixmap());
    }
}

void UserInfoPopup::updateNotes()
{
    static QRegularExpression onlySpaceRegex{"^\\s*$"};

    auto userData = getApp()->getUserData()->getUser(this->userId_);
    if (!userData.has_value() ||
        onlySpaceRegex.match(userData->notes).hasMatch())
    {
        this->ui_.notesPreview->setText("");
        this->ui_.notesPreview->setVisible(false);
        return;
    }

    this->ui_.notesPreview->setText(userData->notes);
    this->ui_.notesPreview->setVisible(true);
}

void UserInfoPopup::updateKickUserData()
{
    assert(this->isKick_);

    auto onChannelFetchFailed = [](UserInfoPopup *self) {
        // this can occur when the account doesn't exist.
        self->ui_.followerCountLabel->setText(
            TEXT_FOLLOWERS.arg(TEXT_UNAVAILABLE));
        self->ui_.createdDateLabel->setText(TEXT_CREATED.arg(TEXT_UNAVAILABLE));

        self->ui_.nameLabel->setText(self->userName_);

        self->ui_.userIDLabel->setText(u"ID " % TEXT_UNAVAILABLE);
        self->ui_.userIDLabel->setProperty("copy-text",
                                           TEXT_UNAVAILABLE.toString());
    };
    auto onChannelFetched = [](UserInfoPopup *self,
                               const KickPrivateChannelInfo &channel) {
        // Correct for when being opened with ID
        if (self->userName_.isEmpty())
        {
            self->userName_ = channel.user.username;
            self->ui_.nameLabel->setText(channel.user.username);

            // Ensure recent messages are shown
            self->updateLatestMessages();
        }

        self->kickUserID_ = channel.user.userID;
        auto userIDStr = QString::number(self->kickUserID_);
        self->userId_ = u"kick:" % userIDStr;
        self->helixAvatarUrl_ = channel.user.profilePictureURL.value_or(
            u"https://kick.com/img/default-profile-pictures/default-avatar-2.webp"_s);
        self->updateAvatarUrl();
        self->updateNotes();

        self->ui_.nameLabel->setText(channel.user.username);
        self->ui_.nameLabel->setProperty("copy-text", channel.user.username);

        self->setWindowTitle(TEXT_TITLE.arg(
            channel.user.username, self->underlyingChannel_->getName()));
        self->ui_.createdDateLabel->setText(TEXT_CREATED.arg(
            channel.chatroom.createdAt.date().toString(Qt::ISODate)));
        self->ui_.createdDateLabel->setToolTip(
            formatLongFriendlyDuration(channel.chatroom.createdAt,
                                       QDateTime::currentDateTimeUtc()) +
            u" ago"_s);
        self->ui_.createdDateLabel->setMouseTracking(true);
        self->ui_.userIDLabel->setText(TEXT_USER_ID % userIDStr);
        self->ui_.userIDLabel->setProperty("copy-text", userIDStr);

        if (getApp()->getStreamerMode()->isEnabled() &&
            getSettings()->streamerModeHideUsercardAvatars)
        {
            self->ui_.avatarButton->setPixmap(getResources().streamerMode);
        }
        else
        {
            self->loadAvatar(userIDStr, self->helixAvatarUrl_, true);
        }

        self->ui_.followerCountLabel->setText(
            TEXT_FOLLOWERS.arg(localizeNumbers(channel.followersCount)));

        // get ignoreHighlights state
        bool isIgnoringHighlights = false;
        const auto &vector = getSettings()->blacklistedUsers.raw();
        for (const auto &blockedUser : vector)
        {
            if (self->userName_ == blockedUser.getPattern())
            {
                isIgnoringHighlights = true;
                break;
            }
        }
        if (getSettings()->isBlacklistedUser(self->userName_) &&
            !isIgnoringHighlights)
        {
            self->ui_.ignoreHighlights->setToolTip("Name matched by regex");
        }
        else
        {
            self->ui_.ignoreHighlights->setEnabled(true);
        }
        self->ui_.block->setChecked(/*is_ignoring=*/false);
        self->ui_.block->setEnabled(true);
        self->ui_.ignoreHighlights->setChecked(isIgnoringHighlights);
        self->ui_.notesAdd->setEnabled(true);
    };

    // FIXME: this doesn't support opening by user ID

    KickApi::privateChannelInfo(
        this->userName_, [self = QPointer(this), onChannelFetched,
                          onChannelFetchFailed](const auto &res) {
            if (!self)
            {
                return;
            }
            if (res)
            {
                onChannelFetched(self.get(), *res);
            }
            else
            {
                qCDebug(chatterinoKick)
                    << "Channel fetch failed" << res.error();
                onChannelFetchFailed(self.get());
            }
        });
    KickApi::privateUserInChannelInfo(
        this->userName_, this->underlyingChannel_->getName(),
        [self = QPointer(this)](const auto &res) {
            if (!self || !res)
            {
                return;
            }

            if (res->followingSince)
            {
                QString followingSince =
                    res->followingSince->date().toString(Qt::ISODate);
                self->ui_.followageLabel->setText("❤ Following since " +
                                                  followingSince);
                self->ui_.followageLabel->setToolTip(
                    formatLongFriendlyDuration(
                        *res->followingSince, QDateTime::currentDateTimeUtc()) +
                    u" ago"_s);
                self->ui_.followageLabel->setMouseTracking(true);
            }

            if (res->subscriptionMonths)
            {
                self->ui_.subageLabel->setText(
                    QString("★ Subscribed for %2 months")
                        .arg(*res->subscriptionMonths));
            }
        });

    this->ui_.block->setEnabled(false);
    this->ui_.ignoreHighlights->setEnabled(false);
    this->ui_.notesAdd->setEnabled(false);

    bool isMyself = false;  // FIXME: kick account
    this->ui_.block->setVisible(!isMyself);
    this->ui_.ignoreHighlights->setVisible(!isMyself);
}

void UserInfoPopup::onKickProfilePictureClick(Qt::MouseButton button)
{
    assert(this->isKick_);

    switch (button)
    {
        case Qt::LeftButton: {
            QDesktopServices::openUrl(
                QUrl("https://kick.com/" + this->userName_.toLower()));
        }
        break;

        // largely the same as on Twitch
        case Qt::RightButton: {
            if (this->avatarUrl_.isEmpty())
            {
                return;
            }

            auto *menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);

            auto avatarUrl = this->avatarUrl_;

            // add context menu actions
            menu->addAction("Open avatar in browser", this, [avatarUrl] {
                QDesktopServices::openUrl(QUrl(avatarUrl));
            });

            menu->addAction("Copy avatar link", this, [avatarUrl] {
                crossPlatformCopy(avatarUrl);
            });

            // we need to assign login name for msvc compilation
            auto username = this->userName_.toLower();
            menu->addAction(
                "Open channel in a new popup window", this, [username] {
                    auto *app = getApp();
                    auto *split = app->getWindows()
                                      ->createWindow(WindowType::Popup, true)
                                      .getNotebook()
                                      .getOrAddSelectedPage()
                                      ->appendNewSplit(false);
                    split->setChannel(
                        app->getKickChatServer()->getOrCreate(username));
                });

            menu->addAction("Open channel in a new tab", this, [username] {
                SplitContainer *container = getApp()
                                                ->getWindows()
                                                ->getMainWindow()
                                                .getNotebook()
                                                .addPage(true);
                auto *split = new Split(container);
                split->setChannel(
                    getApp()->getKickChatServer()->getOrCreate(username));
                container->insertSplit(split);
            });
            menu->popup(QCursor::pos());
            menu->raise();
        }
        break;

        default:
            break;
    }
}

QStringView UserInfoPopup::platformName() const
{
    if (this->isKick_)
    {
        return u"Kick";
    }
    return u"Twitch";
}

//
// TimeoutWidget
//
UserInfoPopup::TimeoutWidget::TimeoutWidget()
    : BaseWidget(nullptr)
{
    auto layout = LayoutCreator<TimeoutWidget>(this)
                      .setLayoutType<QHBoxLayout>()
                      .withoutMargin();

    int buttonWidth = 40;
    int buttonHeight = 32;

    layout->setSpacing(16);

    const auto addLayout = [&](const QString &text) {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
        title->addStretch(1);
        auto label = title.emplace<Label>(text);
        label->setStyleSheet("color: #BBB");
        label->setPadding(QMargins{});
        title->addStretch(1);

        auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
        hbox->setSpacing(0);
        return hbox;
    };

    const auto addButton = [&](Action action, const QString &title,
                               const QPixmap &pixmap) {
        auto button = addLayout(title).emplace<PixmapButton>(nullptr);
        button->setPixmap(pixmap);
        button->setScaleIndependentSize(buttonHeight, buttonHeight);
        button->setBorderColor(QColor(255, 255, 255, 127));

        QObject::connect(
            button.getElement(), &Button::leftClicked, [this, action] {
                this->buttonClicked.invoke(std::make_pair(action, -1));
            });
    };

    auto addTimeouts = [&](const QString &title) {
        auto hbox = addLayout(title);

        for (const auto &item : getSettings()->timeoutButtons.getValue())
        {
            auto a = hbox.emplace<LabelButton>();
            a->setPadding({0, 0});
            a->setText(QString::number(item.second) + item.first);

            a->setScaleIndependentSize(buttonWidth, buttonHeight);
            a->setBorderColor(borderColor);

            const auto pair =
                std::make_pair(Action::Timeout, calculateTimeoutDuration(item));

            QObject::connect(a.getElement(), &LabelButton::leftClicked,
                             [this, pair] {
                                 this->buttonClicked.invoke(pair);
                             });
        }
    };

    addButton(Unban, "Unban", getResources().buttons.unban);
    addTimeouts("Timeouts");
    addButton(Ban, "Ban", getResources().buttons.ban);
}

void UserInfoPopup::TimeoutWidget::paintEvent(QPaintEvent *)
{
    //    QPainter painter(this);

    //    painter.setPen(QColor(255, 255, 255, 63));

    //    painter.drawLine(0, this->height() / 2, this->width(), this->height()
    //    / 2);
}

void UserInfoPopup::updateAvatarUrl()
{
    if (this->isTwitchAvatarShown_)
    {
        this->avatarUrl_ = this->helixAvatarUrl_;
    }
    else
    {
        this->avatarUrl_ = this->seventvAvatarUrl_;
    }
}

}  // namespace chatterino
