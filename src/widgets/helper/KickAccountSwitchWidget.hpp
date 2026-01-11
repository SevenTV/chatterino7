#pragma once

#include <pajlada/signals/signalholder.hpp>
#include <QListWidget>

namespace chatterino {

// FIXME: this is mostly the same as the Twitch account switcher, but we don't
// have some common base class
class KickAccountSwitchWidget : public QListWidget
{
public:
    explicit KickAccountSwitchWidget(QWidget *parent = nullptr);

    void refresh();

private:
    void refreshItems();

    pajlada::Signals::SignalHolder managedConnections_;
};

}  // namespace chatterino
