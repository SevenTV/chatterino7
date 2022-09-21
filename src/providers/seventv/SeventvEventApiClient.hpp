#pragma once

#include <QString>
#include <boost/functional/hash.hpp>
#include <pajlada/signals/signal.hpp>
#include "providers/seventv/SeventvEventApi.hpp"
#include "providers/seventv/SeventvEventApiWebsocket.hpp"

#include <atomic>
#include <vector>

namespace chatterino {
struct SeventvEventApiSubscription {
    QString condition;
    SeventvEventApiSubscriptionType type;
};

class SeventvEventApiClient
    : public std::enable_shared_from_this<SeventvEventApiClient>
{
public:
    // The max amount of channels we may join with a single connection
    static constexpr std::vector<QString>::size_type MAX_LISTENS = 100;

    SeventvEventApiClient(eventapi::WebsocketClient &_websocketClient,
                          eventapi::WebsocketHandle _handle);

    void start();
    void stop();

    void close(const std::string &reason,
               websocketpp::close::status::value code =
                   websocketpp::close::status::normal);

    bool subscribe(const SeventvEventApiSubscription &subscription);
    void unsubscribe(const QString &condition,
                     SeventvEventApiSubscriptionType type);

    void setHeartbeatInterval(int intervalMs);
    void handleHeartbeat();

    bool isSubscribedToEmoteSet(const QString &emoteSetId);

    std::vector<SeventvEventApiSubscription> getSubscriptions() const;

private:
    void checkHeartbeat();
    bool send(const char *payload);

    eventapi::WebsocketClient &websocketClient_;
    eventapi::WebsocketHandle handle_;
    std::vector<SeventvEventApiSubscription> subscriptions_;

    std::atomic<std::chrono::time_point<std::chrono::steady_clock>>
        lastHeartbeat_;
    std::atomic<std::chrono::milliseconds> heartbeatInterval_;
    std::atomic<bool> started_{false};
};
}  // namespace chatterino

namespace std {
template <>
struct hash<chatterino::SeventvEventApiSubscription> {
    size_t operator()(const chatterino::SeventvEventApiSubscription &sub) const
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, qHash(sub.condition));
        boost::hash_combine(seed, sub.type);

        return seed;
    }
};
}  // namespace std
