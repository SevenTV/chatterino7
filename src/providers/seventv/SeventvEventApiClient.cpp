#include "SeventvEventApiClient.hpp"

#include "singletons/Settings.hpp"
#include "util/DebugCount.hpp"
#include "util/Helpers.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <rapidjson/error/en.h>

#include <exception>
#include <iostream>
#include <thread>
#include <utility>
#include "common/QLogging.hpp"

#define SEVENTV_EVENTAPI_URL "wss://events.7tv.app/v1/channel-emotes"

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

namespace chatterino {
namespace eventapi {
    EventApiClient::EventApiClient(WsClient &_websocketClient, WsHandle _handle)
        : websocketClient_(_websocketClient)
        , handle_(_handle)
    {
    }
    void EventApiClient::start()
    {
        assert(!this->started_);
        this->started_ = true;
    }
    void EventApiClient::stop()
    {
        assert(this->started_);
        this->started_ = false;
    }

    bool EventApiClient::join(const QString &channel)
    {
        if (this->channels.size() >= MAX_EVENTAPI_CHANNELS)
        {
            return false;
        }
        this->channels.emplace_back(Listener{channel, false});
        rapidjson::Document doc(rapidjson::kObjectType);
        rj::set(doc, "action", "join");
        rj::set(doc, "payload", channel, doc.GetAllocator());

        this->send(rj::stringify(doc).toUtf8());

        return true;
    }
    void EventApiClient::part(const QString &channel)
    {
        bool found = false;
        for (auto it = this->channels.begin(); it != this->channels.end(); it++)
        {
            if (it->channel == channel)
            {
                this->channels.erase(it);
                found = true;
                break;
            }
        }
        if (!found)
        {
            return;
        }

        rapidjson::Document doc(rapidjson::kObjectType);
        rj::set(doc, "action", "part");
        rj::set(doc, "payload", channel, doc.GetAllocator());

        this->send(rj::stringify(doc).toUtf8());
    }

    bool EventApiClient::isJoinedChannel(const QString &channel)
    {
        for (const auto &listener : this->channels)
        {
            if (listener.channel == channel)
            {
                return true;
            }
        }
        return false;
    }
    bool EventApiClient::send(const char *payload)
    {
        WsErrorCode ec;
        this->websocketClient_.send(this->handle_, payload,
                                    websocketpp::frame::opcode::text, ec);

        if (ec)
        {
            qCDebug(chatterinoSeventvEventApi)
                << "Error sending message" << payload << ":"
                << ec.message().c_str();
            // same todo as in pubsub client
            return false;
        }

        return true;
    }
}  // namespace eventapi
SeventvEventApi::SeventvEventApi()
{
    qCDebug(chatterinoSeventvEventApi) << "started 7TV event api";

    this->websocketClient.set_access_channels(websocketpp::log::alevel::all);
    this->websocketClient.clear_access_channels(
        websocketpp::log::alevel::frame_header |
        websocketpp::log::alevel::frame_payload);
    this->websocketClient.init_asio();
    this->websocketClient.set_tls_init_handler(
        bind(&SeventvEventApi::onTLSInit, this, ::_1));
    this->websocketClient.set_message_handler(
        bind(&SeventvEventApi::onMessage, this, ::_1, ::_2));
    this->websocketClient.set_open_handler(
        bind(&SeventvEventApi::onConnectionOpen, this, ::_1));
    this->websocketClient.set_close_handler(
        bind(&SeventvEventApi::onConnectionClose, this, ::_1));
    this->addClient();
}
void SeventvEventApi::addClient()
{
    if (this->addingClient)
    {
        return;
    }
    this->addingClient = true;

    websocketpp::lib::error_code ec;
    auto con = this->websocketClient.get_connection(SEVENTV_EVENTAPI_URL, ec);

    if (ec)
    {
        qCDebug(chatterinoSeventvEventApi)
            << "Unable to establish connection:" << ec.message().c_str();
        return;
    }

    this->websocketClient.connect(con);
}
void SeventvEventApi::start()
{
    this->mainThread.reset(
        new std::thread(std::bind(&SeventvEventApi::runThread, this)));
}
void SeventvEventApi::joinChannel(const QString &channelName)
{
    if (this->tryJoinChannel(channelName))
    {
        return;
    }

    this->addClient();

    this->pendingChannels.emplace_back(std::make_unique<QString>(channelName));
}
bool SeventvEventApi::tryJoinChannel(const QString &channelName)
{
    for (const auto &p : this->clients)
    {
        const auto &client = p.second;
        if (client->join(channelName))
        {
            return true;
        }
    }

    return false;
}
void SeventvEventApi::partChannel(const QString &channelName)
{
    for (const auto &p : this->clients)
    {
        const auto &client = p.second;
        if (client->isJoinedChannel(channelName))
        {
            client->part(channelName);
            break;
        }
    }
}

void SeventvEventApi::onMessage(websocketpp::connection_hdl hdl,
                                WsMessagePtr wsMsg)
{
    const auto &rawPayload = QByteArray::fromStdString(wsMsg->get_payload());
    QJsonParseError error;
    QJsonDocument msg(QJsonDocument::fromJson(rawPayload, &error));

    if (error.error != QJsonParseError::NoError)
    {
        qCDebug(chatterinoSeventvEventApi)
            << QString("Error parsing message from EventApi: %1")
                   .arg(error.errorString());
        return;
    }
    if (!msg.isObject())
    {
        qCDebug(chatterinoSeventvEventApi)
            << QString("Error parsing message form EventApi, Root object isn't "
                       "an object: '%1'")
                   .arg(QString::fromUtf8(rawPayload));
        return;
    }
    QJsonObject obj = msg.object();
    if (!obj.contains("action") || !obj.value("action").isString())
    {
        qCDebug(chatterinoSeventvEventApi)
            << QString("Error parsing message form EventApi, got payload "
                       "without or invalid action: '%1'")
                   .arg(QString::fromUtf8(rawPayload));
        return;
    }
    QString action = obj.value("action").toString();
    if (action != "update" || !obj.contains("payload") ||
        !obj.value("payload").isString())
    {
        return;  // todo
    }
    QJsonDocument updateDoc(QJsonDocument::fromJson(
        obj.value("payload").toString().toUtf8(), &error));
    if (!updateDoc.isObject())
    {
        qCDebug(chatterinoSeventvEventApi)
            << QString("Error parsing update form EventApi, update isn't "
                       "an object: '%1'")
                   .arg(obj.value("payload").toString());
        return;
    }
    QJsonObject update = updateDoc.object();
    if (!update.contains("action") || !update.contains("channel"))
    {
        return;  // todo print
    }

    if (update.value("action").toString() == "REMOVE")
    {
        std::pair<QString, QString> pair =
            std::make_pair(update.value("channel").toString(),
                           update.value("emote_id").toString());
        this->signals_.emoteRemoved.invoke(pair);
    }
    else
    {
        if (!update.contains("emote") || !update.value("emote").isObject())
        {
            return;  // todo print
        }
        QJsonObject emote = update.value("emote").toObject();
        emote.insert("id", update.value("emote_id"));
        std::pair<QString, QJsonValue> pair =
            std::make_pair(update.value("channel").toString(), emote);
        this->signals_.emoteAddedOrUpdated.invoke(pair);
    }
}

void SeventvEventApi::onConnectionOpen(websocketpp::connection_hdl hdl)
{
    this->addingClient = false;
    auto client =
        std::make_shared<eventapi::EventApiClient>(this->websocketClient, hdl);
    client->start();
    this->clients.emplace(hdl, client);
    this->connected.invoke();

    for (auto it = this->pendingChannels.begin();
         it != this->pendingChannels.end();)
    {
        const auto &channel = *it;
        if (client->join(*channel))
        {
            it = this->pendingChannels.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (!this->pendingChannels.empty())
    {
        this->addClient();
    }
}

void SeventvEventApi::onConnectionClose(websocketpp::connection_hdl hdl)
{
    auto clientIt = this->clients.find(hdl);

    assert(clientIt != this->clients.end());

    auto &client = clientIt->second;

    client->stop();

    this->clients.erase(clientIt);

    this->connected.invoke();
    for (auto it = client->channels.begin(); it != client->channels.end();
         it = client->channels.erase(it))
    {
        const auto &listener = *it;
        this->pendingChannels.push_back(
            std::make_unique<QString>(listener.channel));
    }
    this->addClient();
}

SeventvEventApi::WsContextPtr SeventvEventApi::onTLSInit(
    websocketpp::connection_hdl hdl)
{
    WsContextPtr ctx(
        new boost::asio::ssl::context(boost::asio::ssl::context::tlsv12));

    try
    {
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::single_dh_use);
    }
    catch (const std::exception &e)
    {
        qCDebug(chatterinoSeventvEventApi)
            << "Exception caught in OnTLSInit:" << e.what();
    }

    return ctx;
}

void SeventvEventApi::runThread()
{
    qCDebug(chatterinoSeventvEventApi) << "Start event api manager thread";
    this->websocketClient.run();
    qCDebug(chatterinoSeventvEventApi) << "Done with event api manager thread";
}
}  // namespace chatterino