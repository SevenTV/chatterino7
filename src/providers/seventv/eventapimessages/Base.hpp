#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <boost/optional.hpp>

namespace chatterino {
struct EventApiMessage {
    enum class Action {
        Ping,
        Success,
        Update,
        Error,

        INVALID
    };

    QJsonObject json;

    Action action;
    QString actionString;

    EventApiMessage(QJsonObject _json);

    template <class InnerClass>
    boost::optional<InnerClass> toInner();
};

template <class InnerClass>
boost::optional<InnerClass> EventApiMessage::toInner()
{
    auto dataValue = this->json.value("payload");
    if (!dataValue.isString())
    {
        return boost::none;
    }
    auto innerDoc = QJsonDocument::fromJson(dataValue.toString().toUtf8());
    if (!innerDoc.isObject())
    {
        return boost::none;
    }

    auto data = innerDoc.object();

    return InnerClass{data};
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
