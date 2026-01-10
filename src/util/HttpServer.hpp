#pragma once

#include <QObject>

#include <functional>

namespace chatterino {

class HttpServer : public QObject
{
public:
    HttpServer(uint16_t port, QObject *parent = nullptr);

    using HandlerCb =
        std::function<std::pair<unsigned, QByteArray>(const QString &)>;

    void setHandler(HandlerCb handler);
    const HandlerCb &handler() const;

private:
    HandlerCb handler_;
};

}  // namespace chatterino
