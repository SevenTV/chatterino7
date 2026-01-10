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
void getJson(const QString &url, std::function<void(ExpectedStr<T>)> cb)
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

void KickApi::privateChannelInfo(
    const QString &username,
    std::function<void(ExpectedStr<KickPrivateChannelInfo>)> cb)
{
    getJson<KickPrivateChannelInfo>(
        u"https://kick.com/api/v2/channels/" % username, std::move(cb));
}

void KickApi::privateUserInChannelInfo(
    const QString &userUsername, const QString &channelUsername,
    std::function<void(ExpectedStr<KickPrivateUserInChannelInfo>)> cb)
{
    getJson<KickPrivateUserInChannelInfo>(u"https://kick.com/api/v2/channels/" %
                                              channelUsername % "/users/" %
                                              userUsername,
                                          std::move(cb));
}

}  // namespace chatterino
