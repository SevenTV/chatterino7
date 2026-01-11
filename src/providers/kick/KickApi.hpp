#pragma once

#include "util/Expected.hpp"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <functional>

namespace chatterino {

class BoostJsonObject;
class NetworkRequest;

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
    template <typename T>
    using Callback = std::function<void(ExpectedStr<T>)>;

    static KickApi *instance();

    static void privateChannelInfo(const QString &username,
                                   Callback<KickPrivateChannelInfo> cb);

    static void privateUserInChannelInfo(
        const QString &userUsername, const QString &channelUsername,
        Callback<KickPrivateUserInChannelInfo> cb);

    void sendMessage(uint64_t broadcasterUserID, const QString &message,
                     const QString &replyToMessageID, Callback<void> cb);

    void setAuth(const QString &authToken);

private:
    KickApi();

    template <typename T>
    void getJson(const QString &endpoint, Callback<T> cb);

    template <typename T>
    void postJson(const QString &endpoint, const QJsonObject &json,
                  Callback<T> cb);

    template <typename T>
    void doRequest(NetworkRequest &&req, Callback<T> cb);

    QByteArray authToken;
};

KickApi *getKickApi();

}  // namespace chatterino
