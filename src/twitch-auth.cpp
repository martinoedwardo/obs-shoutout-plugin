#include "twitch-auth.hpp"
#include "token-store.hpp"
#include "twitch-api.hpp"

#include <obs-module.h>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>

// ---------------------------------------------------------------------------
// PKCE helpers
// ---------------------------------------------------------------------------

QString TwitchAuth::generateCodeVerifier()
{
	// RFC 7636 §4.1 — 43–128 URL-safe characters
	const QString chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
	QString v;
	v.reserve(64);
	for (int i = 0; i < 64; ++i)
		v += chars[static_cast<int>(
			QRandomGenerator::global()->bounded(chars.size()))];
	return v;
}

QString TwitchAuth::generateCodeChallenge(const QString &verifier)
{
	// S256 challenge = BASE64URL( SHA256( ASCII(verifier) ) )
	QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(),
						   QCryptographicHash::Sha256);
	return QString::fromLatin1(
		hash.toBase64(QByteArray::Base64UrlEncoding |
			      QByteArray::OmitTrailingEquals));
}

// ---------------------------------------------------------------------------
// TwitchAuth
// ---------------------------------------------------------------------------

TwitchAuth::TwitchAuth(QObject *parent) : QObject(parent) {}

void TwitchAuth::startOAuthFlow()
{
	// Tear down any previous attempt
	if (_server) {
		_server->close();
		_server->deleteLater();
		_server = nullptr;
	}

	_server = new QTcpServer(this);
	// Port 0 → OS picks a free port
	if (!_server->listen(QHostAddress::LocalHost, 0)) {
		emit authFailed("Could not start local callback server");
		return;
	}
	_port = _server->serverPort();

	_codeVerifier = generateCodeVerifier();
	const QString challenge = generateCodeChallenge(_codeVerifier);
	const QString redirectUri =
		QString("http://localhost:%1/callback").arg(_port);

	connect(_server, &QTcpServer::newConnection, this,
		&TwitchAuth::onNewConnection);

	// Build Twitch auth URL
	QUrl url("https://id.twitch.tv/oauth2/authorize");
	QUrlQuery q;
	q.addQueryItem("client_id", TWITCH_CLIENT_ID);
	q.addQueryItem("redirect_uri", redirectUri);
	q.addQueryItem("response_type", "code");
	// moderator:manage:shoutouts — send shoutouts via API
	// user:bot                   — send chat messages
	q.addQueryItem("scope",
		       "moderator:manage:shoutouts user:bot user:read:chat");
	q.addQueryItem("code_challenge", challenge);
	q.addQueryItem("code_challenge_method", "S256");
	url.setQuery(q);

	blog(LOG_INFO, "[obs-shoutout-manager] Opening browser for Twitch auth");
	QDesktopServices::openUrl(url);
}

void TwitchAuth::disconnect()
{
	TokenStore::instance().clear();
}

// ---------------------------------------------------------------------------
// OAuth callback — receives the GET /callback?code=... from the browser
// ---------------------------------------------------------------------------

void TwitchAuth::onNewConnection()
{
	QTcpSocket *socket = _server->nextPendingConnection();
	if (!socket)
		return;

	connect(socket, &QTcpSocket::readyRead, [this, socket]() {
		const QString request =
			QString::fromUtf8(socket->readAll());

		// Parse "GET /callback?<query> HTTP/1.1"
		static const QRegularExpression re(
			R"(GET /callback\?(\S+) HTTP)");
		const auto match = re.match(request);

		QString responseBody;
		QString code, error;

		if (match.hasMatch()) {
			QUrlQuery params(match.captured(1));
			code = params.queryItemValue("code");
			error = params.queryItemValue("error_description");
			if (error.isEmpty())
				error = params.queryItemValue("error");

			responseBody =
				"<html><body style='font-family:sans-serif;"
				"text-align:center;padding:60px'>"
				"<h1>&#x2705; Authorised!</h1>"
				"<p>You can close this tab and return to OBS.</p>"
				"</body></html>";
		} else {
			error = "Invalid callback";
			responseBody =
				"<html><body style='font-family:sans-serif;"
				"text-align:center;padding:60px'>"
				"<h1>&#x274C; Auth failed</h1>"
				"<p>Please try again from OBS.</p>"
				"</body></html>";
		}

		const QByteArray body = responseBody.toUtf8();
		const QString response =
			QString("HTTP/1.1 200 OK\r\n"
				"Content-Type: text/html; charset=utf-8\r\n"
				"Content-Length: %1\r\n"
				"Connection: close\r\n\r\n")
				.arg(body.size());

		socket->write(response.toUtf8());
		socket->write(body);
		socket->flush();
		socket->disconnectFromHost();
		_server->close();

		if (!code.isEmpty()) {
			exchangeCode(code);
		} else {
			emit authFailed(error.isEmpty() ? "Authorization denied"
						       : error);
		}
	});

	connect(socket, &QTcpSocket::disconnected, socket,
		&QTcpSocket::deleteLater);
}

// ---------------------------------------------------------------------------
// Exchange auth code for tokens
// ---------------------------------------------------------------------------

void TwitchAuth::exchangeCode(const QString &code)
{
	const QString redirectUri =
		QString("http://localhost:%1/callback").arg(_port);

	QNetworkRequest req(QUrl("https://id.twitch.tv/oauth2/token"));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      "application/x-www-form-urlencoded");

	QUrlQuery body;
	body.addQueryItem("client_id", TWITCH_CLIENT_ID);
	body.addQueryItem("code", code);
	body.addQueryItem("code_verifier", _codeVerifier);
	body.addQueryItem("grant_type", "authorization_code");
	body.addQueryItem("redirect_uri", redirectUri);

	auto *nam = new QNetworkAccessManager(this);
	auto *reply = nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());

	connect(reply, &QNetworkReply::finished, [this, reply, nam]() {
		reply->deleteLater();
		nam->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			emit authFailed(reply->errorString());
			return;
		}

		const auto obj =
			QJsonDocument::fromJson(reply->readAll()).object();
		const QString access = obj["access_token"].toString();
		const QString refresh = obj["refresh_token"].toString();

		if (access.isEmpty()) {
			emit authFailed(
				obj["message"].toString("Token exchange failed"));
			return;
		}

		fetchUserInfo(access, refresh);
	});
}

// ---------------------------------------------------------------------------
// Fetch the authenticated user's name + ID, then finalise
// ---------------------------------------------------------------------------

void TwitchAuth::fetchUserInfo(const QString &accessToken,
			       const QString &refreshToken)
{
	QNetworkRequest req(QUrl("https://api.twitch.tv/helix/users"));
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);
	req.setRawHeader("Authorization",
			 QStringLiteral("Bearer %1").arg(accessToken).toUtf8());

	auto *nam = new QNetworkAccessManager(this);
	auto *reply = nam->get(req);

	connect(reply, &QNetworkReply::finished,
		[this, reply, nam, accessToken, refreshToken]() {
			reply->deleteLater();
			nam->deleteLater();

			if (reply->error() != QNetworkReply::NoError) {
				emit authFailed(reply->errorString());
				return;
			}

			const auto obj =
				QJsonDocument::fromJson(reply->readAll())
					.object();
			const auto data = obj["data"].toArray();

			if (data.isEmpty()) {
				emit authFailed("Could not retrieve user info");
				return;
			}

			const auto user = data[0].toObject();
			const QString username =
				user["display_name"].toString();
			const QString userId = user["id"].toString();

			// Persist tokens now that we have confirmed they work
			TokenStore::instance().setTokens(accessToken,
							 refreshToken);

			blog(LOG_INFO,
			     "[obs-shoutout-manager] Authenticated as %s (id %s)",
			     qPrintable(username), qPrintable(userId));

			emit authSuccess(username, userId);
		});
}
