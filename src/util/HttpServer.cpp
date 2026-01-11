#include "util/HttpServer.hpp"

#include "common/QLogging.hpp"

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/span_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

using namespace chatterino;

class Handler : public QObject
{
public:
    Handler(HttpServer *server, QTcpSocket *socket)
        : QObject(server)
        , server(server)
        , socket(socket)
    {
        QObject::connect(this->socket, &QTcpSocket::readyRead, this,
                         &Handler::readyRead);
        QObject::connect(this->socket, &QTcpSocket::disconnected, this,
                         &QObject::deleteLater);

        if (this->socket->bytesAvailable() > 0)
        {
            Q_EMIT this->socket->readyRead();
        }
    }

    template <typename ConstBufferSequence>
    // NOLINTNEXTLINE(readability-identifier-naming)
    size_t write_some(ConstBufferSequence cb)
    {
        boost::beast::error_code ec;
        return this->write_some(cb, ec);
    }

    template <typename ConstBufferSequence>
    // NOLINTNEXTLINE(readability-identifier-naming)
    size_t write_some(ConstBufferSequence cb, boost::beast::error_code &ec)
    {
        ec = {};
        auto begin = boost::asio::buffer_sequence_begin(cb);
        auto end = boost::asio::buffer_sequence_end(cb);
        size_t written = 0;
        for (auto it = begin; it != end; it++)
        {
            boost::asio::const_buffer buf(*it);
            this->socket->write(static_cast<const char *>(buf.data()),
                                static_cast<qsizetype>(buf.size()));
            written += buf.size();
        }
        return written;
    }

private:
    void readyRead()
    {
        while (true)
        {
            qint64 available = this->socket->bytesAvailable();
            if (available <= 0)
            {
                return;
            }
            auto prevSize = this->message.size();
            this->message.resize(prevSize + available);
            auto nRead =
                this->socket->read(this->message.data() + prevSize, available);
            if (nRead < available)
            {
                this->message.resize(prevSize + nRead);
            }

            boost::beast::error_code ec;
            auto consumed = this->parser.put(
                boost::asio::buffer(this->message.data(),
                                    static_cast<size_t>(this->message.size())),
                ec);
            if (consumed > 0)
            {
                if (static_cast<qsizetype>(consumed) >= this->message.size())
                {
                    this->message.clear();
                }
                else
                {
                    this->message =
                        std::move(this->message)
                            .right(this->message.size() -
                                   static_cast<qsizetype>(consumed));
                }
            }

            if (ec)
            {
                if (ec == boost::beast::http::error::need_more)
                {
                    continue;
                }
                qCWarning(chatterinoHTTP) << ec.what();
                this->socket->abort();
                return;
            }

            if (this->parser.is_done())
            {
                auto &msg = this->parser.get();
                auto target = msg.base().target();
                auto path = QString::fromUtf8(
                    target.data(), static_cast<qsizetype>(target.size()));
                bool keepAlive = msg.keep_alive();
                auto [status, resBody] = this->server->handler()(path);

                boost::beast::http::response<
                    boost::beast::http::span_body<char>>
                    res{
                        static_cast<boost::beast::http::status>(status),
                        msg.base().version(),
                    };
                res.body() = boost::beast::span<char>{
                    resBody.data(), static_cast<size_t>(resBody.size())};
                res.keep_alive(keepAlive);
                res.prepare_payload();
                boost::beast::http::message_generator gen(std::move(res));

                boost::beast::write(*this, std::move(gen), ec);

                // reset parser
                std::destroy_at(&this->parser);
                std::construct_at(&this->parser);

                if (ec)
                {
                    qCWarning(chatterinoHTTP) << ec.what();
                }

                if (ec || !keepAlive)
                {
                    this->socket->close();
                    return;
                }
            }
        }
    }

    HttpServer *server;
    QTcpSocket *socket;
    QByteArray message;
    using Parser =
        boost::beast::http::parser<true, boost::beast::http::string_body>;
    Parser parser;
};

std::pair<unsigned, QByteArray> defaultHandler(const QString & /* req */)
{
    return {404, {}};
}

}  // namespace

namespace chatterino {

HttpServer::HttpServer(uint16_t port, QObject *parent)
    : QObject(parent)
    , handler_(defaultHandler)
{
    auto *tcpServer = new QTcpServer(this);
    tcpServer->listen(QHostAddress::LocalHost, port);

    QObject::connect(tcpServer, &QTcpServer::newConnection, this,
                     [this, tcpServer] {
                         while (auto *conn = tcpServer->nextPendingConnection())
                         {
                             new Handler(this, conn);
                         }
                     });
}

void HttpServer::setHandler(HandlerCb handler)
{
    if (!handler)
    {
        return;
    }
    this->handler_ = std::move(handler);
}

const HttpServer::HandlerCb &HttpServer::handler() const
{
    return this->handler_;
}

}  // namespace chatterino
