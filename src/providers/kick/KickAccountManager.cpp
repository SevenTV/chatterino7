#include "providers/kick/KickAccountManager.hpp"

#include "common/QLogging.hpp"
#include "KickApi.hpp"
#include "providers/kick/KickAccount.hpp"
#include "util/RapidJsonSerializeQString.hpp"  // IWYU pragma: keep
#include "util/SharedPtrElementLess.hpp"

#include <pajlada/settings/setting.hpp>

namespace chatterino {

KickAccountManager::KickAccountManager()
    : accounts(SharedPtrElementLess<KickAccount>{})
    , anonymousUser_(std::make_shared<KickAccount>(KickAccountData{}))
{
    std::ignore = this->currentUserChanged.connect([this] {
        auto currentUser = this->current();
        currentUser->loadSeventvUser();
    });

    std::ignore = this->accounts.itemRemoved.connect([this](const auto &acc) {
        this->removeAccount(acc.item.get());
    });

    this->refreshTimer.setSingleShot(false);
    this->refreshTimer.setInterval(KickAccount::CHECK_REFRESH_INTERVAL);
    // NOLINTNEXTLINE(clazy-connect-3arg-lambda)
    QObject::connect(&this->refreshTimer, &QTimer::timeout, [this] {
        this->refreshAccounts();
    });
    this->refreshTimer.start();
}

std::shared_ptr<KickAccount> KickAccountManager::current()
{
    if (!this->currentUser_)
    {
        return this->anonymousUser_;
    }
    return this->currentUser_;
}

std::vector<QString> KickAccountManager::usernames() const
{
    std::vector<QString> names;
    for (const auto &acc : this->accounts.raw())
    {
        names.emplace_back(acc->username());
    }
    return names;
}

std::shared_ptr<KickAccount> KickAccountManager::findUserByUsername(
    const QString &username) const
{
    for (const auto &acc : this->accounts.raw())
    {
        if (QString::compare(acc->username(), username, Qt::CaseInsensitive) ==
            0)
        {
            return acc;
        }
    }
    return nullptr;
}

bool KickAccountManager::userExists(const QString &username) const
{
    return this->findUserByUsername(username) != nullptr;
}

bool KickAccountManager::isLoggedIn() const
{
    return this->currentUser_ && !this->currentUser_->isAnonymous();
}

void KickAccountManager::reloadUsers()
{
    auto keys =
        pajlada::Settings::SettingManager::getObjectKeys("/kickAccounts");

    bool listUpdated = false;

    for (const auto &uid : keys)
    {
        if (uid == "current")
        {
            continue;
        }

        auto data = KickAccountData::loadRaw(uid);
        if (!data)
        {
            continue;
        }

        switch (this->addAccount(*data))
        {
            case AddUserResponse::UserAlreadyExists: {
                qCDebug(chatterinoKick)
                    << "User" << data->username << "already exists";
            }
            break;
            case AddUserResponse::UserUpdated: {
                qCDebug(chatterinoKick)
                    << "User" << data->username << "updated";
                if (data->username == this->current()->username())
                {
                    this->currentUserChanged.invoke();
                }
            }
            break;
            case AddUserResponse::UserAdded: {
                qCDebug(chatterinoKick) << "Added account" << data->username;
                listUpdated = true;
            }
            break;
        }
    }

    if (listUpdated)
    {
        this->userListUpdated.invoke();
        this->refreshAccounts();
    }
}

void KickAccountManager::load()
{
    this->reloadUsers();

    this->currentUsername.connect([this](const QString &newUsername) {
        auto user = this->findUserByUsername(newUsername);
        if (user)
        {
            qCDebug(chatterinoKick) << "Kick user updated to" << newUsername;
            getKickApi()->setAuth(user->authToken());
            this->currentUser_ = user;
        }
        else
        {
            qCDebug(chatterinoKick) << "Kick user updated to anonymous";
            this->currentUser_ = this->anonymousUser_;
        }

        this->currentUserChanged.invoke();
    });
}

KickAccountManager::AddUserResponse KickAccountManager::addAccount(
    const KickAccountData &data)
{
    auto previousUser = this->findUserByUsername(data.username);
    if (previousUser)
    {
        bool userUpdated = previousUser->update(data);
        if (userUpdated)
        {
            return AddUserResponse::UserUpdated;
        }

        return AddUserResponse::UserAlreadyExists;
    }

    auto account = std::make_shared<KickAccount>(data);
    this->accounts.insert(account);
    this->holder.managedConnect(account->authUpdated, [this, account] {
        if (this->currentUser_ == account)
        {
            getKickApi()->setAuth(account->authToken());
            qCDebug(chatterinoKick)
                << "Kick auth updated for" << account->username();
        }
    });

    return AddUserResponse::UserAdded;
}

bool KickAccountManager::removeAccount(KickAccount *account)
{
    if (account->isAnonymous())
    {
        return false;
    }

    auto accountPath = "/kickAccounts/uid" + std::to_string(account->userID());
    pajlada::Settings::SettingManager::gRemoveSetting(accountPath);

    if (account->username() == this->currentUsername)
    {
        // The user that was removed is the current user, log into the anonymous
        // account
        this->currentUsername = "";
    }

    this->userListUpdated.invoke();
    return true;
}

void KickAccountManager::refreshAccounts() const
{
    for (const auto &acc : this->accounts.raw())
    {
        acc->refreshIfNeeded();
    }
}

}  // namespace chatterino
