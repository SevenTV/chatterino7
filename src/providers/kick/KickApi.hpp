#pragma once

#include "util/Expected.hpp"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <functional>
#include <span>

namespace chatterino {

class BoostJsonObject;
class NetworkRequest;

// Private API

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

struct KickPrivateEmoteInfo {
    KickPrivateEmoteInfo(BoostJsonObject obj);

    uint64_t emoteID = 0;
    QString name;
    bool subscribersOnly = false;
};

struct KickPrivateEmoteSetInfo {
    KickPrivateEmoteSetInfo(BoostJsonObject obj);

    // if this is set, it's a user set - otherwise it's global
    std::optional<uint64_t> userID;
    std::vector<KickPrivateEmoteInfo> emotes;
};

// Public API

struct KickCategoryInfo {
    KickCategoryInfo(BoostJsonObject obj);

    QString name;
};

struct KickStreamInfo {
    KickStreamInfo(BoostJsonObject obj);

    bool isLive = false;
    uint64_t viewerCount = 0;
    QDateTime startTime;
    QString thumbnailUrl;
};

struct KickChannelInfo {
    KickChannelInfo(BoostJsonObject obj);

    uint64_t userID = 0;
    KickCategoryInfo category;
    KickStreamInfo stream;
    QString streamTitle;
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

    static void privateEmotesInChannel(
        const QString &username,
        Callback<std::vector<KickPrivateEmoteSetInfo>> cb);

    void sendMessage(uint64_t broadcasterUserID, const QString &message,
                     const QString &replyToMessageID, Callback<void> cb);

    void getChannels(std::span<uint64_t> userIDs,
                     Callback<std::vector<KickChannelInfo>> cb);

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
