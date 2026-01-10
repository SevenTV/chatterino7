#include "widgets/dialogs/KickLoginPage.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "singletons/Theme.hpp"
#include "util/HttpServer.hpp"

#include <QClipboard>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QSpacerItem>
#include <QString>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::Literals;

namespace {

using namespace chatterino;

QByteArray generateRandomBytes(qsizetype size)
{
    assert((size % 4) == 0);
    QByteArray bytes;
    bytes.resize(size);
    auto *gen = QRandomGenerator::system();
    for (qsizetype i = 0; i < bytes.size() / 4; i++)
    {
        quint32 v = gen->generate();
        std::memcpy(bytes.data() + (i * 4), &v, 4);
    }
    return bytes;
}

struct AuthParams {
    QByteArray codeVerifier;
    QByteArray codeChallenge;
    QByteArray state;
};

AuthParams startAuthSession()
{
    auto base64Opts =
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
    auto codeVerifier = generateRandomBytes(1024).toBase64(base64Opts);

    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(codeVerifier);
    auto codeChallenge = h.result().toBase64(base64Opts);

    return {
        .codeVerifier = codeVerifier,
        .codeChallenge = codeChallenge,
        .state = generateRandomBytes(512).toBase64(base64Opts),
    };
}

class AuthDialog : public QDialog
{
public:
    AuthDialog(QString clientID, QString clientSecret,
               QWidget *parent = nullptr)
        : QDialog(parent)
        , clientID(std::move(clientID))
        , clientSecret(std::move(clientSecret))
        , authParams(startAuthSession())
        , statusLabel("Waiting...")
    {
        this->setAttribute(Qt::WA_DeleteOnClose);

        QUrlQuery query{
            {"response_type", "code"},
            {"client_id", this->clientID},
            {"redirect_uri", "http://localhost:38275"},
            {"scope", "chat:write"},
            {"code_challenge", this->authParams.codeChallenge},
            {"code_challenge_method", "S256"},
            {"state", this->authParams.state},
        };
        this->authURL = u"https://id.kick.com/oauth/authorize?" %
                        query.toString(QUrl::FullyEncoded);

        auto *srv = new HttpServer(38275, this);
        srv->setHandler([this](const QString &path) {
            return this->handleRequest(path);
        });

        auto *root = new QVBoxLayout(this);
        root->addWidget(&this->statusLabel, 1, Qt::AlignCenter);

        auto *urlButtons = new QWidget;
        auto *urlButtonLayout = new QHBoxLayout(urlButtons);

        auto *openUrl = new QPushButton(u"Log in (Opens in browser)"_s);
        QObject::connect(openUrl, &QPushButton::click, this, [this] {
            QDesktopServices::openUrl(this->authURL);
        });
        urlButtonLayout->addWidget(openUrl, 1);

        auto *copyUrl = new QPushButton(u"Copy URL"_s);
        QObject::connect(copyUrl, &QPushButton::click, this, [this] {
            qApp->clipboard()->setText(
                this->authURL.toString(QUrl::FullyEncoded));
        });
        urlButtonLayout->addWidget(copyUrl, 1);

        root->addWidget(urlButtons, 1);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        root->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                         &QDialog::reject);
    }

    std::pair<unsigned, QByteArray> handleRequest(const QString &path)
    {
        auto queryIdx = path.indexOf('?');
        if (queryIdx < 0)
        {
            return {404, "No query"_ba};
        }
        auto queryStr = path.mid(queryIdx + 1);
        QUrlQuery query(queryStr);
        if (query.hasQueryItem("done"))
        {
            return {200, "You can close this tab now."_ba};
        }

        if (!query.hasQueryItem("code"))
        {
            return {400, "No code"_ba};
        }
        if (query.queryItemValue("state") != this->authParams.state)
        {
            return {400, "State mismatch!"_ba};
        }

        QUrlQuery payload{
            {"grant_type", "authorization_code"},
            {"client_id", this->clientID},
            {"client_secret", this->clientSecret},
            {"redirect_uri", "http://localhost:38275"},
            {"code_verifier", this->authParams.codeVerifier},
            {"code", query.queryItemValue("code")},
        };
        NetworkRequest("https://id.kick.com/oauth/token",
                       NetworkRequestType::Post)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .payload(payload.toString(QUrl::FullyEncoded).toUtf8())
            .caller(this)
            .onError([this](const NetworkResult &result) {
                const auto &data = result.getData();

                qCWarning(chatterinoKick)
                    << "Getting token failed" << result.formatError() << data;

                if (!data.isEmpty())
                {
                    auto error = QJsonDocument::fromJson(data)
                                     .object()
                                     .value("error_description")
                                     .toString();
                    if (!error.isEmpty())
                    {
                        this->statusLabel.setText(u"Error: " % error % u" (" %
                                                  result.formatError() % ')');
                        return;
                    }
                }
                this->statusLabel.setText(u"Error: " % result.formatError() %
                                          u" (no further information)");
            })
            .onSuccess([this](const NetworkResult &result) {
                qWarning() << QString::fromUtf8(result.getData());
                this->accept();
                this->close();
            })
            .execute();

        return {
            200,
            "<!DOCTYPE html><html><head></head><body><script>location.search='?done=1'</script></body></html>"_ba,
        };
    }

private:
    QString clientID;
    QString clientSecret;
    AuthParams authParams;
    QUrl authURL;

    QLabel statusLabel;
};

}  // namespace

namespace chatterino {

KickLoginPage::KickLoginPage()
{
    auto *root = new QFormLayout(this);

    auto *topLabel = new QLabel(
        "The Kick API does not provide a way for chat clients like Chatterino "
        "to authenticate without exposing the client secret or using an "
        "external server that would need to see <i>all</i> tokens of "
        "<i>all</i> users.<br>Because of this, Chatterino7 currently requires "
        "users to provide their own application credentials.<br>Applications "
        "can be created at <a "
        "href=\"https://kick.com/settings/developer\">kick.com/settings/"
        "developer</a>. The following redirect URL <b>must</b> be specified: "
        "<b><code>http://localhost:38275</code></b>");
    topLabel->setWordWrap(true);
    topLabel->setOpenExternalLinks(true);
    topLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    root->addRow(topLabel);

    this->ui.clientID = new QLineEdit;
    this->ui.clientID->setPlaceholderText("ABCD123");
    root->addRow("Client ID:", this->ui.clientID);

    this->ui.clientSecret = new QLineEdit;
    this->ui.clientSecret->setPlaceholderText("12345abcd");
    this->ui.clientSecret->setEchoMode(QLineEdit::Password);
    root->addRow("Client Secret:", this->ui.clientSecret);

    root->addItem(
        new QSpacerItem(0, 10, QSizePolicy::Minimum, QSizePolicy::Fixed));

    auto *startButton = new QPushButton("Start");
    root->addRow(startButton);
}

void KickLoginPage::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    // The default QFrame background in the fusion theme has very poor contrast
    // on links, because it's bright gray.
    painter.setBrush(getTheme()->window.background);
    painter.setPen({});
    painter.drawRect(this->rect());
}

}  // namespace chatterino
