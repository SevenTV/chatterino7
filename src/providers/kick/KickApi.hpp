#pragma once

#include "util/Expected.hpp"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <functional>

namespace chatterino {

class BoostJsonObject;

struct KickPrivateUserInfo {
    KickPrivateUserInfo(BoostJsonObject obj);

    uint64_t userID = 0;
    QString username;
    std::optional<QString> profilePictureURL;
};

struct KickPrivateChatroomInfo {
    KickPrivateChatroomInfo(BoostJsonObject obj);

    uint64_t roomID = 0;
    QDateTime createdAt;
};

struct KickPrivateChannelInfo {
    KickPrivateChannelInfo(BoostJsonObject obj);

    uint64_t channelID = 0;
    uint64_t followersCount = 0;
    KickPrivateUserInfo user;
    KickPrivateChatroomInfo chatroom;
};

struct KickPrivateUserInChannelInfo {
    KickPrivateUserInChannelInfo(BoostJsonObject obj);

    uint64_t userID = 0;
    std::optional<QDateTime> followingSince;
    std::optional<uint16_t> subscriptionMonths;
    std::optional<QString> profilePictureURL;
};

class KickApi
{
public:
    static void privateChannelInfo(
        const QString &username,
        std::function<void(ExpectedStr<KickPrivateChannelInfo>)> cb);

    static void privateUserInChannelInfo(
        const QString &userUsername, const QString &channelUsername,
        std::function<void(ExpectedStr<KickPrivateUserInChannelInfo>)> cb);
};

}  // namespace chatterino
