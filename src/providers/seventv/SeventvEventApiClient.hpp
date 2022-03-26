#pragma once

#include <rapidjson/document.h>
#include <QString>
#include <pajlada/signals/signal.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/extensions/permessage_deflate/disabled.hpp>
#include <websocketpp/logger/basic.hpp>
#include "providers/twitch/PubsubClient.hpp"

namespace chatterino {

#define MAX_EVENTAPI_CHANNELS 100

struct eventapi_ws_config : public websocketpp::config::asio_tls_client {
    typedef websocketpp::log::chatterinowebsocketpplogger<
        concurrency_type, websocketpp::log::elevel>
        elog_type;
    typedef websocketpp::log::chatterinowebsocketpplogger<
        concurrency_type, websocketpp::log::alevel>
        alog_type;

    struct permessage_deflate_config {
    };

    typedef websocketpp::extensions::permessage_deflate::disabled<
        permessage_deflate_config>
        permessage_deflate_type;
};

using WsClient = websocketpp::client<eventapi_ws_config>;
using WsHandle = websocketpp::connection_hdl;
using WsErrorCode = websocketpp::lib::error_code;

namespace eventapi {
    struct Listener {
        QString channel;
        bool confirmed = false;
    };

    class EventApiClient : public std::enable_shared_from_this<EventApiClient>
    {
    public:
        EventApiClient(WsClient &_websocketClient, WsHandle _handle);

        void start();
        void stop();

        bool join(const QString &channel);
        void part(const QString &channel);

        bool isJoinedChannel(const QString &channel);

        std::vector<Listener> channels;

    private:
        bool send(const char *payload);

        WsClient &websocketClient_;
        WsHandle handle_;

        std::atomic<bool> started_{false};
    };
}  // namespace eventapi

struct UpdateSeventvEmoteAction {
    QString channelName;
    QString actor;
    QString emoteBaseName;
    QJsonObject emoteJson;
};

struct AddSeventvEmoteAction {
    QString channelName;
    QString actor;
    QJsonObject emoteJson;
};
struct RemoveSeventvEmoteAction {
    QString channelName;
    QString actor;
    QString emoteName;
};

class SeventvEventApi
{
    using WsMessagePtr =
        websocketpp::config::asio_tls_client::message_type::ptr;
    using WsContextPtr =
        websocketpp::lib::shared_ptr<boost::asio::ssl::context>;

    template <typename T>
    using Signal =
        pajlada::Signals::Signal<T>;  // type-id is vector<T, Alloc<T>>

    WsClient websocketClient;
    std::unique_ptr<std::thread> mainThread;

public:
    SeventvEventApi();

    ~SeventvEventApi() = delete;

    enum class State {
        Connected,
        Disconnected,
    };

    void start();

    bool isConnected() const
    {
        return this->state == State::Connected;
    }

    pajlada::Signals::NoArgSignal connected;

    struct {
        Signal<AddSeventvEmoteAction> emoteAdded;
        Signal<UpdateSeventvEmoteAction> emoteUpdated;
        Signal<RemoveSeventvEmoteAction> emoteRemoved;
    } signals_;

    void joinChannel(const QString &channelName);
    void partChannel(const QString &channelName);

    std::vector<std::unique_ptr<QString>> pendingChannels;

private:
    State state = State::Connected;
    void addClient();
    std::atomic<bool> addingClient{false};
    std::map<WsHandle, std::shared_ptr<eventapi::EventApiClient>,
             std::owner_less<WsHandle>>
        clients;

    bool tryJoinChannel(const QString &channelName);

    void onMessage(websocketpp::connection_hdl hdl, WsMessagePtr msg);
    void onConnectionOpen(websocketpp::connection_hdl hdl);
    void onConnectionClose(websocketpp::connection_hdl hdl);
    WsContextPtr onTLSInit(websocketpp::connection_hdl hdl);

    void runThread();
};
}  // namespace chatterino