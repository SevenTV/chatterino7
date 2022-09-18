#pragma once

#include <QString>
#include "providers/seventv/SeventvEventApi.hpp"

namespace chatterino {
struct EventApiDispatch {
    SeventvEventApiSubscriptionType type =
        SeventvEventApiSubscriptionType::INVALID;
    QJsonObject body;
    QString id;
    QString actorName;

    EventApiDispatch(QJsonObject _data);
};

struct EventApiEmoteAddDispatch {
    QString emoteSetId;
    QString actorName;
    QJsonObject emoteJson;
    QString emoteId;

    EventApiEmoteAddDispatch(const EventApiDispatch &dispatch,
                             QJsonObject emote);
};

struct EventApiEmoteRemoveDispatch {
    QString emoteSetId;
    QString actorName;
    QString emoteName;
    QString emoteId;

    EventApiEmoteRemoveDispatch(const EventApiDispatch &dispatch,
                                QJsonObject emote);
};

struct EventApiEmoteUpdateDispatch {
    QString emoteSetId;
    QString actorName;
    QString emoteId;
    QString oldEmoteName;
    QString emoteName;

    EventApiEmoteUpdateDispatch(const EventApiDispatch &dispatch,
                                QJsonObject emote);
};
}  // namespace chatterino
