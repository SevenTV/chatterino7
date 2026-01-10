#pragma once

#include "common/SignalVector.hpp"

#include <QString>

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

    boost::signals2::signal<void()> currentUserChanged;
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

    std::shared_ptr<KickAccount> currentUser_;
    std::shared_ptr<KickAccount> anonymousUser_;
};

}  // namespace chatterino
