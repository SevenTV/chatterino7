#include "SeventvEvents.hpp"
#include "common/QLogging.hpp"
#include "common/NetworkRequest.hpp"

#include <QStringList>
#include <QNetworkAccessManager>

namespace chatterino {
namespace {
    QStringList channels;
    bool initialized = false;
} // namespace

SeventvEvents::SeventvEvents()
{
}

QNetworkRequest SeventvEvents::createRequest()
{
    qCDebug(chatterinoSeventv) << "Connecting to Seventv EventAPI";
    ensureQNAM();

    // Create the request
    QString url = "http://localhost:3100/channel-emotes?channel=anatoleam";
    QNetworkRequest request(url);
    request.setRawHeader(QByteArray("Accept"), "text/event-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, true);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    //

    return request;
}

QNetworkAccessManager *SeventvEvents::QNAM() const
{
    return m_QNAM;
}

void SeventvEvents::setQNAM(QNetworkAccessManager *qnam) {
    m_QNAM = qnam;
}

void SeventvEvents::ensureQNAM() {
    if (!initialized) {
        qCDebug(chatterinoSeventv) << "Initialized QNetworkAccessManager";

        this->setQNAM(new QNetworkAccessManager());
        initialized = true;
    }
}

void SeventvEvents::addChannel(QString login)
{
    // Add the channel to the set to be connected to
    channels.append(login);

    // Create the new network request
    QNetworkRequest request = SeventvEvents::createRequest();
}

} // namespace chatterino

