#include "providers/twitch/TwitchReadConnectionPool.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/irc/IrcConnection2.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/WindowManager.hpp"
#include "util/QStringHash.hpp"  // IWYU pragma: keep
#include "util/RatelimitBucket.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

#include <span>

namespace {

using namespace chatterino;

// Ratelimits for joinBucket_
constexpr int JOIN_RATELIMIT_BUDGET = 18;
constexpr int JOIN_RATELIMIT_COOLDOWN = 12500;

constexpr size_t CHANNELS_PER_CONNECTION = 18;

void appendConnectedMessage(std::span<const ChannelPtr> channels)
{
    // connected/disconnected message
    auto connectedMsg = makeSystemMessage("connected");
    connectedMsg->flags.set(MessageFlag::ConnectedMessage);
    auto reconnected = makeSystemMessage("reconnected");
    reconnected->flags.set(MessageFlag::ConnectedMessage);

    for (const auto &chan : channels)
    {
        MessagePtr last = chan->getLastMessage();

        bool replaceMessage =
            last && last->flags.has(MessageFlag::DisconnectedMessage);

        if (replaceMessage)
        {
            chan->replaceMessage(last, reconnected);
        }
        else
        {
            chan->addMessage(connectedMsg, MessageContext::Original);
        }
    }
}

}  // namespace

namespace chatterino {

TwitchReadConnectionPool::TwitchReadConnectionPool(TwitchIrcServer *parent)
    : QObject(parent)
{
}

// MARK: Single

/// Manages a single read connection.
///
/// When too many channels are joined, the IRC `JOIN`s are delayed.
class TwitchReadConnectionPoolSingle final : public TwitchReadConnectionPool
{
public:
    TwitchReadConnectionPoolSingle(TwitchIrcServer *parent);

    void onChannelCreated(TwitchChannel &channel) override;
    void reconnect() override;
    void close() override;

private:
    void onMessageReceived(Communi::IrcMessage *msg);
    void onConnected();
    void onDisconnected();

    void markChannelsConnected();

    QObjectPtr<IrcConnection> connection = nullptr;

    // Our rate limiting bucket for the Twitch join rate limits
    // https://dev.twitch.tv/docs/irc/guide#rate-limits
    QObjectPtr<RatelimitBucket> joinBucket;

    pajlada::Signals::SignalHolder signalHolder;
    boost::unordered_flat_map<QString, std::weak_ptr<TwitchChannel>> channels;

    TwitchIrcServer *server;
};

TwitchReadConnectionPoolSingle::TwitchReadConnectionPoolSingle(
    TwitchIrcServer *parent)
    : TwitchReadConnectionPool(parent)
    , connection(new IrcConnection)
    , server(parent)
{
    assert(this->server);

    // Apply a leaky bucket rate limiting to JOIN messages
    auto actuallyJoin = [&](const QString &name) {
        if (!this->channels.contains(name))
        {
            return;
        }
        qCDebug(chatterinoIrc) << "Joining" << name;
        this->connection->sendRaw("JOIN #" + name);
    };
    this->joinBucket.reset(new RatelimitBucket(
        JOIN_RATELIMIT_BUDGET, JOIN_RATELIMIT_COOLDOWN, actuallyJoin, this));

    // Listen to read connection message signals
    this->connection->moveToThread(QCoreApplication::instance()->thread());

    QObject::connect(this->connection.get(), &IrcConnection::messageReceived,
                     this, &TwitchReadConnectionPoolSingle::onMessageReceived);
    QObject::connect(this->connection.get(),
                     &Communi::IrcConnection::privateMessageReceived, this,
                     [this](auto *msg) {
                         this->privateMessageReceived.invoke(msg);
                     });
    QObject::connect(this->connection.get(), &Communi::IrcConnection::connected,
                     this, &TwitchReadConnectionPoolSingle::onConnected);
    QObject::connect(this->connection.get(),
                     &Communi::IrcConnection::disconnected, this,
                     &TwitchReadConnectionPoolSingle::onDisconnected);
    this->signalHolder.managedConnect(
        this->connection->connectionLost, [this](bool timeout) {
            qCDebug(chatterinoIrc)
                << "Read connection reconnect requested. Timeout:" << timeout;
            if (timeout)
            {
                // Show additional message since this is going to interrupt a
                // connection that is still "connected"
                this->server->addGlobalSystemMessage(
                    "Server connection timed out, reconnecting");
            }
            this->connection->smartReconnect();
        });
    this->signalHolder.managedConnect(this->connection->heartbeat, [this] {
        this->markChannelsConnected();
    });
}

void TwitchReadConnectionPoolSingle::onChannelCreated(TwitchChannel &channel)
{
    this->channels.emplace(channel.getName(), channel.weakFromThis());
    if (this->connection->isConnected())
    {
        this->joinBucket->send(channel.getName());
    }
    this->signalHolder.managedConnect(
        channel.destroyed, [this, name = channel.getName()] {
            this->channels.erase(name);
            qCDebug(chatterinoIrc) << "Parting" << name;
            this->connection->sendRaw("PART #" + name);
        });
}

void TwitchReadConnectionPoolSingle::reconnect()
{
    this->connection->close();
    TwitchIrcServer::initializeConnection(
        this->connection.get(), TwitchIrcServer::ConnectionType::Read);
}

void TwitchReadConnectionPoolSingle::close()
{
    this->connection->close();
}

void TwitchReadConnectionPoolSingle::onMessageReceived(Communi::IrcMessage *msg)
{
    if (msg->type() == Communi::IrcMessage::Type::Private)
    {
        // We already have a handler for private messages
        return;
    }

    const QString &command = msg->command();

    if (command == "RECONNECT")
    {
        this->server->addGlobalSystemMessage(
            "Twitch Servers requested us to reconnect, reconnecting");
        this->markChannelsConnected();
        this->reconnect();
        return;
    }

    this->messageReceived.invoke(msg);
}

void TwitchReadConnectionPoolSingle::onConnected()
{
    std::vector<ChannelPtr> activeChannels;
    activeChannels.reserve(this->channels.size());
    for (const auto &[name, weak] : this->channels)
    {
        if (auto channel = weak.lock())
        {
            activeChannels.push_back(channel);
        }
    }

    // put the visible channels first
    auto visible = getApp()->getWindows()->getVisibleChannelNames();

    std::ranges::stable_partition(activeChannels, [&](const auto &chan) {
        return visible.contains(chan->getName());
    });

    // join channels
    for (const auto &channel : activeChannels)
    {
        this->joinBucket->send(channel->getName());
    }

    appendConnectedMessage(activeChannels);
}

void TwitchReadConnectionPoolSingle::onDisconnected()
{
    MessageBuilder b(systemMessage, "disconnected");
    b->flags.set(MessageFlag::DisconnectedMessage);
    auto disconnectedMsg = b.release();

    for (const auto &[name, weak] : this->channels)
    {
        auto chan = weak.lock();
        if (!chan)
        {
            continue;
        }

        chan->addMessage(disconnectedMsg, MessageContext::Original);
        chan->markDisconnected();
    }
}

void TwitchReadConnectionPoolSingle::markChannelsConnected()
{
    for (const auto &[name, weak] : this->channels)
    {
        if (auto c = weak.lock())
        {
            c->markConnected();
        }
    }
}

// MARK: Multi

/// Join channels on multiple connections at once.
///
/// Twitch imposes a limit of 20 joins per 10s per authenticated user or
/// connection if anonymous. By using multiple connections, we can join channels
/// faster. This significantly speeds up the launch.
///
/// The join rate limits on Twitch are based on the user. Because of that,
/// usins multiple connections requires them to use the anonymous user.
class TwitchReadConnectionPoolMulti final : public TwitchReadConnectionPool
{
public:
    TwitchReadConnectionPoolMulti(TwitchIrcServer *parent);

    void onChannelCreated(TwitchChannel &channel) override;
    void reconnect() override;
    void close() override;

private:
    struct Connection {
        Connection(size_t id, TwitchReadConnectionPoolMulti *parent);

        void reconnect();
        void close();

        void onMessageReceived(Communi::IrcMessage *msg);
        void onConnected();
        void onDisconnected();
        void markChannelsConnected();

        size_t id = 0;
        IrcConnection connection;
        /// Channel name -> Channel
        boost::unordered_flat_map<QString, std::weak_ptr<TwitchChannel>>
            channels;
        pajlada::Signals::ScopedConnection heartbeatConnection;
        pajlada::Signals::ScopedConnection connectionLostConnection;
        TwitchReadConnectionPoolMulti *parent = nullptr;
    };

    /// Connection ID -> Connection
    boost::unordered_flat_map<size_t, std::unique_ptr<Connection>> connections;
    size_t nextID = 1;
    std::pair<Connection *, bool> getOrCreate();

    /// Channel name -> Connection ID
    boost::unordered_flat_map<QString, size_t> channels;

    pajlada::Signals::SignalHolder signalHolder;

    /// Have we ever been asked to connect? If not, we shouldn't open
    /// connections on our own.
    bool everConnected = false;

    friend Connection;
};

TwitchReadConnectionPoolMulti::TwitchReadConnectionPoolMulti(
    TwitchIrcServer *parent)
    : TwitchReadConnectionPool(parent)
{
}

void TwitchReadConnectionPoolMulti::onChannelCreated(TwitchChannel &channel)
{
    auto [conn, created] = this->getOrCreate();
    this->channels.emplace(channel.getName(), conn->id);
    conn->channels.emplace(channel.getName(), channel.weakFromThis());
    if (conn->connection.isConnected())
    {
        qCDebug(chatterinoIrc) << "Joining" << channel.getName();
        conn->connection.sendRaw("JOIN #" + channel.getName());
    }
    this->signalHolder.managedConnect(
        channel.destroyed, [this, id = conn->id, name = channel.getName()] {
            this->channels.erase(name);
            auto it = this->connections.find(id);
            if (it == this->connections.end())
            {
                return;
            }
            qCDebug(chatterinoIrc) << "Parting" << name;
            it->second->connection.sendRaw("PART #" + name);
            it->second->channels.erase(name);
            if (it->second->channels.empty())
            {
                qCDebug(chatterinoIrc)
                    << "Connection" << id << "has no more channels, removing";
                this->connections.erase(it);
            }
        });

    if (created && this->everConnected)
    {
        conn->reconnect();
    }
}

void TwitchReadConnectionPoolMulti::reconnect()
{
    for (const auto &[id, conn] : this->connections)
    {
        conn->reconnect();
    }
}

void TwitchReadConnectionPoolMulti::close()
{
    for (const auto &[id, conn] : this->connections)
    {
        conn->close();
    }
}

std::pair<TwitchReadConnectionPoolMulti::Connection *, bool>
    TwitchReadConnectionPoolMulti::getOrCreate()
{
    // This is slow if we have many connections, but realistically, users have <5.
    for (const auto &[id, conn] : this->connections)
    {
        if (conn->channels.size() < CHANNELS_PER_CONNECTION)
        {
            return {conn.get(), false};
        }
    }

    size_t id = this->nextID++;
    qCDebug(chatterinoIrc) << "Creating new connection" << id;
    auto [it, inserted] =
        this->connections.emplace(id, new Connection(id, this));
    assert(inserted);
    return {it->second.get(), true};
}

TwitchReadConnectionPoolMulti::Connection::Connection(
    size_t id, TwitchReadConnectionPoolMulti *parent)
    : id(id)
    , parent(parent)
{
    this->connectionLostConnection =
        this->connection.connectionLost.connect([this](bool timeout) {
            qCDebug(chatterinoIrc)
                << "Read connection" << this->id
                << "reconnect requested. Timeout:" << timeout;
            if (timeout)
            {
                // Show additional message since this is going to interrupt a
                // connection that is still "connected"
                for (const auto &[name, weak] : this->channels)
                {
                    if (auto chan = weak.lock())
                    {
                        chan->addSystemMessage(
                            "Server connection timed out, reconnecting");
                    }
                }
            }
            this->connection.smartReconnect();
        });
    this->heartbeatConnection = this->connection.heartbeat.connect([this] {
        this->markChannelsConnected();
    });

    // Listen to read connection message signals
    this->connection.moveToThread(QCoreApplication::instance()->thread());

    QObject::connect(&this->connection, &IrcConnection::messageReceived,
                     &this->connection, [this](auto *msg) {
                         this->onMessageReceived(msg);
                     });
    QObject::connect(&this->connection,
                     &Communi::IrcConnection::privateMessageReceived,
                     &this->connection, [this](auto *msg) {
                         this->parent->privateMessageReceived.invoke(msg);
                     });
    QObject::connect(&this->connection, &Communi::IrcConnection::connected,
                     &this->connection, [this] {
                         this->onConnected();
                     });
    QObject::connect(&this->connection, &Communi::IrcConnection::disconnected,
                     &this->connection, [this] {
                         this->onConnected();
                     });
}

void TwitchReadConnectionPoolMulti::Connection::onMessageReceived(
    Communi::IrcMessage *msg)
{
    if (msg->type() == Communi::IrcMessage::Type::Private)
    {
        // We already have a handler for private messages
        return;
    }

    const QString &command = msg->command();

    if (command == "RECONNECT")
    {
        for (const auto &[name, weak] : this->channels)
        {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    "Twitch Servers requested us to reconnect, reconnecting");
            }
        }
        this->markChannelsConnected();
        this->reconnect();
        return;
    }

    this->parent->messageReceived.invoke(msg);
}

void TwitchReadConnectionPoolMulti::Connection::reconnect()
{
    this->connection.close();
    TwitchIrcServer::initializeConnection(
        &this->connection, TwitchIrcServer::ConnectionType::Read);
}

void TwitchReadConnectionPoolMulti::Connection::close()
{
    this->connection.close();
}

void TwitchReadConnectionPoolMulti::Connection::onConnected()
{
    std::vector<ChannelPtr> activeChannels;
    activeChannels.reserve(this->channels.size());
    for (const auto &[name, weak] : this->channels)
    {
        if (auto channel = weak.lock())
        {
            activeChannels.push_back(channel);
        }
    }

    // join channels
    for (const auto &channel : activeChannels)
    {
        qCDebug(chatterinoIrc) << "Joining" << channel->getName();
        this->connection.sendRaw("JOIN #" + channel->getName());
    }

    appendConnectedMessage(activeChannels);
}

void TwitchReadConnectionPoolMulti::Connection::onDisconnected()
{
    MessageBuilder b(systemMessage, "disconnected");
    b->flags.set(MessageFlag::DisconnectedMessage);
    auto disconnectedMsg = b.release();

    for (const auto &[name, weak] : this->channels)
    {
        auto chan = weak.lock();
        if (!chan)
        {
            continue;
        }

        chan->addMessage(disconnectedMsg, MessageContext::Original);
        chan->markDisconnected();
    }
}

void TwitchReadConnectionPoolMulti::Connection::markChannelsConnected()
{
    for (const auto &[name, weak] : this->channels)
    {
        if (auto chan = weak.lock())
        {
            chan->markConnected();
        }
    }
}

// MARK: Selection

TwitchReadConnectionPool *createTwitchConnectionPool(TwitchIrcServer *parent,
                                                     bool multi)
{
    if (multi)
    {
        return new TwitchReadConnectionPoolMulti(parent);
    }
    return new TwitchReadConnectionPoolSingle(parent);
}

}  // namespace chatterino
