#include "providers/kick/KickAccount.hpp"

#include "Application.hpp"
#include "common/ChatterinoSetting.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/settings/settinglistener.hpp>
#include <pajlada/settings/settingmanager.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QUrlQuery>

namespace chatterino {

using namespace Qt::Literals;

std::optional<KickAccountData> KickAccountData::loadRaw(const std::string &key)
{
    auto username = QStringSetting::get("/kickAccounts/" + key + "/username");
    auto userID = UInt64Setting::get("/kickAccounts/" + key + "/userID");
    auto clientID = QStringSetting::get("/kickAccounts/" + key + "/clientID");
    auto clientSecret =
        QStringSetting::get("/kickAccounts/" + key + "/clientSecret");
    auto authToken = QStringSetting::get("/kickAccounts/" + key + "/authToken");
    auto refreshToken =
        QStringSetting::get("/kickAccounts/" + key + "/refreshToken");
    auto expiresAtStr =
        QStringSetting::get("/kickAccounts/" + key + "/expiresAt");

    if (username.isEmpty() || userID == 0 || clientID.isEmpty() ||
        clientSecret.isEmpty() || authToken.isEmpty() ||
        refreshToken.isEmpty() || expiresAtStr.isEmpty())
    {
        return std::nullopt;
    }

    QDateTime expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);

    return KickAccountData{
        .username = username.trimmed(),
        .userID = userID,
        .clientID = clientID.trimmed(),
        .clientSecret = clientSecret.trimmed(),
        .authToken = authToken.trimmed(),
        .refreshToken = refreshToken.trimmed(),
        .expiresAt = expiresAt,
    };
}

void KickAccountData::save() const
{
    auto basePath = "/kickAccounts/uid" + std::to_string(this->userID);
    QStringSetting::set(basePath + "/username", this->username.toLower());
    UInt64Setting::set(basePath + "/userID", this->userID);
    QStringSetting::set(basePath + "/clientID", this->clientID);
    QStringSetting::set(basePath + "/clientSecret", this->clientSecret);
    QStringSetting::set(basePath + "/authToken", this->authToken);
    QStringSetting::set(basePath + "/refreshToken", this->refreshToken);
    QStringSetting::set(basePath + "/expiresAt",
                        this->expiresAt.toString(Qt::ISODate));
    std::ignore = getSettings()->requestSave();
}

KickAccount::KickAccount(const KickAccountData &args)
    : Account(ProviderId::Kick)
    , username_(args.username.toLower())
    , userID_(args.userID)
    , clientID_(args.clientID)
    , clientSecret_(args.clientSecret)
    , authToken_(args.authToken)
    , refreshToken_(args.refreshToken)
    , expiresAt_(args.expiresAt)
{
}

KickAccount::~KickAccount() = default;

void KickAccount::save() const
{
    KickAccountData{
        .username = this->username_,
        .userID = this->userID_,
        .clientID = this->clientID_,
        .clientSecret = this->clientSecret_,
        .authToken = this->authToken_,
        .refreshToken = this->refreshToken_,
        .expiresAt = this->expiresAt_,
    }
        .save();
}

bool KickAccount::update(const KickAccountData &data)
{
    bool changed = false;

    if (QString::compare(this->username_, data.username, Qt::CaseInsensitive) ==
        0)
    {
        changed = true;
        this->username_ = data.username;
    }
    if (this->userID_ != data.userID)
    {
        changed = true;
        this->userID_ = data.userID;
    }
    if (this->clientID_ != data.clientID)
    {
        changed = true;
        this->clientID_ = data.clientID;
    }
    if (this->clientSecret_ != data.clientSecret)
    {
        changed = true;
        this->clientSecret_ = data.clientSecret;
    }
    if (this->authToken_ != data.authToken)
    {
        changed = true;
        this->authToken_ = data.authToken;
    }
    if (this->refreshToken_ != data.refreshToken)
    {
        changed = true;
        this->refreshToken_ = data.refreshToken;
    }
    if (this->expiresAt_ != data.expiresAt)
    {
        changed = true;
        this->expiresAt_ = data.expiresAt;
    }

    if (changed)
    {
        this->save();
    }
    return changed;
}

QString KickAccount::toString() const
{
    return this->username_;
}

void KickAccount::refreshIfNeeded()
{
    if (this->isAnonymous())
    {
        return;
    }

    auto now = QDateTime::currentDateTimeUtc() + CHECK_REFRESH_INTERVAL +
               std::chrono::seconds{30};
    if (now < this->expiresAt_)
    {
        return;
    }

    QUrlQuery payload{
        {"refresh_token"_L1, this->refreshToken_},
        {"client_id"_L1, this->clientID_},
        {"client_secret"_L1, this->clientSecret_},
        {"grant_type"_L1, "refresh_token"_L1},
    };

    auto weak = this->weak_from_this();
    NetworkRequest(u"https://id.kick.com/oauth/token"_s,
                   NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .payload(payload.toString(QUrl::FullyEncoded).toUtf8())
        .onSuccess([weak](const NetworkResult &res) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto json = res.parseJson();
            self->authToken_ = json["access_token"_L1].toString();
            self->refreshToken_ = json["refresh_token"_L1].toString();
            auto expiresInSec =
                std::clamp<qint64>(json["expires_in"_L1].toInteger(), 0,
                                   std::numeric_limits<qint32>::max());
            self->expiresAt_ =
                QDateTime::currentDateTimeUtc().addSecs(expiresInSec);
            self->save();
            self->authUpdated.invoke();
        })
        .onError([weak](const NetworkResult &res) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            qCWarning(chatterinoKick) << "Failed to refresh" << self->username()
                                      << "error:" << res.formatError();
        })
        .execute();
}

}  // namespace chatterino
