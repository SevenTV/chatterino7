// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/SelectChannelDialog.hpp"

#include "Application.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "util/MultiChannel.hpp"
#include "widgets/BasePopup.hpp"
#include "widgets/helper/MicroNotebook.hpp"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include <vector>

namespace {

using namespace chatterino;

class AddToMultiChannel : public BasePopup
{
    Q_OBJECT

public:
    AddToMultiChannel(QWidget *parent = nullptr)
        : BasePopup(
              {
                  BaseWindow::EnableCustomFrame,
                  BaseWindow::DisableLayoutSave,
                  BaseWindow::BoundsCheckOnShow,
              },
              parent)
        , platform(new QComboBox)
        , name(new QLineEdit)
    {
        this->setAttribute(Qt::WA_DeleteOnClose);
        this->setWindowTitle("Add Channel");

        auto *layout = new QVBoxLayout(this->getLayoutContainer());

        this->platform->addItem(
            "Twitch", QVariant::fromValue(MultiChannel::Platform::Twitch));
        this->platform->addItem(
            "Kick", QVariant::fromValue(MultiChannel::Platform::Kick));
        layout->addWidget(this->platform);

        this->name->setPlaceholderText("Name");
        layout->addWidget(this->name);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                         &AddToMultiChannel::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                         &AddToMultiChannel::close);
        layout->addStretch();
        layout->addWidget(buttons);

        this->addShortcuts();
        this->name->setFocus();
    }

    void addShortcuts() override
    {
        HotkeyController::HotkeyMap actions{
            {"accept",
             [this](const std::vector<QString> &) -> QString {
                 this->accept();
                 return {};
             }},
            {"reject",
             [this](const std::vector<QString> &) -> QString {
                 this->close();
                 return {};
             }},
        };

        this->shortcuts_ = getApp()->getHotkeys()->shortcutsForCategory(
            HotkeyCategory::PopupWindow, actions, this);
    }

Q_SIGNALS:
    // NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
    void specAdded(chatterino::MultiChannel::Spec spec);

private:
    void accept()
    {
        auto nameText = this->name->text();
        auto platform =
            this->platform->currentData().value<MultiChannel::Platform>();
        if (!nameText.isEmpty())
        {
            this->specAdded(MultiChannel::Spec{
                .platform = platform,
                .name = nameText,
            });
        }
        this->close();
    }

    QComboBox *platform = nullptr;
    QLineEdit *name = nullptr;
};

QListWidgetItem *makeMultiChannelItem(const MultiChannel::Spec &spec)
{
    QString name;
    switch (spec.platform)
    {
        case MultiChannel::Platform::Twitch:
            name += u"[T] ";
            break;
        case MultiChannel::Platform::Kick:
            name += u"[K] ";
            break;
    }
    name += spec.name;
    auto *item = new QListWidgetItem(name);
    item->setData(Qt::UserRole, QVariant::fromValue(spec));
    return item;
}

QListWidgetItem *makeMultiChannelItem(const MultiChannel::ChildChannel &chan)
{
    return makeMultiChannelItem(MultiChannel::Spec{
        .platform = chan.platform,
        .name = chan.channel->getName(),
    });
}

}  // namespace

namespace chatterino {

SelectChannelDialog::SelectChannelDialog(QWidget *parent)
    : BaseWindow(
          {
              BaseWindow::Flags::EnableCustomFrame,
              BaseWindow::Flags::Dialog,
              BaseWindow::DisableLayoutSave,
              BaseWindow::BoundsCheckOnShow,
          },
          parent)
    , selectedChannel_(Channel::getEmpty())
{
    using AutoCheckedRadioButton = detail::AutoCheckedRadioButton;

    this->setWindowTitle("Select a channel to join");

    this->tabFilter_.dialog = this;

    auto &ui = this->ui_;
    auto *rootLayout = new QVBoxLayout(this->getLayoutContainer());
    rootLayout->setContentsMargins({});
    ui.notebook = new MicroNotebook(this->getLayoutContainer());
    rootLayout->addWidget(ui.notebook, 1);

    ui.twitchPage = new QWidget;
    auto *layout = new QVBoxLayout(ui.twitchPage);

    // Channel
    ui.channel = new AutoCheckedRadioButton("Channel");
    layout->addWidget(ui.channel);

    ui.channelLabel = new QLabel("Join a Twitch channel by its channel name");
    ui.channelLabel->setVisible(false);
    layout->addWidget(ui.channelLabel);

    ui.channelName = new QLineEdit();
    ui.channelName->setVisible(false);
    layout->addWidget(ui.channelName);

    QObject::connect(ui.channel, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.channelName->setVisible(enabled);
                         ui.channelLabel->setVisible(enabled);

                         if (enabled)
                         {
                             ui.channelName->setFocus();
                             ui.channelName->selectAll();
                         }
                     });

    ui.channel->installEventFilter(&this->tabFilter_);
    ui.channelName->installEventFilter(&this->tabFilter_);

    // Whispers
    ui.whispers = new AutoCheckedRadioButton("Whispers");
    layout->addWidget(ui.whispers);

    ui.whispersLabel = new QLabel(
        "Shows the whispers that you receive while Chatterino is running");
    ui.whispersLabel->setVisible(false);
    ui.whispersLabel->setWordWrap(true);
    layout->addWidget(ui.whispersLabel);

    QObject::connect(ui.whispers, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.whispersLabel->setVisible(enabled);
                     });

    ui.whispers->installEventFilter(&this->tabFilter_);

    // Mentions
    ui.mentions = new AutoCheckedRadioButton("Mentions");
    layout->addWidget(ui.mentions);

    ui.mentionsLabel = new QLabel(
        "Shows all the messages that highlight you from any channel");
    ui.mentionsLabel->setVisible(false);
    ui.mentionsLabel->setWordWrap(true);
    layout->addWidget(ui.mentionsLabel);

    QObject::connect(ui.mentions, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.mentionsLabel->setVisible(enabled);
                     });

    ui.mentions->installEventFilter(&this->tabFilter_);

    // Watching
    ui.watching = new AutoCheckedRadioButton("Watching");
    layout->addWidget(ui.watching);

    ui.watchingLabel = new QLabel("Requires the Chatterino browser extension");
    ui.watchingLabel->setVisible(false);
    layout->addWidget(ui.watchingLabel);

    QObject::connect(ui.watching, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.watchingLabel->setVisible(enabled);
                     });

    ui.watching->installEventFilter(&this->tabFilter_);

    // Live
    ui.live = new AutoCheckedRadioButton("Live");
    layout->addWidget(ui.live);

    ui.liveLabel = new QLabel("Shows when channels go live");
    ui.liveLabel->setVisible(false);
    layout->addWidget(ui.liveLabel);

    QObject::connect(ui.live, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.liveLabel->setVisible(enabled);
                     });

    ui.live->installEventFilter(&this->tabFilter_);

    // Automod
    ui.automod = new AutoCheckedRadioButton("AutoMod");
    layout->addWidget(ui.automod);

    ui.automodLabel = new QLabel("Shows when AutoMod catches a message in "
                                 "any channel you moderate.");
    ui.automodLabel->setVisible(false);
    ui.automodLabel->setWordWrap(true);
    layout->addWidget(ui.automodLabel);

    QObject::connect(ui.automod, &AutoCheckedRadioButton::toggled, this,
                     [this](bool enabled) {
                         auto &ui = this->ui_;
                         ui.automodLabel->setVisible(enabled);
                     });

    ui.automod->installEventFilter(&this->tabFilter_);

    layout->addStretch(1);

    ui.notebook->addPage(ui.twitchPage, "Twitch");

    // Kick
    {
        ui.kickPage = new QWidget;
        auto *layout = new QVBoxLayout(ui.kickPage);

        auto *kickLabel = new QLabel(
            "Join a Kick channel by its name.<br>This is <b>very "
            "experimental</b> and Chatterino7 specific. Only basic features "
            "are supported. Please report bugs <a "
            "href=\"https://github.com/SevenTV/chatterino7/issues\">here</a>.");
        kickLabel->setOpenExternalLinks(true);
        kickLabel->setWordWrap(true);
        layout->addWidget(kickLabel);

        ui.kickName = new QLineEdit();
        ui.kickName->setPlaceholderText("Username");
        layout->addWidget(ui.kickName);

        layout->addStretch(1);

        ui.notebook->addPage(ui.kickPage, "Kick");
    }
    // Multi
    {
        ui.multiPage = new QWidget;
        ui.multiView = new QListWidget;
        ui.multiIndicatorMode = new QComboBox;
        auto *layout = new QVBoxLayout(ui.multiPage);
        {
            auto *descriptionLabel = new QLabel(
                "Show multiple channels in one split. From the input box, you "
                "can select an active/context channel to send messages in. "
                "Report issues <a "
                "href=\"https://github.com/SevenTV/chatterino7/issues\">here</"
                "a>.");
            descriptionLabel->setWordWrap(true);
            descriptionLabel->setOpenExternalLinks(true);
            layout->addWidget(descriptionLabel);

            auto *header = new QWidget;
            auto *add = new QPushButton("Add");
            auto *remove = new QPushButton("Remove");
            auto *headerLayout = new QHBoxLayout(header);
            headerLayout->addWidget(add, 1);
            headerLayout->addWidget(remove, 1);
            layout->addWidget(header);

            QObject::connect(add, &QPushButton::clicked, this, [this] {
                auto *diag = new AddToMultiChannel(this);
                QObject::connect(diag, &AddToMultiChannel::specAdded, this,
                                 [this](const MultiChannel::Spec &spec) {
                                     this->ui_.multiView->addItem(
                                         makeMultiChannelItem(spec));
                                 });
                diag->show();
            });
            QObject::connect(remove, &QPushButton::clicked, this, [this] {
                delete this->ui_.multiView->currentItem();
            });
        }

        ui.multiView->setAutoScroll(true);
        ui.multiView->setSelectionMode(QListWidget::SingleSelection);
        ui.multiView->setSelectionBehavior(QListWidget::SelectRows);
        ui.multiView->setDragDropMode(QListWidget::InternalMove);
        ui.multiView->setFrameStyle(QFrame::NoFrame);
        ui.multiView->setSizeAdjustPolicy(QListView::AdjustToContents);
        layout->addWidget(ui.multiView, 1);

        layout->addWidget(new QLabel("Channel indicator:"));
        {
            using Mode = MultiChannelIndicatorMode;

            auto v = [](Mode mode) {
                return QVariant::fromValue(mode);
            };
            ui.multiIndicatorMode->addItem("None", v(Mode::None));
            ui.multiIndicatorMode->addItem("Platform badge if unselected",
                                           v(Mode::PlatformBadgeIfUnselected));
            ui.multiIndicatorMode->addItem("Platform badge",
                                           v(Mode::PlatformBadgeAlways));
            ui.multiIndicatorMode->addItem("Channel name",
                                           v(Mode::ChannelName));
            ui.multiIndicatorMode->setCurrentIndex(1);
        }
        layout->addWidget(ui.multiIndicatorMode);

        ui.notebook->addPage(ui.multiPage, "Multi");
    }

    auto *buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setContentsMargins({10, 10, 10, 10});
    rootLayout->addWidget(buttonBox);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, [this] {
        this->ok();
    });
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, [this] {
        this->close();
    });

    this->addShortcuts();

    this->themeChangedEvent();
}

void SelectChannelDialog::ok()
{
    // accept and close
    this->hasSelectedChannel_ = true;
    this->close();
}

void SelectChannelDialog::setSelectedChannel(
    std::optional<IndirectChannel> channel_)
{
    if (!channel_.has_value())
    {
        this->ui_.channel->setChecked(true);

        this->hasSelectedChannel_ = false;
        return;
    }

    const auto &indirectChannel = channel_.value();
    const auto &channel = indirectChannel.get();

    assert(channel);

    this->selectedChannel_ = channel;

    switch (indirectChannel.getType())
    {
        case Channel::Type::Twitch: {
            this->ui_.channelName->setText(channel->getName());
            this->ui_.channel->setChecked(true);
        }
        break;
        case Channel::Type::TwitchWatching: {
            this->ui_.watching->setFocus();
        }
        break;
        case Channel::Type::TwitchMentions: {
            this->ui_.mentions->setFocus();
        }
        break;
        case Channel::Type::TwitchWhispers: {
            this->ui_.whispers->setFocus();
        }
        break;
        case Channel::Type::TwitchLive: {
            this->ui_.live->setFocus();
        }
        break;
        case Channel::Type::TwitchAutomod: {
            this->ui_.automod->setFocus();
        }
        break;
        case Channel::Type::Kick: {
            this->ui_.kickName->setText(channel->getName());
            this->ui_.kickName->selectAll();
            this->ui_.notebook->select(this->ui_.kickPage);
        }
        break;
        case Channel::Type::Multi: {
            const auto *mc = dynamic_cast<const MultiChannel *>(channel.get());
            if (mc)
            {
                for (const auto &child : mc->channels())
                {
                    this->ui_.multiView->addItem(makeMultiChannelItem(child));
                }
                int indicatorIdx = this->ui_.multiIndicatorMode->findData(
                    QVariant::fromValue(mc->indicatorMode()));
                if (indicatorIdx >= 0)
                {
                    this->ui_.multiIndicatorMode->setCurrentIndex(indicatorIdx);
                }
                this->mcChannelIndex = mc->activeChannelIndex();
            }
            this->ui_.notebook->select(this->ui_.multiPage);
        }
        break;
        default: {
            this->ui_.channel->setChecked(true);
        }
    }

    this->hasSelectedChannel_ = false;
}

IndirectChannel SelectChannelDialog::getSelectedChannel() const
{
    if (!this->hasSelectedChannel_)
    {
        return this->selectedChannel_;
    }

    if (this->ui_.notebook->isSelected(this->ui_.kickPage))
    {
        return getApp()->getKickChatServer()->getOrCreate(
            this->ui_.kickName->text().trimmed());
    }

    if (this->ui_.notebook->isSelected(this->ui_.multiPage))
    {
        QVarLengthArray<MultiChannel::Spec, 4> specs;
        for (int i = 0; i < this->ui_.multiView->count(); i++)
        {
            auto *item = this->ui_.multiView->item(i);
            if (!item)
            {
                continue;
            }
            QVariant data = item->data(Qt::UserRole);
            auto *spec = get_if<MultiChannel::Spec>(&data);
            if (spec)
            {
                specs.emplace_back(std::move(*spec));
            }
        }
        auto ptr = std::make_shared<MultiChannel>(
            specs, this->ui_.multiIndicatorMode->currentData()
                       .value<MultiChannelIndicatorMode>());
        ptr->setActiveChannelIndex(this->mcChannelIndex);
        return {std::move(ptr)};
    }

    if (this->ui_.channel->isChecked())
    {
        return getApp()->getTwitch()->getOrAddChannel(
            this->ui_.channelName->text().trimmed());
    }

    if (this->ui_.watching->isChecked())
    {
        return getApp()->getTwitch()->getWatchingChannel();
    }

    if (this->ui_.mentions->isChecked())
    {
        return getApp()->getTwitch()->getMentionsChannel();
    }

    if (this->ui_.whispers->isChecked())
    {
        return getApp()->getTwitch()->getWhispersChannel();
    }

    if (this->ui_.live->isChecked())
    {
        return getApp()->getTwitch()->getLiveChannel();
    }

    if (this->ui_.automod->isChecked())
    {
        return getApp()->getTwitch()->getAutomodChannel();
    }

    return this->selectedChannel_;
}

bool SelectChannelDialog::hasSeletedChannel() const
{
    return this->hasSelectedChannel_;
}

bool SelectChannelDialog::EventFilter::eventFilter(QObject *watched,
                                                   QEvent *event)
{
    auto *widget = dynamic_cast<QWidget *>(watched);
    assert(widget);

    auto &ui = this->dialog->ui_;

    if (event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        assert(keyEvent);

        if ((keyEvent->key() == Qt::Key_Tab ||
             keyEvent->key() == Qt::Key_Down) &&
            keyEvent->modifiers() == Qt::NoModifier)
        {
            // Tab has been pressed, focus next entry in list

            if (widget == ui.channelName)
            {
                // Special case for when current selection is the "Channel" entry's edit box since the Edit box actually has the focus
                ui.whispers->setFocus();
                return true;
            }

            if (widget == ui.automod)
            {
                // Special case for when current selection is "AutoMod" (the last entry in the list), next wrap is Channel, but we need to select its edit box
                ui.channel->setFocus();
                return true;
            }

            auto *nextInFocusChain = widget->nextInFocusChain();
            if (nextInFocusChain->focusPolicy() == Qt::FocusPolicy::NoFocus)
            {
                // Make sure we're not selecting one of the labels
                nextInFocusChain = nextInFocusChain->nextInFocusChain();
            }
            nextInFocusChain->setFocus();
            return true;
        }

        if (((keyEvent->key() == Qt::Key_Tab ||
              keyEvent->key() == Qt::Key_Backtab) &&
             keyEvent->modifiers() == Qt::ShiftModifier) ||
            ((keyEvent->key() == Qt::Key_Up) &&
             keyEvent->modifiers() == Qt::NoModifier))
        {
            // Shift+Tab has been pressed, focus previous entry in list

            if (widget == ui.channelName)
            {
                // Special case for when current selection is the "Channel" entry's edit box since the Edit box actually has the focus
                ui.automod->setFocus();
                return true;
            }

            if (widget == ui.whispers)
            {
                ui.channel->setFocus();
                return true;
            }

            auto *previousInFocusChain = widget->previousInFocusChain();
            if (previousInFocusChain->focusPolicy() == Qt::FocusPolicy::NoFocus)
            {
                // Make sure we're not selecting one of the labels
                previousInFocusChain =
                    previousInFocusChain->previousInFocusChain();
            }
            previousInFocusChain->setFocus();
            return true;
        }

        if (keyEvent == QKeySequence::DeleteStartOfWord &&
            ui.channelName->selectionLength() > 0)
        {
            ui.channelName->backspace();
            return true;
        }

        return false;
    }

    return false;
}

void SelectChannelDialog::closeEvent(QCloseEvent * /*event*/)
{
    this->closed.invoke();
}

void SelectChannelDialog::themeChangedEvent()
{
    BaseWindow::themeChangedEvent();

    this->setPalette(getTheme()->palette);
}

void SelectChannelDialog::scaleChangedEvent(float newScale)
{
    BaseWindow::scaleChangedEvent(newScale);

    auto &ui = this->ui_;

    // NOTE: Normally the font is automatically inherited from its parent, but since we override
    // the style sheet to respect light/dark theme, we have to manually update the font here
    auto uiFont =
        getApp()->getFonts()->getFont(FontStyle::UiMedium, this->scale());

    ui.channelName->setFont(uiFont);
}

void SelectChannelDialog::addShortcuts()
{
    HotkeyController::HotkeyMap actions{
        {"accept",
         [this](const std::vector<QString> &) -> QString {
             this->ok();
             return "";
         }},
        {"reject",
         [this](const std::vector<QString> &) -> QString {
             this->close();
             return "";
         }},

        // these make no sense, so they aren't implemented
        {"scrollPage", nullptr},
        {"search", nullptr},
        {"delete", nullptr},
        {"openTab", nullptr},
    };

    this->shortcuts_ = getApp()->getHotkeys()->shortcutsForCategory(
        HotkeyCategory::PopupWindow, actions, this);
}

}  // namespace chatterino

#include "SelectChannelDialog.moc"
