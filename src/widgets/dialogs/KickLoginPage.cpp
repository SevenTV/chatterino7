#include "widgets/dialogs/KickLoginPage.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/kick/KickAccount.hpp"
#include "singletons/Theme.hpp"
#include "util/HttpServer.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpressionValidator>
#include <QSpacerItem>
#include <QString>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <utility>

using namespace Qt::Literals;

namespace {

using namespace chatterino;

const QString REDIRECT_URL = u"http://localhost:38275"_s;
constexpr uint16_t SERVER_PORT = 38275;

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

QString formatAPIError(const NetworkResult &result)
{
    const auto json = result.parseJson();
    auto error =
        json["error_description"_L1].toString(json["message"_L1].toString());
    if (!error.isEmpty())
    {
        return u"Error: " % error % u" (" % result.formatError() % ')';
    }
    return u"Error: " % result.formatError() % u" (no further information)";
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
            {"redirect_uri", REDIRECT_URL},
            {"scope", "user:read channel:read channel:write chat:write "
                      "moderation:ban moderation:chat_message:manage"},
            {"code_challenge", this->authParams.codeChallenge},
            {"code_challenge_method", "S256"},
            {"state", this->authParams.state},
        };
        this->authURL = u"https://id.kick.com/oauth/authorize?" %
                        query.toString(QUrl::FullyEncoded);

        auto *srv = new HttpServer(SERVER_PORT, this);
        srv->setHandler([this](const QString &path) {
            return this->handleRequest(path);
        });

        auto *root = new QVBoxLayout(this);
        root->addWidget(&this->statusLabel, 1, Qt::AlignCenter);

        auto *urlButtons = new QWidget;
        auto *urlButtonLayout = new QHBoxLayout(urlButtons);

        auto *openUrl = new QPushButton(u"Log in (Opens in browser)"_s);
        QObject::connect(openUrl, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(this->authURL);
        });
        urlButtonLayout->addWidget(openUrl, 1);

        auto *copyUrl = new QPushButton(u"Copy URL"_s);
        QObject::connect(copyUrl, &QPushButton::clicked, this, [this] {
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

        this->requestToken(query.queryItemValue("code"));

        return {
            200,
            "<!DOCTYPE html><html><head></head><body><script>location.search='?done=1'</script></body></html>"_ba,
        };
    }

private:
    void requestToken(const QString &code)
    {
        QUrlQuery payload{
            {"grant_type", "authorization_code"},
            {"client_id", this->clientID},
            {"client_secret", this->clientSecret},
            {"redirect_uri", REDIRECT_URL},
            {"code_verifier", this->authParams.codeVerifier},
            {"code", code},
        };
        NetworkRequest("https://id.kick.com/oauth/token",
                       NetworkRequestType::Post)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .payload(payload.toString(QUrl::FullyEncoded).toUtf8())
            .caller(this)
            .onError([this](const NetworkResult &result) {
                auto error = formatAPIError(result);
                qCWarning(chatterinoKick) << "Getting token failed" << error;
                this->statusLabel.setText(error);
            })
            .onSuccess([this](const NetworkResult &result) {
                this->getAuthenticatedUser(result.parseJson());
            })
            .execute();
    }

    void getAuthenticatedUser(const QJsonObject &tokenData)
    {
        qint64 expiresIn = 0;
        auto expiresInVal = tokenData["expires_in"];
        if (expiresInVal.isString())
        {
            expiresIn = expiresInVal.toString().toLongLong();
        }
        else
        {
            expiresIn = expiresInVal.toInteger();
        }

        auto expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
        NetworkRequest("https://api.kick.com/public/v1/users")
            .header("Authorization",
                    u"Bearer " % tokenData["access_token"_L1].toString())
            .caller(this)
            .onError([this](const NetworkResult &result) {
                auto error = formatAPIError(result);
                qCWarning(chatterinoKick) << "Getting user failed" << error;
                this->statusLabel.setText(error);
            })
            .onSuccess([this, tokenData,
                        expiresAt](const NetworkResult &result) {
                const auto obj = result.parseJson()
                                     .value("data"_L1)
                                     .toArray()
                                     .at(0)
                                     .toObject();
                KickAccountData{
                    .username = obj["name"].toString(),
                    .userID =
                        static_cast<uint64_t>(obj["user_id"_L1].toInteger()),
                    .clientID = this->clientID,
                    .clientSecret = this->clientSecret,
                    .authToken = tokenData["access_token"_L1].toString(),
                    .refreshToken = tokenData["refresh_token"_L1].toString(),
                    .expiresAt = expiresAt,
                }
                    .save();
                getApp()->getAccounts()->kick.reloadUsers();
                this->accept();
                this->close();
            })
            .execute();
    }

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
    static const QRegularExpression nonEmptyRe{u".+"_s};

    auto *root = new QFormLayout(this);

    auto *topLabel = new QLabel(
        "The Kick API does not provide an OAuth flow for local chat clients "
        "like Chatterino "
        "to authenticate without exposing the client secret or using an "
        "external server that would need to see <i>all</i> tokens of "
        "<i>all</i> users.<br>Because of this, Chatterino7 currently requires "
        "users to provide their own application credentials. Unfortunately, "
        "this requires you to enable 2FA.<br><br>Applications "
        "can be created at <a "
        "href=\"https://kick.com/settings/developer\">kick.com/settings/"
        "developer</a>. The following redirect URL <b>must</b> be specified: "
        "<b><code>" %
        REDIRECT_URL % "</code></b>");
    topLabel->setWordWrap(true);
    topLabel->setOpenExternalLinks(true);
    topLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    root->addRow(topLabel);
    root->addItem(
        new QSpacerItem(0, 10, QSizePolicy::Minimum, QSizePolicy::Fixed));

    this->ui.clientID = new QLineEdit;
    this->ui.clientID->setPlaceholderText("ABCD123");
    this->ui.clientID->setValidator(
        new QRegularExpressionValidator(nonEmptyRe, this));
    root->addRow("Client ID:", this->ui.clientID);

    this->ui.clientSecret = new QLineEdit;
    this->ui.clientSecret->setPlaceholderText("12345abcd");
    this->ui.clientSecret->setEchoMode(QLineEdit::Password);
    this->ui.clientSecret->setValidator(
        new QRegularExpressionValidator(nonEmptyRe, this));
    root->addRow("Client Secret:", this->ui.clientSecret);

    auto currentAccount = getApp()->getAccounts()->kick.current();
    if (!currentAccount->isAnonymous())
    {
        this->ui.clientID->setText(currentAccount->clientID());
        this->ui.clientSecret->setText(currentAccount->clientSecret());
    }

    root->addItem(
        new QSpacerItem(0, 10, QSizePolicy::Minimum, QSizePolicy::Fixed));

    auto *startButton = new QPushButton("Start");
    root->addRow(startButton);
    QObject::connect(startButton, &QPushButton::clicked, this, [this] {
        if (!this->ui.clientID->hasAcceptableInput() ||
            !this->ui.clientSecret->hasAcceptableInput())
        {
            return;
        }
        auto *diag = new AuthDialog(this->ui.clientID->text(),
                                    this->ui.clientSecret->text(), this);
        QObject::connect(diag, &QDialog::accepted, this, [this] {
            this->window()->close();
        });
        diag->show();
    });
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
