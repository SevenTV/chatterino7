#pragma once

#include "common/SignalVector.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QString>
#include <QTimer>

namespace chatterino {

class KickAccount;
struct KickAccountData;

class KickAccountManager
{
public:
    KickAccountManager();

    std::shared_ptr<KickAccount> current();

    std::vector<QString> usernames() const;

    std::shared_ptr<KickAccount> findUserByUsername(
        const QString &username) const;
    bool userExists(const QString &username) const;

    void reloadUsers();
    void load();

    bool isLoggedIn() const;

    pajlada::Settings::Setting<QString> currentUsername{"/kickAccounts/current",
                                                        ""};

    pajlada::Signals::NoArgSignal currentUserChanged;
    pajlada::Signals::NoArgSignal userListUpdated;

    SignalVector<std::shared_ptr<KickAccount>> accounts;

private:
    enum class AddUserResponse : uint8_t {
        UserAlreadyExists,
        UserUpdated,
        UserAdded,
    };
    AddUserResponse addAccount(const KickAccountData &data);
    bool removeAccount(KickAccount *account);

    void refreshAccounts() const;

    std::shared_ptr<KickAccount> currentUser_;
    std::shared_ptr<KickAccount> anonymousUser_;
    QTimer refreshTimer;
    pajlada::Signals::SignalHolder holder;
};

}  // namespace chatterino
