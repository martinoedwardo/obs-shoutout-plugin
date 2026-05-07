#include "twitch-chat.hpp"
#include "twitch-api.hpp"

#include <obs-module.h>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QTimer>

static constexpr char EVENTSUB_URL[] = "wss://eventsub.wss.twitch.tv/ws";

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

TwitchChat &TwitchChat::instance()
{
	static TwitchChat inst;
	return inst;
}

TwitchChat::TwitchChat(QObject *parent) : QObject(parent)
{
	_reconnectTimer = new QTimer(this);
	_reconnectTimer->setSingleShot(true);
	_reconnectTimer->setInterval(RECONNECT_DELAY_MS);
	connect(_reconnectTimer, &QTimer::timeout, this,
		&TwitchChat::attemptReconnect);

	// Watchdog — Twitch sends session_keepalive every 10 s.
	// If we go 20 s without any message, reconnect.
	_keepaliveTimer = new QTimer(this);
	_keepaliveTimer->setSingleShot(true);
	_keepaliveTimer->setInterval(KEEPALIVE_TIMEOUT_MS);
	connect(_keepaliveTimer, &QTimer::timeout, this,
		&TwitchChat::attemptReconnect);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TwitchChat::connectToChat(const QString &broadcasterId,
			       const QString &accessToken)
{
	_broadcasterId = broadcasterId;
	_accessToken = accessToken;
	_intentionalDisconnect = false;

	if (_socket) {
		_socket->abort();
		_socket->deleteLater();
	}

	_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest,
				 this);

	connect(_socket, &QWebSocket::connected, this,
		&TwitchChat::onConnected);
	connect(_socket, &QWebSocket::textMessageReceived, this,
		&TwitchChat::onTextMessageReceived);
	connect(_socket, &QWebSocket::disconnected, this,
		&TwitchChat::onDisconnected);
	connect(_socket,
		QOverload<QAbstractSocket::SocketError>::of(
			&QWebSocket::error),
		this, &TwitchChat::onError);

	blog(LOG_INFO,
	     "[obs-shoutout-manager] Connecting to EventSub WebSocket...");
	_socket->open(QUrl(EVENTSUB_URL));
}

void TwitchChat::disconnectFromChat()
{
	_intentionalDisconnect = true;
	_reconnectTimer->stop();
	_keepaliveTimer->stop();

	if (_socket) {
		_socket->close();
		_socket->deleteLater();
		_socket = nullptr;
	}
}

bool TwitchChat::isConnected() const
{
	return _socket &&
	       _socket->state() == QAbstractSocket::ConnectedState;
}

// ---------------------------------------------------------------------------
// WebSocket events
// ---------------------------------------------------------------------------

void TwitchChat::onConnected()
{
	blog(LOG_INFO,
	     "[obs-shoutout-manager] EventSub WebSocket connected, "
	     "waiting for session_welcome...");
	_keepaliveTimer->start();
}

void TwitchChat::onTextMessageReceived(const QString &message)
{
	// Reset keepalive watchdog on every message
	_keepaliveTimer->start();

	const auto doc = QJsonDocument::fromJson(message.toUtf8());
	if (doc.isNull() || !doc.isObject())
		return;

	const auto root = doc.object();
	const QString msgType =
		root["metadata"].toObject()["message_type"].toString();

	if (msgType == "session_welcome") {
		const QString sessionId =
			root["payload"]
				.toObject()["session"]
				.toObject()["id"]
				.toString();
		blog(LOG_INFO,
		     "[obs-shoutout-manager] Session ID received: %s",
		     qPrintable(sessionId));
		subscribe(sessionId);

	} else if (msgType == "notification") {
		handleNotification(root["payload"].toObject());

	} else if (msgType == "session_reconnect") {
		// Twitch asking us to reconnect to a new URL
		const QString reconnectUrl =
			root["payload"]
				.toObject()["session"]
				.toObject()["reconnect_url"]
				.toString();
		blog(LOG_INFO,
		     "[obs-shoutout-manager] EventSub reconnect requested");
		if (_socket && !reconnectUrl.isEmpty())
			_socket->open(QUrl(reconnectUrl));

	} else if (msgType == "session_keepalive") {
		// Nothing to do — watchdog timer already reset above
	}
}

// ---------------------------------------------------------------------------
// Subscribe to channel.chat.message
// ---------------------------------------------------------------------------

void TwitchChat::subscribe(const QString &sessionId)
{
	QNetworkRequest req(
		QUrl("https://api.twitch.tv/helix/eventsub/subscriptions"));
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);
	req.setRawHeader("Authorization",
			 QStringLiteral("Bearer %1").arg(_accessToken).toUtf8());
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QJsonObject condition;
	condition["broadcaster_user_id"] = _broadcasterId;
	condition["user_id"] = _broadcasterId; // reading chat as the broadcaster

	QJsonObject transport;
	transport["method"] = "websocket";
	transport["session_id"] = sessionId;

	QJsonObject body;
	body["type"] = "channel.chat.message";
	body["version"] = "1";
	body["condition"] = condition;
	body["transport"] = transport;

	auto *nam = new QNetworkAccessManager(this);
	auto *reply =
		nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

	connect(reply, &QNetworkReply::finished, [this, reply, nam]() {
		reply->deleteLater();
		nam->deleteLater();

		const int status =
			reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute)
				.toInt();

		if (status == 202) {
			blog(LOG_INFO,
			     "[obs-shoutout-manager] EventSub subscription active — listening for !bso");
			emit chatConnected();
		} else {
			const QString err =
				QJsonDocument::fromJson(reply->readAll())
					.object()["message"]
					.toString(reply->errorString());
			blog(LOG_WARNING,
			     "[obs-shoutout-manager] EventSub subscribe failed (%d): %s",
			     status, qPrintable(err));
			emit chatError(
				QString("Chat subscription failed: %1").arg(err));
		}
	});
}

// ---------------------------------------------------------------------------
// Handle channel.chat.message notification
// ---------------------------------------------------------------------------

void TwitchChat::handleNotification(const QJsonObject &payload)
{
	const QString subType =
		payload["subscription"].toObject()["type"].toString();

	if (subType != "channel.chat.message")
		return;

	const auto event = payload["event"].toObject();
	const QString text =
		event["message"].toObject()["text"].toString().trimmed();

	// Case-insensitive !bso check
	if (!text.startsWith("!bso", Qt::CaseInsensitive))
		return;

	// Permission check via badges array
	const auto badges = event["badges"].toArray();
	bool privileged = false;
	for (const auto &b : badges) {
		const QString setId = b.toObject()["set_id"].toString();
		if (setId == "moderator" || setId == "broadcaster") {
			privileged = true;
			break;
		}
	}

	if (!privileged) {
		blog(LOG_DEBUG,
		     "[obs-shoutout-manager] !bso ignored — not mod/broadcaster");
		return;
	}

	// Parse targets — everything after "!bso"
	const QString argStr = text.mid(4).trimmed();
	if (argStr.isEmpty())
		return;

	const QStringList targets =
		argStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

	if (!targets.isEmpty()) {
		blog(LOG_INFO,
		     "[obs-shoutout-manager] !bso: %s",
		     qPrintable(targets.join(", ")));
		emit bsoCommand(targets);
	}
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void TwitchChat::onDisconnected()
{
	_keepaliveTimer->stop();
	blog(LOG_INFO, "[obs-shoutout-manager] EventSub WebSocket disconnected");
	emit chatDisconnected();

	if (!_intentionalDisconnect) {
		blog(LOG_INFO,
		     "[obs-shoutout-manager] Scheduling reconnect in %d ms",
		     RECONNECT_DELAY_MS);
		_reconnectTimer->start();
	}
}

void TwitchChat::onError(QAbstractSocket::SocketError /*err*/)
{
	const QString msg = _socket ? _socket->errorString() : "Unknown error";
	blog(LOG_WARNING, "[obs-shoutout-manager] WebSocket error: %s",
	     qPrintable(msg));
	emit chatError(msg);
}

void TwitchChat::attemptReconnect()
{
	if (_intentionalDisconnect || _broadcasterId.isEmpty())
		return;

	blog(LOG_INFO,
	     "[obs-shoutout-manager] Attempting EventSub reconnect...");

	// Refresh token before reconnecting in case it expired
	TwitchApi::instance().refreshToken([this](bool ok) {
		if (ok)
			connectToChat(_broadcasterId, _accessToken);
	});
}
