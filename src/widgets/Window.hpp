// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWindow.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>

namespace chatterino {

class DrawnButton;
class PixmapButton;
class LabelButton;
class SvgButton;
class Theme;
class UpdateDialog;
class SplitNotebook;
class Channel;

/**
 * @exposeenum c2.WindowType
 */
enum class WindowType { Main, Popup, Attached };

class Window : public BaseWindow
{
    Q_OBJECT

public:
    explicit Window(WindowType type, QWidget *parent);

    WindowType getType();
    SplitNotebook &getNotebook();
    bool supportsCompactHeaders() const;

    pajlada::Signals::NoArgSignal closed;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void themeChangedEvent() override;

private:
    void addCustomTitlebarButtons();
    void addDebugStuff(
        std::map<QString, std::function<QString(std::vector<QString>)>>
            &actions);
    void addShortcuts() override;
    void addLayout();
    void onAccountSelected();
    void addMenuBar();

    WindowType type_;

    SplitNotebook *notebook_;
    LabelButton *userLabel_ = nullptr;
    std::shared_ptr<UpdateDialog> updateDialogHandle_;

    bool macTitlebarSetup_ = false;

    pajlada::Signals::SignalHolder signalHolder_;

    // this is only used on Windows and only on the main window, for the one used otherwise, see SplitNotebook in Notebook.hpp
    PixmapButton *streamerModeTitlebarIcon_ = nullptr;
    void updateStreamerModeIcon();

    // Compact header: buttons shown in titlebar/tab-row when compactHeaders is enabled
    LabelButton *compactHeaderLabel_ = nullptr;
    DrawnButton *compactDropdownButton_ = nullptr;  // Linux: notebook tab row
    SvgButton *compactModButton_ = nullptr;
    SvgButton *compactChattersButton_ = nullptr;
    LabelButton *compactModeButton_ = nullptr;
    pajlada::Signals::SignalHolder compactHeaderConnections_;
    void updateCompactHeader();
    void updateCompactHeaderButtons();
    void updateCompactHeaderMode();
    void setupCompactHeaderConnections();

    friend class Notebook;
};

}  // namespace chatterino
