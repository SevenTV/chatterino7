#include "providers/kick/KickAccountManager.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"
#include "util/SharedPtrElementLess.hpp"

namespace chatterino {

KickAccountManager::KickAccountManager()
    : accounts(SharedPtrElementLess<KickAccount>{})
    , anonymousUser_(std::make_shared<KickAccount>(KickAccountData{}))
{
    this->currentUserChanged.connect([this] {
        // auto currentUser = this->current();
        // FIXME: load 7tv user
    });

    std::ignore = this->accounts.itemRemoved.connect([this](const auto &acc) {
        this->removeAccount(acc.item.get());
    });
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

void KickAccountManager::reloadUsers()
{
    auto keys = pajlada::Settings::SettingManager::getObjectKeys("/accounts");

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
                    this->currentUserChanged();
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
    }
}

void KickAccountManager::load()
{
    this->reloadUsers();

    this->currentUsername.connect([this](const QString &newUsername) {
        auto user = this->findUserByUsername(newUsername);
        if (user)
        {
            qCDebug(chatterinoTwitch)
                << "Twitch user updated to" << newUsername;
            getHelix()->update(user->getOAuthClient(), user->getOAuthToken());
            this->currentUser_ = user;
        }
        else
        {
            qCDebug(chatterinoKick) << "Kick user updated to anonymous";
            this->currentUser_ = this->anonymousUser_;
        }

        this->currentUserChanged();
    });
}

}  // namespace chatterino
