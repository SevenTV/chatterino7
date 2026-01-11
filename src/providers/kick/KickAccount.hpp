#pragma once

#include "controllers/accounts/Account.hpp"

#include <QDateTime>
#include <QString>

#include <memory>
#include <string>

namespace chatterino {

struct KickAccountData {
    QString username;
    uint64_t userID = 0;
    QString clientID;
    QString clientSecret;
    QString authToken;
    QString refreshToken;
    QDateTime expiresAt;

    void save() const;
    static std::optional<KickAccountData> loadRaw(const std::string &key);
};

class KickAccount : public Account,
                    public std::enable_shared_from_this<KickAccount>
{
public:
    KickAccount(const KickAccountData &args);

    void save() const;

    bool update(const KickAccountData &data);

    QString toString() const override;

    bool isAnonymous() const
    {
        return this->userID_ == 0;
    }

    QString username() const
    {
        return this->username_;
    }
    uint64_t userID() const
    {
        return this->userID_;
    }
    QString clientID() const
    {
        return this->clientID_;
    }
    QString clientSecret() const
    {
        return this->clientSecret_;
    }
    QString authToken() const
    {
        return this->authToken_;
    }
    QString refreshToken() const
    {
        return this->refreshToken_;
    }

    void refreshIfNeeded();

private:
    QString username_;
    uint64_t userID_ = 0;
    QString clientID_;
    QString clientSecret_;
    QString authToken_;
    QString refreshToken_;
    QDateTime expiresAt_;
};

}  // namespace chatterino
