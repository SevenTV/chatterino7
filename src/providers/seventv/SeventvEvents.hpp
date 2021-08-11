#ifndef SEVENTVEVENTS_H
#define SEVENTVEVENTS_H

namespace chatterino {
class SeventvEvents final
{
    const char *eventsUrl = "http://localhost:3100/channel-emotes?channel=anatoleam";

public:
    SeventvEvents();

    static void addChannel(QString login);

private:
    QNetworkRequest createRequest();
    QNetworkAccessManager *QNAM() const;
    void setQNAM(QNetworkAccessManager *qnam);
    void ensureQNAM();
    QNetworkAccessManager *m_QNAM;
};
}

#endif  // SEVENTVEVENTS_H
