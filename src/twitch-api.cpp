#include "twitch-api.hpp"
#include "token-store.hpp"

#include <obs-module.h>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QUrl>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

TwitchApi &TwitchApi::instance()
{
	static TwitchApi inst;
	return inst;
}

TwitchApi::TwitchApi(QObject *parent)
	: QObject(parent), _nam(new QNetworkAccessManager(this))
{
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

QNetworkRequest TwitchApi::makeRequest(const QString &url) const
{
	QNetworkRequest req{QUrl(url)};
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);
	req.setRawHeader(
		"Authorization",
		QStringLiteral("Bearer %1")
			.arg(TokenStore::instance().accessToken())
			.toUtf8());
	return req;
}

// ---------------------------------------------------------------------------
// Token refresh
// ---------------------------------------------------------------------------

void TwitchApi::refreshToken(std::function<void(bool)> cb)
{
	const QString refresh = TokenStore::instance().refreshToken();
	if (refresh.isEmpty()) {
		cb(false);
		return;
	}

	QNetworkRequest req(QUrl("https://id.twitch.tv/oauth2/token"));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      "application/x-www-form-urlencoded");

	QUrlQuery body;
	body.addQueryItem("client_id", TWITCH_CLIENT_ID);
	body.addQueryItem("grant_type", "refresh_token");
	body.addQueryItem("refresh_token", refresh);

	auto *reply =
		_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());

	connect(reply, &QNetworkReply::finished, [reply, cb]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			blog(LOG_WARNING,
			     "[obs-shoutout-manager] Token refresh failed: %s",
			     qPrintable(reply->errorString()));
			cb(false);
			return;
		}

		const auto obj =
			QJsonDocument::fromJson(reply->readAll()).object();
		const QString newAccess = obj["access_token"].toString();
		const QString newRefresh = obj["refresh_token"].toString();

		if (newAccess.isEmpty()) {
			cb(false);
			return;
		}

		if (!newRefresh.isEmpty())
			TokenStore::instance().setTokens(newAccess, newRefresh);
		else
			TokenStore::instance().setAccessToken(newAccess);

		cb(true);
	});
}

// ---------------------------------------------------------------------------
// Resolve username → numeric ID
// ---------------------------------------------------------------------------

void TwitchApi::getUserId(const QString &username,
			  std::function<void(QString)> cb)
{
	auto req = makeRequest(
		QString("https://api.twitch.tv/helix/users?login=%1")
			.arg(username.toLower()));

	auto *reply = _nam->get(req);

	connect(reply, &QNetworkReply::finished,
		[this, reply, username, cb]() mutable {
			reply->deleteLater();

			if (reply->error() != QNetworkReply::NoError) {
				// Try once after token refresh
				refreshToken([this, username,
					      cb](bool ok) mutable {
					if (!ok) {
						cb({});
						return;
					}
					getUserId(username, cb);
				});
				return;
			}

			const auto data =
				QJsonDocument::fromJson(reply->readAll())
					.object()["data"]
					.toArray();

			cb(data.isEmpty()
				   ? QString{}
				   : data[0].toObject()["id"].toString());
		});
}

// ---------------------------------------------------------------------------
// Send shoutout
// ---------------------------------------------------------------------------

void TwitchApi::sendShoutout(const QString &fromId, const QString &toId,
			     const QString &moderatorId,
			     std::function<void(bool, QString)> cb)
{
	const QString url =
		QString("https://api.twitch.tv/helix/chat/shoutouts"
			"?from_broadcaster_id=%1"
			"&to_broadcaster_id=%2"
			"&moderator_id=%3")
			.arg(fromId, toId, moderatorId);

	auto req = makeRequest(url);
	// POST with empty body
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      "application/json");

	auto *reply = _nam->post(req, QByteArray{});

	connect(reply, &QNetworkReply::finished,
		[this, reply, fromId, toId, moderatorId, cb]() mutable {
			reply->deleteLater();

			const int status =
				reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute)
					.toInt();

			if (status == 204) {
				// 204 No Content = success
				cb(true, {});
				return;
			}

			if (status == 401) {
				// Unauthorised — refresh and retry once
				refreshToken([this, fromId, toId, moderatorId,
					      cb](bool ok) mutable {
					if (!ok) {
						cb(false, "Auth expired");
						return;
					}
					sendShoutout(fromId, toId, moderatorId,
						     cb);
				});
				return;
			}

			const auto obj =
				QJsonDocument::fromJson(reply->readAll())
					.object();
			const QString msg =
				obj["message"].toString(reply->errorString());

			blog(LOG_WARNING,
			     "[obs-shoutout-manager] Shoutout failed (%d): %s",
			     status, qPrintable(msg));

			cb(false, msg);
		});
}

// ---------------------------------------------------------------------------
// Live check
// ---------------------------------------------------------------------------

void TwitchApi::isLive(const QString &broadcasterId,
		       std::function<void(bool)> cb)
{
	auto req = makeRequest(
		QString("https://api.twitch.tv/helix/streams?user_id=%1")
			.arg(broadcasterId));

	auto *reply = _nam->get(req);

	connect(reply, &QNetworkReply::finished, [reply, cb]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			// Network error — be optimistic (let the shoutout fail naturally)
			cb(true);
			return;
		}

		const auto data = QJsonDocument::fromJson(reply->readAll())
					  .object()["data"]
					  .toArray();
		cb(!data.isEmpty());
	});
}

// ---------------------------------------------------------------------------
// Send chat message
// ---------------------------------------------------------------------------

void TwitchApi::sendChatMessage(const QString &broadcasterId,
				const QString &senderId,
				const QString &message)
{
	QNetworkRequest req(
		QUrl("https://api.twitch.tv/helix/chat/messages"));
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);
	req.setRawHeader(
		"Authorization",
		QStringLiteral("Bearer %1")
			.arg(TokenStore::instance().accessToken())
			.toUtf8());
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject body;
	body["broadcaster_id"] = broadcasterId;
	body["sender_id"] = senderId;
	body["message"] = message;

	auto *reply =
		_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

	connect(reply, &QNetworkReply::finished, [reply]() {
		if (reply->error() != QNetworkReply::NoError) {
			blog(LOG_WARNING,
			     "[obs-shoutout-manager] Chat message failed: %s",
			     qPrintable(reply->errorString()));
		}
		reply->deleteLater();
	});
}
