// SPDX-FileCopyrightText: 2026 Contributors to Chatterino 7TV <https://7tv.app>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/ws/KickPusherManager.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/kick/ws/KickWebSocketCommon.hpp"
#include "providers/liveupdates/BasicPubSubClient.hpp"
#include "providers/liveupdates/BasicPubSubManager.hpp"
#include "util/BoostJsonWrap.hpp"

#include <boost/json.hpp>
#include <QPointer>

#include <utility>

using namespace Qt::Literals;

namespace {

using namespace chatterino;

constexpr std::chrono::seconds MAX_HEARTBEAT_INTERVAL{20};
const QString WS_URL =
    u"wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0&flash=false"_s;

class KickPusherClient : public BasicPubSubClient<QString, KickPusherClient>,
                         public std::enable_shared_from_this<KickPusherClient>
{
public:
    KickPusherClient(QPointer<KickChatServer> chatServer)
        : BasicPubSubClient(100)
        , lastHeartbeat_(std::chrono::steady_clock::now())
        , heartbeatInterval_(MAX_HEARTBEAT_INTERVAL)
        , chatServer_(std::move(chatServer))
    {
    }

    void onOpen() /* override */
    {
        BasicPubSubClient::onOpen();
        this->lastHeartbeat_ = std::chrono::steady_clock::now();
    }

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

    QByteArray encodeSubscription(const Subscription &subscription);
    QByteArray encodeUnsubscription(const Subscription &subscription);

private:
    void onMessageUi(const QByteArray &msg);

    std::chrono::steady_clock::time_point lastHeartbeat_;
    std::chrono::milliseconds heartbeatInterval_;
    QPointer<KickChatServer> chatServer_;
};

void KickPusherClient::onMessage(const QByteArray &msg)
{
    runInGuiThread([weak = this->weak_from_this(), msg] {
        auto self = weak.lock();
        if (self)
        {
            self->onMessageUi(msg);
        }
    });
}

void KickPusherClient::onMessageUi(const QByteArray &msg)
{
    boost::system::error_code ec;
    auto rootJv =
        boost::json::parse(std::string_view(msg.data(), msg.size()), ec);
    if (ec)
    {
        qCWarning(chatterinoKick) << "Failed to parse message:" << ec.message();
        return;
    }
    BoostJsonValue rootRef(rootJv);
    auto rootObj = rootRef.toObject();
    auto event = rootObj["event"].toStringView();

    auto dataStr = rootObj["data"].toStringView();
    BoostJsonValue data;
    boost::json::value dataJv;
    if (!dataStr.empty() && dataStr != "{}")
    {
        dataJv = boost::json::parse(dataStr, ec);
        data = BoostJsonValue(dataJv);
    }

    if (event == "pusher:pong")
    {
        this->lastHeartbeat_ = std::chrono::steady_clock::now();
    }
    else if (event == "pusher_internal:subscription_succeeded")
    {
        auto channel = rootObj["channel"].toStdString();
        // that's the main chat subscription
        if (channel.starts_with("chatrooms.") && channel.ends_with(".v2"))
        {
            auto ids = kick::ws::parseIDs(channel);
            if (this->chatServer_ && ids.roomID > 0)
            {
                this->chatServer_->onJoin(ids.roomID);
            }
        }
    }
    else if (event == "pusher:subscription_error")
    {
        qCWarning(chatterinoKick) << "Failed to subscribe" << msg;
    }
    else if (event == "pusher:connection_established")
    {
        std::chrono::seconds activityTimeout{
            data["activity_timeout"].toInt64()};
        if (activityTimeout.count() > 2 &&
            activityTimeout < this->heartbeatInterval_)
        {
            this->heartbeatInterval_ = activityTimeout;
        }
    }
    else
    {
        bool isApp = kick::ws::stripPrefix(event, "App\\Events\\");

        auto channel = rootObj["channel"].toStringView();
        auto ids = kick::ws::parseIDs(channel);
        if (this->chatServer_ && (ids.roomID > 0 || ids.channelID > 0))
        {
            bool handled = this->chatServer_->onAppEvent(
                ids.roomID, ids.channelID, event, data.toObject());
            if (!handled)
            {
                qCWarning(chatterinoKick).noquote()
                    << "Unknown event" << event << "isApp:" << isApp
                    << "channel:" << rootObj["channel"].toStringView()
                    << "data:" << dataStr;
            }
        }
    }
}

void KickPusherClient::checkHeartbeat()
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

    this->sendText(R"({"event":"pusher:ping","data":0})"_ba);
}

// NOLINTBEGIN(readability-convert-member-functions-to-static)
QByteArray KickPusherClient::encodeSubscription(const Subscription &sub)
{
    return QByteArray::fromStdString(boost::json::serialize(boost::json::object{
        {"event", "pusher:subscribe"},
        {"data",
         boost::json::object{
             {"auth", ""},
             {"channel", sub.toStdString()},
         }},
    }));
}

QByteArray KickPusherClient::encodeUnsubscription(const Subscription &sub)
{
    return QByteArray::fromStdString(boost::json::serialize(boost::json::object{
        {"event", "pusher:unsubscribe"},
        {"data",
         boost::json::object{
             {"channel", sub.toStdString()},
         }},
    }));
}
// NOLINTEND(readability-convert-member-functions-to-static)

QString makeUrl(QStringView appKey, QStringView cluster)
{
    const auto isSimple = [](QStringView s) {
        return !s.empty() && std::ranges::all_of(s, [](QChar c) {
            return c == u'_' || c == u'-' || c == u'+' || c.isLetterOrNumber();
        });
    };

    if (!isSimple(appKey) || !isSimple(cluster))
    {
        qCWarning(chatterinoKick)
            << "Unexpected appKey:" << appKey << "cluster:" << cluster;
        return WS_URL;
    }

    return u"wss://ws-" % cluster % u".pusher.com/app/" % appKey %
           u"?protocol=7&client=js&version=8.4.0&flash=false";
}

}  // namespace

namespace chatterino {

class KickPusherManagerPrivate
    : public BasicPubSubManager<KickPusherManagerPrivate, KickPusherClient>
{
public:
    KickPusherManagerPrivate(QStringView appKey, QStringView cluster);
    ~KickPusherManagerPrivate() override;

    Q_DISABLE_COPY_MOVE(KickPusherManagerPrivate);

    std::shared_ptr<KickPusherClient> makeClient();
    void checkHeartbeats();

    std::chrono::milliseconds heartbeatInterval = MAX_HEARTBEAT_INTERVAL;
    QTimer heartbeatTimer;

private:
    friend KickPusherManager;
};

KickPusherManagerPrivate::KickPusherManagerPrivate(QStringView appKey,
                                                   QStringView cluster)
    : BasicPubSubManager(makeUrl(appKey, cluster), "kick")
{
    QObject::connect(&this->heartbeatTimer, &QTimer::timeout, this,
                     &KickPusherManagerPrivate::checkHeartbeats);
    this->heartbeatTimer.setInterval(this->heartbeatInterval);
    this->heartbeatTimer.setSingleShot(false);
    this->heartbeatTimer.start();
}

KickPusherManagerPrivate::~KickPusherManagerPrivate()
{
    this->stop();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::shared_ptr<KickPusherClient> KickPusherManagerPrivate::makeClient()
{
    return std::make_shared<KickPusherClient>(getApp()->getKickChatServer());
}

void KickPusherManagerPrivate::checkHeartbeats()
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

KickPusherManager::KickPusherManager(QStringView appKey, QStringView cluster)
    : private_(new KickPusherManagerPrivate(appKey, cluster))
{
}
KickPusherManager::~KickPusherManager() = default;

void KickPusherManager::joinChannel(const QString &name)
{
    this->private_->subscribe(name);
}

void KickPusherManager::partChannel(const QString &name)
{
    this->private_->unsubscribe(name);
}

}  // namespace chatterino
