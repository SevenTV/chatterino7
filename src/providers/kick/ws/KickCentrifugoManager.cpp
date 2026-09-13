// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/ws/KickCentrifugoManager.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/ws/KickWebSocketCommon.hpp"
#include "providers/liveupdates/BasicPubSubClient.hpp"
#include "providers/liveupdates/BasicPubSubManager.hpp"
#include "util/BoostJsonWrap.hpp"
#include "util/Helpers.hpp"
#include "util/Variant.hpp"

#include <boost/json.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <QPointer>

#include <utility>

using namespace Qt::Literals;

namespace {

using namespace chatterino;

constexpr std::chrono::seconds MAX_HEARTBEAT_INTERVAL{20};

class KickCentrifugoClient
    : public BasicPubSubClient<QString, KickCentrifugoClient>,
      public std::enable_shared_from_this<KickCentrifugoClient>
{
public:
    KickCentrifugoClient(QString clientID, QPointer<KickChatServer> chatServer);

    void onOpen() /* override */;

    void onMessage(const QByteArray &msg);

    std::chrono::steady_clock::time_point lastHeartbeat() const
    {
        return this->lastHeartbeat_;
    }

    std::chrono::milliseconds heartbeatInterval() const
    {
        return this->heartbeatInterval_;
    }

    void checkHeartbeat();

    void subscribeImpl(const Subscription &topic);
    void unsubscribeImpl(const Subscription &topic);

    // unimplemented
    QByteArray encodeSubscription(const Subscription &subscription);
    QByteArray encodeUnsubscription(const Subscription &subscription);

private:
    void onMessageUi(QByteArrayView msg);
    void onResponseOrUnk(BoostJsonObject root, QByteArrayView raw);

    void doRefresh();
    void sendRefreshOrConnect();

    std::string encodeSubImpl(std::string channel);
    std::string encodeUnsubImpl(std::string_view channel);

    std::chrono::steady_clock::time_point lastHeartbeat_;
    std::chrono::milliseconds heartbeatInterval_;

    QTimer refreshTimer;
    bool isRefreshing = false;
    bool isConnected = false;
    uint32_t nextId = 1;

    QString clientID;
    std::string token;

    QPointer<KickChatServer> chatServer_;

    struct SubCompletion {
        std::string channel;
    };
    struct Refresh {
    };
    struct Connect {
    };
    using CompletionData = std::variant<SubCompletion, Refresh, Connect>;

    boost::unordered_flat_set<QString> subscriptionBacklog;
    boost::unordered_flat_map<uint32_t, CompletionData> pendingCompletions;
};

KickCentrifugoClient::KickCentrifugoClient(QString clientID,
                                           QPointer<KickChatServer> chatServer)
    : BasicPubSubClient(500)  // FIXME: Is this limit correct?
    , lastHeartbeat_(std::chrono::steady_clock::now())
    , heartbeatInterval_(MAX_HEARTBEAT_INTERVAL)
    , clientID(std::move(clientID))
    , chatServer_(std::move(chatServer))
{
    this->refreshTimer.setSingleShot(true);
    // NOLINTNEXTLINE(clazy-connect-3arg-lambda)
    QObject::connect(&this->refreshTimer, &QTimer::timeout, [this] {
        this->doRefresh();
    });
}

void KickCentrifugoClient::onOpen()
{
    BasicPubSubClient::onOpen();
    this->lastHeartbeat_ = std::chrono::steady_clock::now();
    this->doRefresh();
}

void KickCentrifugoClient::doRefresh()
{
    if (this->isRefreshing)
    {
        return;
    }
    qCDebug(chatterinoKick) << "[Centrifugo] Refreshing...";
    this->isRefreshing = true;
    NetworkRequest(u"https://web.kick.com/api/v1/realtime/auth/connection"_s,
                   NetworkRequestType::Post)
        .json(QJsonObject{{"client_id"_L1, this->clientID}})
        .onSuccess([weak = this->weak_from_this()](const NetworkResult &res) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            // FIXME: Use QAnyStringView.
            self->token = res.parseJson()
                              .value("data"_L1)["token"_L1]
                              .toString()
                              .toStdString();
            self->sendRefreshOrConnect();
        })
        .onError([weak = this->weak_from_this()](const NetworkResult &res) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            self->isRefreshing = false;
            qCWarning(chatterinoKick)
                << "Failed to get centrifugo token:" << res.formatError();
            if (!self->isConnected)
            {
                self->close();
            }
        })
        .execute();
}

void KickCentrifugoClient::sendRefreshOrConnect()
{
    assert(this->isRefreshing);
    assertInGuiThread();
    this->isRefreshing = false;

    QByteArray data;
    if (this->isConnected)
    {
        auto id = this->nextId++;
        qCDebug(chatterinoKick) << "[Centrifugo] Refresh id:" << id;
        this->pendingCompletions.emplace(id, Refresh{});
        data.append(boost::json::serialize(boost::json::object{
            {
                "refresh",
                boost::json::object{{"token", this->token}},
            },
            {"id", id},
        }));
    }
    else
    {
        auto id = this->nextId++;
        qCDebug(chatterinoKick) << "[Centrifugo] Connect id:" << id;
        this->pendingCompletions.emplace(id, Connect{});
        data.append(boost::json::serialize(boost::json::object{
            {
                "connect",
                boost::json::object{
                    {"token", this->token},
                    {"name", "js"},
                },
            },
            {"id", id},
        }));
        this->isConnected = true;
    }

    auto subs = std::exchange(this->subscriptionBacklog, {});
    for (const auto &sub : subs)
    {
        data.append('\n');
        data.append(this->encodeSubImpl(sub.toStdString()));
    }
    this->sendText(data);
}

void KickCentrifugoClient::subscribeImpl(const Subscription &topic)
{
    assertInGuiThread();
    if (this->isConnected)
    {
        this->sendText(QByteArray::fromStdString(
            this->encodeSubImpl(topic.toStdString())));
    }
    else
    {
        this->subscriptionBacklog.emplace(topic);
    }
}

void KickCentrifugoClient::unsubscribeImpl(const Subscription &topic)
{
    assertInGuiThread();
    if (this->isConnected)
    {
        this->sendText(QByteArray::fromStdString(
            this->encodeUnsubImpl(topic.toStdString())));
    }
    else
    {
        this->subscriptionBacklog.erase(topic);
    }
}

std::string KickCentrifugoClient::encodeSubImpl(std::string channel)
{
    auto id = this->nextId++;
    qCDebug(chatterinoKick) << "[Centrifugo] Sub to" << channel << "id:" << id;
    auto res = boost::json::serialize(boost::json::object{
        {
            "subscribe",
            boost::json::object{
                {"channel", channel},
                // Web enables channel compaction, but Kick doesn't support
                // that and I don't feel the need to implement it.
                {"flag", 0},
            },
        },
        {"id", id},
    });
    this->pendingCompletions.emplace(id, SubCompletion{std::move(channel)});

    return res;
}

std::string KickCentrifugoClient::encodeUnsubImpl(std::string_view channel)
{
    auto id = this->nextId++;
    qCDebug(chatterinoKick)
        << "[Centrifugo] Unsub from " << channel << "id:" << id;

    return boost::json::serialize(boost::json::object{
        {
            "unsubscribe",
            boost::json::object{{"channel", channel}},
        },
        {"id", id},
    });
}

void KickCentrifugoClient::onMessage(const QByteArray &msg)
{
    runInGuiThread([weak = this->weak_from_this(), msg] {
        auto self = weak.lock();
        if (self)
        {
            QByteArrayView ba = msg;
            while (!ba.empty())
            {
                auto [part, rest] = splitOnce(ba, '\n');
                self->onMessageUi(part);
                ba = rest;
            }
        }
    });
}

void KickCentrifugoClient::onMessageUi(QByteArrayView msg)
{
    boost::system::error_code ec;
    auto rootJv =
        boost::json::parse(std::string_view(msg.data(), msg.size()), ec);
    if (ec)
    {
        qCWarning(chatterinoKick)
            << "Failed to parse message:" << ec.message() << msg;
        return;
    }
    BoostJsonValue rootRef(rootJv);
    auto rootObj = rootRef.toObject();
    if (rootObj.empty())
    {
        // Heartbeats are empty objects.
        this->sendText("{}"_ba);
        this->lastHeartbeat_ = std::chrono::steady_clock::now();
        return;
    }

    auto pushEvent = rootObj["push"].toObject();
    if (pushEvent.empty())
    {
        this->onResponseOrUnk(rootObj, msg);
        return;
    }

    auto pubData = pushEvent["pub"]["data"].toObject();
    auto event = pubData["event"].toStringView();

    auto dataStr = pubData["data"].toStringView();
    BoostJsonValue data;
    boost::json::value dataJv;
    if (!dataStr.empty() && dataStr != "{}")
    {
        dataJv = boost::json::parse(dataStr, ec);
        data = BoostJsonValue(dataJv);
    }

    bool isApp = kick::ws::stripPrefix(event, "App\\Events\\");

    auto channel = pushEvent["channel"].toStringView();
    auto ids = kick::ws::parseIDs(channel);
    if (this->chatServer_ && (ids.roomID > 0 || ids.channelID > 0))
    {
        bool handled = this->chatServer_->onAppEvent(ids.roomID, ids.channelID,
                                                     event, data.toObject());
        if (!handled)
        {
            qCWarning(chatterinoKick).noquote()
                << "Unknown event" << event << "isApp:" << isApp
                << "channel:" << channel << "data:" << dataStr;
        }
    }
}

void KickCentrifugoClient::onResponseOrUnk(BoostJsonObject root,
                                           QByteArrayView raw)
{
    auto id = static_cast<uint32_t>(root["id"].toUint64());
    auto it = this->pendingCompletions.find(id);
    if (it == this->pendingCompletions.end())
    {
        qCWarning(chatterinoKick) << "Unexpected centrifugo message:" << raw;
        return;
    }
    auto completion = std::move(it->second);
    this->pendingCompletions.erase(it);

    auto error = [&] -> std::optional<std::string_view> {
        auto o = root["error"];
        if (o.isObject())
        {
            return o["message"].toStringView();
        }
        return std::nullopt;
    }();

    if (error)
    {
        qCWarning(chatterinoKick)
            << "[Centrifugo] id" << id << "errored: " << *error;
        return;
    }

    variant::Overloaded{
        [&](const SubCompletion &sub) {
            // That's the main chat subscription.
            if (sub.channel.starts_with("chatrooms.") &&
                sub.channel.ends_with(".v2"))
            {
                auto ids = kick::ws::parseIDs(sub.channel);
                if (this->chatServer_ && ids.roomID > 0)
                {
                    this->chatServer_->onJoin(ids.roomID);
                }
            }
        },
        [&](Connect) {
            auto o = root["connect"].toObject();
            auto hb = o["ping"].toInt64();
            this->heartbeatInterval_ = std::chrono::seconds(hb);
            qCDebug(chatterinoKick) << "[Centrifugo] Connected.";
            if (o["expires"].toBool())
            {
                std::chrono::seconds ttl(o["ttl"].toInt64(30LL * 60) - 10);
                qCDebug(chatterinoKick) << "[Centrifugo] Using TTL:" << ttl;
                this->refreshTimer.start(ttl);
            }
        },
        [&](Refresh) {
            auto o = root["refresh"].toObject();
            if (o["expires"].toBool())
            {
                std::chrono::seconds ttl(o["ttl"].toInt64(30LL * 60) - 10);
                qCDebug(chatterinoKick) << "[Centrifugo] Using TTL:" << ttl;
                this->refreshTimer.start(ttl);
            }
        },
    }
        .visit(completion);
}

void KickCentrifugoClient::checkHeartbeat()
{
    if (!this->isOpen())
    {
        return;
    }

    if ((std::chrono::steady_clock::now() - this->lastHeartbeat_) >
        this->heartbeatInterval_ * 1.5)
    {
        qCDebug(chatterinoKick) << "Heartbeat timed out";
        this->close();
    }
}

}  // namespace

namespace chatterino {

class KickCentrifugoManagerPrivate
    : public BasicPubSubManager<KickCentrifugoManagerPrivate,
                                KickCentrifugoClient>
{
public:
    KickCentrifugoManagerPrivate(QString url, QString clientID);
    ~KickCentrifugoManagerPrivate() override;

    Q_DISABLE_COPY_MOVE(KickCentrifugoManagerPrivate);

    std::shared_ptr<KickCentrifugoClient> makeClient();
    void checkHeartbeats();

    std::chrono::milliseconds heartbeatInterval = MAX_HEARTBEAT_INTERVAL;
    QTimer heartbeatTimer;
    QString clientID;

private:
    friend KickCentrifugoManager;
};

KickCentrifugoManagerPrivate::KickCentrifugoManagerPrivate(QString url,
                                                           QString clientID)
    : BasicPubSubManager(std::move(url), "kick")
    , clientID(std::move(clientID))
{
    QObject::connect(&this->heartbeatTimer, &QTimer::timeout, this,
                     &KickCentrifugoManagerPrivate::checkHeartbeats);
    this->heartbeatTimer.setInterval(this->heartbeatInterval);
    this->heartbeatTimer.setSingleShot(false);
    this->heartbeatTimer.start();
}

KickCentrifugoManagerPrivate::~KickCentrifugoManagerPrivate()
{
    this->stop();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::shared_ptr<KickCentrifugoClient> KickCentrifugoManagerPrivate::makeClient()
{
    return std::make_shared<KickCentrifugoClient>(
        this->clientID, getApp()->getKickChatServer());
}

void KickCentrifugoManagerPrivate::checkHeartbeats()
{
    auto minInterval = std::chrono::milliseconds::max();
    for (const auto &[id, client] : this->clients())
    {
        client->checkHeartbeat();
        minInterval = std::min(minInterval, client->heartbeatInterval());
    }
    if (minInterval != std::chrono::milliseconds::max())
    {
        this->heartbeatInterval = minInterval;
        this->heartbeatTimer.setInterval(this->heartbeatInterval);
    }
}

KickCentrifugoManager::KickCentrifugoManager(QString url, QString clientID)
    : private_(
          new KickCentrifugoManagerPrivate(std::move(url), std::move(clientID)))
{
}
KickCentrifugoManager::~KickCentrifugoManager() = default;

void KickCentrifugoManager::joinChannel(const QString &name)
{
    this->private_->subscribe(name);
}

void KickCentrifugoManager::partChannel(const QString &name)
{
    this->private_->unsubscribe(name);
}

}  // namespace chatterino
