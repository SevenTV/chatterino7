#include "GeneralPage.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "controllers/sound/SoundController.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "util/Helpers.hpp"
#include "util/IncognitoBrowser.hpp"
#include "util/LoadPixmap.hpp"
#include "util/StreamLink.hpp"
#include "widgets/BaseWindow.hpp"
#include "widgets/SettingsDialog.hpp"
#include "widgets/helper/Line.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"

#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QScrollArea>
#include <QStandardPaths>

// define any helper functions or classes that are not members of the class here

namespace chatterino {

using namespace pajlada::Settings;

namespace {

    QString streamlinkTooltip(const std::vector<std.pair<QString, QString>> &paths)
    {
        QString tooltip = "Choose your streamlink executable file.";

        if (paths.empty())
        {
            tooltip += "<br><br>No streamlink executable found.";
        }
        else
        {
            tooltip += "<br><br>Found streamlink executable in the following locations:";
            for (const auto &pair : paths)
            {
                tooltip += QString("<br>-%1 (%2)").arg(pair.first, pair.second);
            }
        }

        return tooltip;
    }

}  // namespace

GeneralPage::GeneralPage()
{
    auto *y = new GeneralPageView;
    this->setPageView(y);

    this->setLocalizedTitle("General");

    y->addTitle("General");
    y->addCheckbox("Show changelog on update", getSettings()->showChangelogOnUpdate);

    y->addTitle("Links");
    y->addCheckbox("Open links in incognito/private mode", getSettings()->openLinksInIncognito);

    y->addTitle("Timestamps");
    y->addCheckbox("Show timestamps", getSettings()->showTimestamps);
    y->addDropdown<QString>(
        "Timestamp format", getSettings()->timestampFormat,
        {"h:mm", "hh:mm", "h:mm a", "hh:mm a", "h:mm:ss", "hh:mm:ss", "h:mm:ss a", "hh:mm:ss a"},
        [](const auto &s) {
            return s;
        });

    y->addTitle("Messages");
    y->addCheckbox("Show message length", getSettings()->showMessageLength);
    y->addCheckbox("Show centered messages (e.g. sub notices)", getSettings()->showCenteredMessages);
    y->addCheckbox("Allow sending duplicate messages (holding Shift)", getSettings()->allowDuplicateMessages);
    y->addCheckbox("Show which channels are in streamer mode", getSettings()->showJoinedChannelsInStreamerMode);
    y->addDropdown<LastMessageLineStyle>(
        "Draw a line below the last message", getSettings()->lastMessageLineStyle,
        {
            LastMessageLineStyle::None,
            LastMessageLineStyle::OnBottom,
            LastMessageLineStyle::OnBottomAndOnSecondToLast,
        },
        [](const auto &v) {
            switch (v)
            {
                case LastMessageLineStyle::None:
                    return "Never";
                case LastMessageLineStyle::OnBottom:
                    return "Only on the bottom-most message";
                case LastMessageLineStyle::OnBottomAndOnSecondToLast:
                    return "On the last two messages";
            }
            return "";
        });
    y->addDropdown<TimeoutStackStyle>(
        "Show how many times a user has been timed out in this channel",
        getSettings()->timeoutStackStyle,
        {
            TimeoutStackStyle::None,
            TimeoutStackStyle::Text,
            TimeoutStackStyle::Icons,
        },
        [](const auto &v) {
            switch (v)
            {
                case TimeoutStackStyle::None:
                    return "Disabled";
                case TimeoutStackStyle::Text:
                    return "Enabled (text)";
                case TimeoutStackStyle::Icons:
                    return "Enabled (icons)";
            }
            return "";
        });

    y->addTitle("Streamer Mode");
    auto *streamerModeCheckbox =
        y->addCheckbox("Enable Streamer Mode (hides user content)", getSettings()->streamerMode);
    QObject::connect(streamerModeCheckbox, &QCheckBox::stateChanged, this, [this](int state) {
        if (getSettings()->streamerMode)
        {
            // When enabling streamer mode, ask the user if they're sure
            this->showStreamerModeEnableConfirmDialog();
        }
    });

    auto *automaticStreamerMode = y->addCheckbox("Automatically enable Streamer Mode when streaming software is running",
                                                  getSettings()->enableStreamerModeAutomatically);
    auto *detectStreamingSoftware =
        y->addCheckbox("Detect streaming software (OBS, Streamlabs Desktop, XSplit)",
                       getSettings()->detectStreamingSoftware);
#ifdef Q_OS_WIN
    auto *automaticStreamerModeOnScreen =
        y->addCheckbox("Automatically enable Streamer Mode depending on which screen Chatterino is on",
                       getSettings()->enableStreamerModeWithWindow);
    auto *streamerModeScreenChooser = y->addDropdown<StreamerModeScreen>(
        "Screen to check for", getSettings()->streamerModeScreen,
        {
            StreamerModeScreen::Same,
            StreamerModeScreen::All,
        },
        [](const auto &v) {
            switch (v)
            {
                case StreamerModeScreen::Same:
                    return "Same screen as Chatterino";
                case StreamerModeScreen::All:
                    return "Any screen";
            }
            return "";
        });
    streamerModeScreenChooser->setToolTip(
        "Checks if any of the chosen screens are being captured by streaming software");
    streamerModeScreenChooser->setVisible(getSettings()->enableStreamerModeWithWindow);
    QObject::connect(automaticStreamerModeOnScreen, &QCheckBox::stateChanged, this,
                     [streamerModeScreenChooser](int state) {
                         streamerModeScreenChooser->setVisible(state == Qt::Checked);
                     });
#endif

    // Hide sub-settings when automatic streamer mode is disabled
    detectStreamingSoftware->setVisible(getSettings()->enableStreamerModeAutomatically);
#ifdef Q_OS_WIN
    automaticStreamerModeOnScreen->setVisible(getSettings()->enableStreamerModeAutomatically);
    streamerModeScreenChooser->setVisible(getSettings()->enableStreamerModeAutomatically &&
                                          getSettings()->enableStreamerModeWithWindow);
#endif

    QObject::connect(automaticStreamerMode, &QCheckBox::stateChanged, this,
                     [detectStreamingSoftware
#ifdef Q_OS_WIN
                      ,
                      automaticStreamerModeOnScreen, streamerModeScreenChooser
#endif
    ](int state) {
        detectStreamingSoftware->setVisible(state == Qt::Checked);
#ifdef Q_OS_WIN
        automaticStreamerModeOnScreen->setVisible(state == Qt::Checked);
        streamerModeScreenChooser->setVisible(state == Qt::Checked &&
                                              getSettings()->enableStreamerModeWithWindow);
#endif
    });

    y->addTitle("Miscellaneous");

#ifdef Q_OS_WIN
    if (supportsIncognitoLinks())
    {
        y->addDropdown<QString>(
            "Browser to open links in", getSettings()->openLinksIn, {"Default", "Chrome", "Firefox", "Edge"},
            [](const auto &v) {
                return v;
            },
            true);
    }
#endif
    y->addDropdown<ThumbnailPreviewMode>(
        "Show thumbnail previews on link hover", getSettings()->thumbnailPreviewMode,
        {
            ThumbnailPreviewMode::Off,
            ThumbnailPreviewMode::OnlyFromSameChannel,
            ThumbnailPreviewMode::AllChannels,
        },
        [](const auto &v) {
            switch (v)
            {
                case ThumbnailPreviewMode::Off:
                    return "Off";
                case ThumbnailPreviewMode::OnlyFromSameChannel:
                    return "From the same channel";
                case ThumbnailPreviewMode::AllChannels:
                    return "From all channels";
            }
            return "";
        });
    y->addDropdown<QString>(
        "Play sound on highlight", getSettings()->highlightSoundUrl,
        {getSettings()->highlightSoundUrl.getValue(), "Default", "Custom"},
        [this](const auto &v) {
            if (v == "Default")
            {
                return "Default";
            }
            if (v == "Custom")
            {
                this->selectCustomHighlightSound();
                return getSettings()->highlightSoundUrl.getValue();
            }

            auto url = QUrl(v);
            if (url.isLocalFile())
            {
                return url.fileName();
            }

            return v;
        });

    y->addCheckbox("Restart on crash", getSettings()->restartOnCrash);

    // clang-format off
    y->addTitle("Logging");
    auto *logPath = new PathInput(getSettings()->logPath);
    logPath->setPlaceholderText("Leave empty to disable logging");
    y->addWidget(logPath);
    y->addDescription("Path to directory where logs will be stored. \nLogs are sorted by channels, dates and splits. File format is Month-Day_Year.");
    // clang-format on

    y->addTitle("Streamlink");
    auto *execPath = new PathInput(getSettings()->streamlinkPath);
    execPath->setPlaceholderText("Choose path to streamlink executable");
    execPath->setTooltip(streamlinkTooltip(findStreamlinkPaths()));
    execPath->getButton()->setText("Find");
    y->addWidget(execPath);

    auto *customArguments = new QLineEdit();
    getSettings()->streamlinkCustomArguments.connect([&](const auto &val, auto) {
        customArguments->setText(val);
    });
    y->addDescription("Arguments to pass to streamlink (e.g. --player-passthrough=hls,rtmp,http)");
    y->addWidget(customArguments);

    y->addTitle("Cache");
    auto *clearCacheButton = new QPushButton("Clear emote cache");
    y->addWidget(clearCacheButton);

    auto *cachePathLabel = new QLabel;
    cachePathLabel->setWordWrap(true);
    cachePathLabel->setText(formatRichString(
        "Cache is stored in '{}'",
        formatRichString("<a href='file:/{path}'><span style='color: #0092DB;'>{path}</span></a>",
                         getPaths()->cacheDirectory())));
    cachePathLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    cachePathLabel->setOpenExternalLinks(true);
    y->addWidget(cachePathLabel);

    y->addStretch();

    // ---- end of settings
    QObject::connect(customArguments, &QLineEdit::textChanged, this, [this](const QString &newValue) {
        getSettings()->streamlinkCustomArguments = newValue;
    });

    QObject::connect(clearCacheButton, &QPushButton::clicked, this, [this] {
        QMessageBox box;
        box.setText("Are you sure you want to clear the cache?");
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        int ret = box.exec();

        if (ret == QMessageBox::Yes)
        {
            this->clearCache();
        }
    });

#if 0
    // ------
    y->addCheckbox("Pause chat when hovering over it",
                   getSettings()->pauseChatOnHover);

    // ------
    y->addTitle("Emotes");
    y->addCheckbox("Enable emotes", getSettings()->enableEmotes);
    y->addCheckbox("Enable gif animations", getSettings()->enableGifAnimations);

    // ------
    y->addTitle("UI");
    y->addCheckbox("Show last read message indicator",
                   getSettings()->showLastReadMessageIndicator);
#endif
}

void GeneralPage::selectCustomHighlightSound()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open Sound"), "/home/", tr("Sound Files (*.wav *.mp3)"));
    getSettings()->highlightSoundUrl = QUrl::fromLocalFile(fileName).toString();
}

void GeneralPage::clearCache()
{
    auto confirmation =
        QMessageBox::question(this, "Clear cache",
                              "This will clear all cached emotes and badges. Continue?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (confirmation != QMessageBox::Yes)
    {
        return;
    }

    auto cachePath = getPaths()->cacheDirectory();
    qCDebug(chatterinoWidget) << "Clearing cache at " << cachePath;

    QDir dir(cachePath);
    if (!dir.removeRecursively())
    {
        qCWarning(chatterinoWidget) << "Failed to clear cache. Maybe a permission error?";
    }
}

void GeneralPage::showStreamerModeEnableConfirmDialog()
{
    if (this->streamerModeEnableConfirmDialog_)
    {
        this->streamerModeEnableConfirmDialog_->show();
        this->streamerModeEnableConfirmDialog_->raise();
        return;
    }

    this->streamerModeEnableConfirmDialog_ = new QMessageBox(
        QMessageBox::Information, "Enable Streamer Mode?",
        "Are you sure you want to enable Streamer Mode?\n\nThis will hide user-generated content like "
        "messages, usernames, and profile pictures to protect your privacy while streaming.",
        QMessageBox::Yes | QMessageBox::No, this);
    this->streamerModeEnableConfirmDialog_->exec();

    if (this->streamerModeEnableConfirmDialog_->result() != QMessageBox::Yes)
    {
        getSettings()->streamerMode = false;
    }

    this->streamerModeEnableConfirmDialog_->deleteLater();
    this->streamerModeEnableConfirmDialog_ = nullptr;
}

}  // namespace chatterino
