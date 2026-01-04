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
}

KickPrivateChatroomInfo::KickPrivateChatroomInfo(BoostJsonObject obj)
    : roomID(obj["id"].toUint64())
{
}

KickPrivateChannelInfo::KickPrivateChannelInfo(BoostJsonObject obj)
    : channelID(obj["id"].toUint64())
    , followersCount(obj["followers_count"].toUint64())
    , user(obj["user"].toObject())
    , chatroom(obj["chatroom"].toObject())
{
}

void KickApi::privateChannelInfo(
    const QString &slug,
    std::function<void(ExpectedStr<KickPrivateChannelInfo>)> cb)
{
    getJson<KickPrivateChannelInfo>(u"https://kick.com/api/v2/channels/" % slug,
                                    std::move(cb));
}

}  // namespace chatterino
