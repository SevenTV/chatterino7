#include "singletons/NativeMessaging.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/QLogging.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/PostToThread.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

namespace ipc = boost::interprocess;

#ifdef Q_OS_WIN
#    include <QProcess>

#    include <Windows.h>
#    include "singletons/WindowManager.hpp"
#    include "widgets/AttachedWindow.hpp"
#endif

#include <iostream>

#define EXTENSION_ID "iphlcjigblilalddfnkdjfghhnclkcek"
#define MESSAGE_SIZE 1024

namespace chatterino {

void registerNmManifest(Paths &paths, const QString &manifestFilename,
                        const QString &registryKeyName,
                        const QJsonDocument &document);

void registerNmHost(Paths &paths)
{
    if (paths.isPortable())
        return;

    auto getBaseDocument = [&] {
        QJsonObject obj;
        obj.insert("name", "com.2547techno.technorino");
        obj.insert("description", "Browser interaction with technorino.");
        obj.insert("path", QCoreApplication::applicationFilePath());
        obj.insert("type", "stdio");

        return obj;
    };

    // chrome
    {
        QJsonDocument document;

        auto obj = getBaseDocument();
        QJsonArray allowed_origins_arr = {"chrome-extension://" EXTENSION_ID
                                          "/"};
        obj.insert("allowed_origins", allowed_origins_arr);
        document.setObject(obj);

        registerNmManifest(paths, "/native-messaging-manifest-chrome.json",
                           "HKCU\\Software\\Google\\Chrome\\NativeMessagingHost"
                           "s\\com.2547techno.technorino",
                           document);
    }

    // firefox
    // lmao fuck firefox
    /*{
        QJsonDocument document;

        auto obj = getBaseDocument();
        QJsonArray allowed_extensions = {"chatterino_native@chatterino.com"};
        obj.insert("allowed_extensions", allowed_extensions);
        document.setObject(obj);

        registerNmManifest(paths, "/native-messaging-manifest-firefox.json",
                           "HKCU\\Software\\Mozilla\\NativeMessagingHosts\\com."
                           "chatterino.chatterino",
                           document);
    }*/
}

void registerNmManifest(Paths &paths, const QString &manifestFilename,
                        const QString &registryKeyName,
                        const QJsonDocument &document)
{
    (void)registryKeyName;

    // save the manifest
    QString manifestPath = paths.miscDirectory + manifestFilename;
    QFile file(manifestPath);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(document.toJson());
    file.flush();

#ifdef Q_OS_WIN
    // clang-format off
        QProcess::execute("REG ADD \"" + registryKeyName + "\" /ve /t REG_SZ /d \"" + manifestPath + "\" /f");
// clang-format on
#endif
}

std::string &getNmQueueName(Paths &paths)
{
    static std::string name =
        "chatterino_gui" + paths.applicationFilePathHash.toStdString();
    return name;
}

// CLIENT

void NativeMessagingClient::sendMessage(const QByteArray &array)
{
    try
    {
        ipc::message_queue messageQueue(ipc::open_only, "chatterino_gui");

        messageQueue.try_send(array.data(), size_t(array.size()), 1);
        //        messageQueue.timed_send(array.data(), size_t(array.size()), 1,
        //                                boost::posix_time::second_clock::local_time() +
        //                                    boost::posix_time::seconds(10));
    }
    catch (ipc::interprocess_exception &ex)
    {
        qCDebug(chatterinoNativeMessage) << "send to gui process:" << ex.what();
    }
}

void NativeMessagingClient::writeToCout(const QByteArray &array)
{
    auto *data = array.data();
    auto size = uint32_t(array.size());

    std::cout.write(reinterpret_cast<char *>(&size), 4);
    std::cout.write(data, size);
    std::cout.flush();
}

// SERVER

NativeMessagingServer::NativeMessagingServer()
{
    this->thread = new ReceiverThread(&(this->lastPing));
}

void NativeMessagingServer::start()
{
    auto app = getApp();
    this->thread->start();

    this->lastPing = QTime::currentTime();
    this->detachTimer_ = new QTimer();
    QObject::connect(this->detachTimer_, &QTimer::timeout, [=] {
        if (getSettings()->autoDetachLiveTab &&
            (this->lastPing.addSecs(10) < QTime::currentTime()))
        {
            // detach time out reached
            this->lastPing = QTime::currentTime();
            app->twitch->watchingChannel.reset(
                app->twitch->getChannelOrEmpty(""));
        };
    });
    detachTimer_->start(5 * 1000);
}

NativeMessagingServer::ReceiverThread::ReceiverThread()
{
}

NativeMessagingServer::ReceiverThread::ReceiverThread(QTime *plastPing)
{
    //&(plastPing) = QTime::currentTime();
    this->plastPing = plastPing;
}

void NativeMessagingServer::ReceiverThread::run()
{
    try
    {
        ipc::message_queue::remove("chatterino_gui");
        ipc::message_queue messageQueue(ipc::open_or_create, "chatterino_gui",
                                        100, MESSAGE_SIZE);

        while (true)
        {
            try
            {
                auto buf = std::make_unique<char[]>(MESSAGE_SIZE);
                auto retSize = ipc::message_queue::size_type();
                auto priority = static_cast<unsigned int>(0);

                messageQueue.receive(buf.get(), MESSAGE_SIZE, retSize,
                                     priority);

                auto document = QJsonDocument::fromJson(
                    QByteArray::fromRawData(buf.get(), retSize));

                this->handleMessage(document.object());
            }
            catch (ipc::interprocess_exception &ex)
            {
                qCDebug(chatterinoNativeMessage)
                    << "received from gui process:" << ex.what();
            }
        }
    }
    catch (ipc::interprocess_exception &ex)
    {
        qCDebug(chatterinoNativeMessage)
            << "run ipc message queue:" << ex.what();

        nmIpcError().set(QString::fromLatin1(ex.what()));
    }
}

void NativeMessagingServer::ReceiverThread::handleMessage(
    const QJsonObject &root)
{
    auto app = getApp();
    QString action = root.value("action").toString();
    QString data = root.value("data").toString();

    if (action.isNull())
    {
        qCDebug(chatterinoNativeMessage) << "NM action was null";
        return;
    }

    if (action == "attach")
    {
        qCDebug(chatterinoNativeMessage) << "NM ATTACH:" << data;
        *(this->plastPing) = QTime::currentTime();
        postToThread([=] {
            if (!data.isEmpty())
            {
                app->twitch->watchingChannel.reset(
                    app->twitch->getOrAddChannel(data, true));
            }
        });
    }
    else if (action == "ping")
    {
        qCDebug(chatterinoNativeMessage) << "NM PING";
        *(this->plastPing) = QTime::currentTime();
    }
    else if (action == "detach")
    {
        qCDebug(chatterinoNativeMessage) << "NM DETACH";
        postToThread([=] {
            app->twitch->watchingChannel.reset(
                app->twitch->getChannelOrEmpty(""));
        });
    }
    else
    {
        qCDebug(chatterinoNativeMessage) << "NM unknown action " + action;
    }
}

Atomic<boost::optional<QString>> &nmIpcError()
{
    static Atomic<boost::optional<QString>> x;
    return x;
}

}  // namespace chatterino
