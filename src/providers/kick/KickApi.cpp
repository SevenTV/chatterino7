#include "providers/kick/KickApi.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "util/BoostJsonWrap.hpp"

#include <boost/json/parse.hpp>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

template <typename T>
void getJsonNoAuth(const QString &url, std::function<void(ExpectedStr<T>)> cb)
{
    NetworkRequest(url)
        .onError([cb](const NetworkResult &res) {
            cb(makeUnexpected(res.formatError()));
        })
        .onSuccess([cb = std::move(cb)](const NetworkResult &res) {
            const auto &ba = res.getData();
            boost::system::error_code ec;
            auto jv =
                boost::json::parse(std::string_view(ba.data(), ba.size()), ec);
            if (ec)
            {
                qCWarning(chatterinoKick)
                    << "Failed to parse API response:" << ec.message();
                cb(makeUnexpected(u"Failed to parse API response: "_s %
                                  QString::fromStdString(ec.message())));
                return;
            }

            BoostJsonValue ref(jv);
            if (!ref.isObject())
            {
                qCWarning(chatterinoKick) << "Root value was not an object";
                cb(makeUnexpected(u"Root value was not an object"_s));
                return;
            }
            cb(T(ref.toObject()));
        })
        .execute();
}

QString makePublicV1Url(QStringView endpoint)
{
    return u"https://api.kick.com/public/v1/" % endpoint;
}

}  // namespace

namespace chatterino {

KickPrivateUserInfo::KickPrivateUserInfo(BoostJsonObject obj)
    : userID(obj["id"].toUint64())
    , username(obj["username"].toQString())
{
    auto pictureUrl = obj["profile_pic"];
    if (pictureUrl.isString())
    {
        this->profilePictureURL = pictureUrl.toQString();
    }
}

KickPrivateChatroomInfo::KickPrivateChatroomInfo(BoostJsonObject obj)
    : roomID(obj["id"].toUint64())
    , createdAt(QDateTime::fromString(obj["created_at"].toQString(),
                                      Qt::ISODateWithMs))
{
}

KickPrivateChannelInfo::KickPrivateChannelInfo(BoostJsonObject obj)
    : channelID(obj["id"].toUint64())
    , followersCount(obj["followers_count"].toUint64())
    , user(obj["user"].toObject())
    , chatroom(obj["chatroom"].toObject())
{
}

KickPrivateUserInChannelInfo::KickPrivateUserInChannelInfo(BoostJsonObject obj)
    : userID(obj["id"].toUint64())

{
    auto followingSinceStr = obj["following_since"].toQString();
    if (!followingSinceStr.isEmpty())
    {
        this->followingSince =
            QDateTime::fromString(followingSinceStr, Qt::ISODateWithMs);
    }

    auto months = obj["subscribed_for"].toUint64();
    if (months > 0 && months < std::numeric_limits<uint16_t>::max())
    {
        this->subscriptionMonths = static_cast<uint16_t>(months);
    }

    auto pictureUrl = obj["profile_pic"];
    if (pictureUrl.isString())
    {
        this->profilePictureURL = pictureUrl.toQString();
    }
}

KickApi *KickApi::instance()
{
    static std::unique_ptr<KickApi> api;
    if (!api)
    {
        api = std::unique_ptr<KickApi>{new KickApi};
    }
    return api.get();
}

void KickApi::privateChannelInfo(const QString &username,
                                 Callback<KickPrivateChannelInfo> cb)
{
    getJsonNoAuth<KickPrivateChannelInfo>(
        u"https://kick.com/api/v2/channels/" % username, std::move(cb));
}

void KickApi::privateUserInChannelInfo(
    const QString &userUsername, const QString &channelUsername,
    Callback<KickPrivateUserInChannelInfo> cb)
{
    getJsonNoAuth<KickPrivateUserInChannelInfo>(
        u"https://kick.com/api/v2/channels/" % channelUsername % "/users/" %
            userUsername,
        std::move(cb));
}

void KickApi::sendMessage(uint64_t broadcasterUserID, const QString &message,
                          const QString &replyToMessageID, Callback<void> cb)
{
    struct Response {
        Response(BoostJsonObject obj)
            : isSent(obj["is_sent"].toBool())
        {
        }
        bool isSent = false;
    };

    QJsonObject json{
        {"broadcaster_user_id"_L1, static_cast<qint64>(broadcasterUserID)},
        {"content"_L1, message},
        {"type"_L1, "user"_L1},
    };
    if (!replyToMessageID.isEmpty())
    {
        json.insert("reply_to_message_id"_L1, replyToMessageID);
    }
    this->postJson<Response>(
        u"chat"_s, json,
        [cb = std::move(cb)](const ExpectedStr<Response> &res) {
            cb(res.and_then([](Response res) {
                if (res.isSent)
                {
                    return ExpectedStr<void>{};
                }
                return ExpectedStr<void>{
                    makeUnexpected(u"Message was not sent"_s)};
            }));
        });
}

void KickApi::setAuth(const QString &authToken)
{
    this->authToken = authToken.toUtf8();
}

template <typename T>
void KickApi::getJson(const QString &endpoint, Callback<T> cb)
{
    this->doRequest(NetworkRequest(makePublicV1Url(endpoint)), std::move(cb));
}

template <typename T>
void KickApi::postJson(const QString &endpoint, const QJsonObject &json,
                       Callback<T> cb)
{
    this->doRequest(
        NetworkRequest(makePublicV1Url(endpoint), NetworkRequestType::Post)
            .json(json),
        std::move(cb));
}

template <typename T>
void KickApi::doRequest(NetworkRequest &&req, Callback<T> cb)
{
    std::move(req)
        .header("Authorization"_ba, "Bearer "_ba + this->authToken)
        .onError([cb](const NetworkResult &res) {
            auto message = res.parseJson().value("message").toString();
            if (!message.isEmpty())
            {
                cb(makeUnexpected(message));
            }
            else
            {
                cb(makeUnexpected(res.formatError()));
            }
        })
        .onSuccess([cb = std::move(cb)](const NetworkResult &res) {
            const auto &ba = res.getData();
            boost::system::error_code ec;
            auto jv =
                boost::json::parse(std::string_view(ba.data(), ba.size()), ec);
            if (ec)
            {
                qCWarning(chatterinoKick)
                    << "Failed to parse API response:" << ec.message();
                cb(makeUnexpected(u"Failed to parse API response: "_s %
                                  QString::fromStdString(ec.message())));
                return;
            }

            BoostJsonValue ref(jv);
            if (!ref.isObject())
            {
                qCWarning(chatterinoKick) << "Root value was not an object";
                cb(makeUnexpected(u"Root value was not an object"_s));
                return;
            }
            auto dataObj = ref["data"];
            if (!dataObj.isObject())
            {
                qCWarning(chatterinoKick) << "Data value was not an object";
                cb(makeUnexpected(u"'data' value was not an object"_s));
                return;
            }
            cb(T(dataObj.toObject()));
        })
        .execute();
}

KickApi::KickApi() = default;

KickApi *getKickApi()
{
    return KickApi::instance();
}

}  // namespace chatterino
