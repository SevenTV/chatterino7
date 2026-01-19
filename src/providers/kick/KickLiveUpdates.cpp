#include "providers/kick/KickLiveUpdates.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/liveupdates/BasicPubSubClient.hpp"
#include "providers/liveupdates/BasicPubSubManager.hpp"
#include "util/BoostJsonWrap.hpp"

#include <boost/json.hpp>
#include <QPointer>

#include <charconv>
#include <utility>

using namespace Qt::Literals;

namespace {

constexpr std::chrono::seconds MAX_HEARTBEAT_INTERVAL{20};
const QString WS_URL =
    u"wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0&flash=false"_s;

bool stripPrefix(std::string_view &str, std::string_view prefix)
{
    if (str.starts_with(prefix))
    {
        str = str.substr(prefix.size());
        return true;
    }
    return false;
}

bool stripSuffix(std::string_view &str, std::string_view suffix)
{
    if (str.ends_with(suffix))
    {
        str = str.substr(0, str.size() - suffix.size());
        return true;
    }
    return false;
}

struct IDs {
    uint64_t roomID = 0;
    uint64_t channelID = 0;
};

IDs parseIDs(std::string_view channel)
{
    bool isChannel = false;
    if (stripPrefix(channel, "chatrooms.") || stripPrefix(channel, "chatroom_"))
    {
        stripSuffix(channel, ".v2");
    }
    else if (stripPrefix(channel, "channel_") ||
             stripPrefix(channel, "channel.") ||
             stripPrefix(channel, "predictions-channel-"))
    {
        isChannel = true;
    }

    uint64_t v = 0;
    std::from_chars(channel.data(), channel.data() + channel.size(), v);

    if (isChannel)
    {
        return IDs{.channelID = v};
    }
    return IDs{.roomID = v};
}

}  // namespace

namespace chatterino {

class KickLiveUpdatesClient
    : public BasicPubSubClient<QString, KickLiveUpdatesClient>,
      public std::enable_shared_from_this<KickLiveUpdatesClient>
{
public:
    KickLiveUpdatesClient(QPointer<KickChatServer> chatServer)
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

void KickLiveUpdatesClient::onMessage(const QByteArray &msg)
{
    runInGuiThread([weak = this->weak_from_this(), msg] {
        auto self = weak.lock();
        if (self)
        {
            self->onMessageUi(msg);
        }
    });
}

void KickLiveUpdatesClient::onMessageUi(const QByteArray &msg)
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
            auto ids = parseIDs(channel);
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
        bool isApp = stripPrefix(event, "App\\Events\\");

        auto channel = rootObj["channel"].toStringView();
        auto ids = parseIDs(channel);
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

void KickLiveUpdatesClient::checkHeartbeat()
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
QByteArray KickLiveUpdatesClient::encodeSubscription(const Subscription &sub)
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

QByteArray KickLiveUpdatesClient::encodeUnsubscription(const Subscription &sub)
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

class KickLiveUpdatesPrivate
    : public BasicPubSubManager<KickLiveUpdatesPrivate, KickLiveUpdatesClient>
{
public:
    KickLiveUpdatesPrivate();
    ~KickLiveUpdatesPrivate() override;

    Q_DISABLE_COPY_MOVE(KickLiveUpdatesPrivate);

    std::shared_ptr<KickLiveUpdatesClient> makeClient();
    void checkHeartbeats();

    std::chrono::milliseconds heartbeatInterval = MAX_HEARTBEAT_INTERVAL;
    QTimer heartbeatTimer;

private:
    friend KickLiveUpdates;
};

KickLiveUpdatesPrivate::KickLiveUpdatesPrivate()
    : BasicPubSubManager(WS_URL, "kick")
{
    QObject::connect(&this->heartbeatTimer, &QTimer::timeout, this,
                     &KickLiveUpdatesPrivate::checkHeartbeats);
    this->heartbeatTimer.setInterval(this->heartbeatInterval);
    this->heartbeatTimer.setSingleShot(false);
    this->heartbeatTimer.start();
}

KickLiveUpdatesPrivate::~KickLiveUpdatesPrivate()
{
    this->stop();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::shared_ptr<KickLiveUpdatesClient> KickLiveUpdatesPrivate::makeClient()
{
    return std::make_shared<KickLiveUpdatesClient>(
        getApp()->getKickChatServer());
}

void KickLiveUpdatesPrivate::checkHeartbeats()
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

KickLiveUpdates::KickLiveUpdates()
    : private_(new KickLiveUpdatesPrivate)
{
}
KickLiveUpdates::~KickLiveUpdates() = default;

void KickLiveUpdates::joinRoom(uint64_t roomID, uint64_t channelID)
{
    this->private_->subscribe(u"chatroom_" % QString::number(roomID));
    this->private_->subscribe(u"chatrooms." % QString::number(roomID));
    this->private_->subscribe(u"chatrooms." % QString::number(roomID) % u".v2");
    this->private_->subscribe(u"channel." % QString::number(channelID));
    this->private_->subscribe(u"channel_" % QString::number(channelID));
    this->private_->subscribe(u"predictions-channel-" %
                              QString::number(channelID));
}

void KickLiveUpdates::leaveRoom(uint64_t roomID, uint64_t channelID)
{
    this->private_->unsubscribe(u"chatroom_" % QString::number(roomID));
    this->private_->unsubscribe(u"chatrooms." % QString::number(roomID));
    this->private_->unsubscribe(u"chatrooms." % QString::number(roomID) %
                                u".v2");
    this->private_->unsubscribe(u"channel." % QString::number(channelID));
    this->private_->unsubscribe(u"channel_" % QString::number(channelID));
    this->private_->unsubscribe(u"predictions-channel-" %
                                QString::number(channelID));
}

}  // namespace chatterino
