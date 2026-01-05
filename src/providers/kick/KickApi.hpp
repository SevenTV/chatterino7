#pragma once

#include "util/Expected.hpp"

#include <QString>

#include <cstdint>
#include <functional>

namespace chatterino {

class BoostJsonObject;

struct KickPrivateUserInfo {
    KickPrivateUserInfo(BoostJsonObject obj);

    uint64_t userID = 0;
    QString username;
};

struct KickPrivateChatroomInfo {
    KickPrivateChatroomInfo(BoostJsonObject obj);

    uint64_t roomID = 0;
};

struct KickPrivateChannelInfo {
    KickPrivateChannelInfo(BoostJsonObject obj);

    uint64_t channelID = 0;
    uint64_t followersCount = 0;
    KickPrivateUserInfo user;
    KickPrivateChatroomInfo chatroom;
};

class KickApi
{
public:
    static void privateChannelInfo(
        const QString &slug,
        std::function<void(ExpectedStr<KickPrivateChannelInfo>)> cb);
};

}  // namespace chatterino
