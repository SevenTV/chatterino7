#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <boost/optional.hpp>
#include <magic_enum.hpp>
#include "providers/seventv/SeventvEventApi.hpp"

namespace chatterino {
struct EventApiMessage {
    QJsonObject data;

    SeventvEventApiOpcode op;

    EventApiMessage(QJsonObject _json);

    template <class InnerClass>
    boost::optional<InnerClass> toInner();
};

template <class InnerClass>
boost::optional<InnerClass> EventApiMessage::toInner()
{
    return InnerClass{this->data};
}

static boost::optional<EventApiMessage> parseEventApiBaseMessage(
    const QString &blob)
{
    QJsonDocument jsonDoc(QJsonDocument::fromJson(blob.toUtf8()));

    if (jsonDoc.isNull())
    {
        return boost::none;
    }

    return EventApiMessage(jsonDoc.object());
}

}  // namespace chatterino
