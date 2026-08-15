#pragma once

#include <IrcMessage>
#include <pajlada/signals/signal.hpp>
#include <QObject>

namespace chatterino {

class TwitchChannel;
class TwitchIrcServer;

class TwitchReadConnectionPool : public QObject
{
public:
    TwitchReadConnectionPool(TwitchIrcServer *parent);

    virtual void onChannelCreated(TwitchChannel &channel) = 0;
    virtual void reconnect() = 0;
    virtual void close() = 0;

    pajlada::Signals::Signal<Communi::IrcMessage *> messageReceived;
    pajlada::Signals::Signal<Communi::IrcPrivateMessage *>
        privateMessageReceived;
};

class TwitchReadConnectionPoolSingle;
class TwitchReadConnectionPoolMulti;

TwitchReadConnectionPool *createTwitchConnectionPool(TwitchIrcServer *parent,
                                                     bool multi);

}  // namespace chatterino
