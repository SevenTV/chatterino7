#include "widgets/helper/KickAccountSwitchWidget.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/kick/KickAccount.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

KickAccountSwitchWidget::KickAccountSwitchWidget(QWidget *parent)
    : QListWidget(parent)
{
    this->managedConnections_.managedConnect(
        getApp()->getAccounts()->kick.userListUpdated, [this]() {
            QSignalBlocker b(this);

            this->refreshItems();
            this->refresh();
        });

    this->refreshItems();
    this->refresh();

    QObject::connect(this, &QListWidget::clicked, [this] {
        if (this->selectedItems().isEmpty())
        {
            return;
        }

        QString newUsername = this->currentItem()->text();
        if (newUsername.compare(ANONYMOUS_USERNAME_LABEL,
                                Qt::CaseInsensitive) == 0)
        {
            getApp()->getAccounts()->kick.currentUsername = "";
        }
        else
        {
            getApp()->getAccounts()->kick.currentUsername = newUsername;
        }

        std::ignore = getSettings()->requestSave();
    });
}

void KickAccountSwitchWidget::refresh()
{
    QSignalBlocker b(this);

    if (this->count() <= 0)
    {
        return;
    }

    // Select the currently logged in user
    auto currentUser = getApp()->getAccounts()->kick.current();

    if (currentUser->isAnonymous())
    {
        this->setCurrentRow(0);
        return;
    }

    QString currentUsername = currentUser->username();
    for (int i = 0; i < this->count(); ++i)
    {
        QString itemText = this->item(i)->text();

        if (itemText.compare(currentUsername, Qt::CaseInsensitive) == 0)
        {
            this->setCurrentRow(i);
            break;
        }
    }
}

void KickAccountSwitchWidget::refreshItems()
{
    this->clear();

    this->addItem(ANONYMOUS_USERNAME_LABEL);

    for (const auto &userName : getApp()->getAccounts()->kick.usernames())
    {
        this->addItem(userName);
    }
}

}  // namespace chatterino
