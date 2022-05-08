#pragma once

#include <QJsonObject>
#include <QString>

#include <boost/optional.hpp>

#include "EventApiEmoteData.hpp"

namespace chatterino {
struct EventApiEmoteUpdate {
    enum class Action {
        Add,
        Remove,
        Update,

        INVALID,
    };

    QJsonObject json;

    QString channel;
    QString emoteId;
    QString actor;
    QString emoteName;

    Action action;
    QString actionString;

    boost::optional<EventApiEmoteData> emote;

    EventApiEmoteUpdate(QJsonObject _json);
};
}  // namespace chatterino
