#pragma once

#include <QString>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <boost/optional.hpp>
#include <common/Atomic.hpp>

namespace chatterino {

class Application;
class Paths;

void registerNmHost(Paths &paths);
std::string &getNmQueueName(Paths &paths);

Atomic<boost::optional<QString>> &nmIpcError();

class NativeMessagingClient final
{
public:
    void sendMessage(const QByteArray &array);
    void writeToCout(const QByteArray &array);
};

class NativeMessagingServer
{
public:
    NativeMessagingServer();
    void start();

    class ReceiverThread : public QThread
    {
    public:
        ReceiverThread();
        ReceiverThread(QTime *plastPing);
        void run() override;

    private:
        void handleMessage(const QJsonObject &root);
        QTime *plastPing;
    };

private:
    QTimer *detachTimer_;
    QTime lastPing;
    ReceiverThread *thread;
};

}  // namespace chatterino
