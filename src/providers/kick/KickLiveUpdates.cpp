#include "providers/kick/KickLiveUpdates.hpp"

#include "common/FlagsEnum.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/kick/ws/KickCentrifugoManager.hpp"
#include "providers/kick/ws/KickPusherManager.hpp"
#include "providers/kick/ws/KickWebSocketManager.hpp"
#include "singletons/Settings.hpp"

#include <boost/unordered/unordered_flat_set.hpp>

using namespace Qt::Literals;

namespace chatterino {

namespace {

FlagsEnum<KickConnectionPreference> currentPrefs()
{
    FlagsEnum<KickConnectionPreference> pref =
        getSettings()->kickConnectionPreference;
    if (pref == KickConnectionPreference::Default)
    {
        pref = KickConnectionPreference::Pusher;
    }
    return pref;
}

std::unique_ptr<KickWebSocketManager> makeDefaultManager(
    const QString &clientID)
{
    auto prefs = currentPrefs();
    if (prefs.has(KickConnectionPreference::Centrifugo))
    {
        return std::make_unique<KickCentrifugoManager>(
            u"wss://realtime.us-east-1.platform.kick.com/connection/websocket"_s,
            clientID);
    }

    return std::make_unique<KickPusherManager>(u"", u"");
}

}  // namespace

class KickLiveUpdatesPrivate
    : public std::enable_shared_from_this<KickLiveUpdatesPrivate>
{
public:
    KickLiveUpdatesPrivate();

    std::unique_ptr<KickWebSocketManager> manager;
    boost::unordered_flat_set<std::pair<uint64_t, uint64_t>> backlog;
    bool requestInProgress = false;

    QString clientID;

    bool hasManagerOrFetch();

    void flushBacklog();

    void joinImpl(uint64_t roomID, uint64_t channelID);
    void partImpl(uint64_t roomID, uint64_t channelID);

private:
    friend KickLiveUpdates;
};

KickLiveUpdatesPrivate::KickLiveUpdatesPrivate()
    : clientID(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

bool KickLiveUpdatesPrivate::hasManagerOrFetch()
{
    assertInGuiThread();

    if (this->manager)
    {
        return true;
    }
    if (!this->requestInProgress)
    {
        this->requestInProgress = true;

        auto pref = currentPrefs();
        QJsonArray accepted;
        if (pref.has(KickConnectionPreference::Pusher))
        {
            accepted.append(QJsonObject{{"provider"_L1, "pusher"_L1}});
        }
        if (pref.has(KickConnectionPreference::Centrifugo))
        {
            accepted.append(QJsonObject{{"provider"_L1, "centrifugo"_L1}});
        }

        // FIXME: Is it okay to just pass `1` here? It doesn't really depend on
        // the channel, but maybe Kick does some load balancing based on it.
        NetworkRequest(
            u"https://web.kick.com/api/v1/realtime/channels/1/chat/connection"_s,
            NetworkRequestType::Post)
            .json(QJsonObject{
                {
                    "client"_L1,
                    QJsonObject{
                        {"id"_L1, this->clientID},
                        {"type"_L1, "web"_L1},
                    },
                },
                {
                    "capabilities"_L1,
                    QJsonObject{{"accepted_providers"_L1, accepted}},
                },
            })
            .onSuccess([weak =
                            this->weak_from_this()](const NetworkResult &res) {
                auto self = weak.lock();
                if (!self)
                {
                    return;
                }

                const auto json = res.parseJson();
                const auto connection =
                    json["data"_L1]["connections"_L1][0].toObject();
                auto provider = connection["provider"_L1].toString();
                if (provider == u"pusher")
                {
                    const auto creds = connection["credentials"_L1].toObject();
                    auto appKey = creds["app_key"_L1].toString();
                    auto cluster = creds["cluster"_L1].toString();
                    qCDebug(chatterinoKick)
                        << "Using Pusher with app_key:" << appKey
                        << "cluster:" << cluster;
                    self->manager =
                        std::make_unique<KickPusherManager>(appKey, cluster);
                }
                else if (provider == u"centrifugo")
                {
                    const auto creds = connection["credentials"_L1].toObject();
                    auto url = creds["url"_L1].toString();
                    qCDebug(chatterinoKick)
                        << "Using Centrifugo with url:" << url;
                    self->manager = std::make_unique<KickCentrifugoManager>(
                        url, self->clientID);
                }
                else
                {
                    qCWarning(chatterinoKick)
                        << "Unknown Kick provider, using Pusher"
                        << res.getData();
                    self->manager = makeDefaultManager(self->clientID);
                }

                self->flushBacklog();
            })
            .onError([weak = this->weak_from_this()](const NetworkResult &res) {
                auto self = weak.lock();
                if (!self)
                {
                    return;
                }
                qCWarning(chatterinoKick)
                    << "Failed to request Kick connection, using Pusher"
                    << res.formatError();
                // Just use pusher. This may be wrong, but the best we can do.
                // FIXME: Definitely wrong if the user isn't connected.
                self->manager = std::make_unique<KickPusherManager>(u"", u"");
                self->flushBacklog();
            })
            .execute();
    }

    return false;
}

void KickLiveUpdatesPrivate::flushBacklog()
{
    assertInGuiThread();
    this->requestInProgress = false;
    if (!this->manager)
    {
        assert(false);
        return;
    }
    auto backlog = std::exchange(this->backlog, {});
    for (const auto [roomID, channelID] : backlog)
    {
        this->joinImpl(roomID, channelID);
    }
    assert(this->backlog.empty());
}

void KickLiveUpdatesPrivate::joinImpl(uint64_t roomID, uint64_t channelID)
{
    if (!this->hasManagerOrFetch())
    {
        this->backlog.insert({roomID, channelID});
        return;
    }
    this->manager->joinChannel(u"chatroom_" % QString::number(roomID));
    this->manager->joinChannel(u"chatrooms." % QString::number(roomID));
    this->manager->joinChannel(u"chatrooms." % QString::number(roomID) %
                               u".v2");
    this->manager->joinChannel(u"channel." % QString::number(channelID));
    this->manager->joinChannel(u"channel_" % QString::number(channelID));
    this->manager->joinChannel(u"predictions-channel-" %
                               QString::number(channelID));
}

void KickLiveUpdatesPrivate::partImpl(uint64_t roomID, uint64_t channelID)
{
    if (!this->hasManagerOrFetch())
    {
        this->backlog.erase({roomID, channelID});
        return;
    }
    this->manager->partChannel(u"chatroom_" % QString::number(roomID));
    this->manager->partChannel(u"chatrooms." % QString::number(roomID));
    this->manager->partChannel(u"chatrooms." % QString::number(roomID) %
                               u".v2");
    this->manager->partChannel(u"channel." % QString::number(channelID));
    this->manager->partChannel(u"channel_" % QString::number(channelID));
    this->manager->partChannel(u"predictions-channel-" %
                               QString::number(channelID));
}

KickLiveUpdates::KickLiveUpdates()
    : private_(std::make_shared<KickLiveUpdatesPrivate>())
{
}
KickLiveUpdates::~KickLiveUpdates() = default;

void KickLiveUpdates::joinRoom(uint64_t roomID, uint64_t channelID)
{
    this->private_->joinImpl(roomID, channelID);
}

void KickLiveUpdates::leaveRoom(uint64_t roomID, uint64_t channelID)
{
    this->private_->partImpl(roomID, channelID);
}

}  // namespace chatterino
