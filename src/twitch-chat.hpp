#pragma once

#include <QObject>
#include <QWebSocket>
#include <QStringList>
#include <QTimer>

// Connects to Twitch EventSub WebSocket and listens for !bso commands.
// Uses channel.chat.message subscription (modern replacement for IRC).
// Only broadcaster and moderators can trigger the command.
// Automatically reconnects on disconnect.
// Singleton — access via TwitchChat::instance().
//
// Required scopes: user:read:chat
class TwitchChat : public QObject {
	Q_OBJECT
public:
	static TwitchChat &instance();

	// Start listening. broadcasterId and userId are both the authenticated
	// broadcaster's numeric Twitch ID (same value, different label for the
	// EventSub subscription condition).
	void connectToChat(const QString &broadcasterId,
			   const QString &accessToken);

	void disconnectFromChat();
	bool isConnected() const;

signals:
	void chatConnected();
	void chatDisconnected();

	// Emitted when a broadcaster or mod sends !bso <name1> [name2] ...
	void bsoCommand(const QStringList &targets);

	void chatError(const QString &message);

private slots:
	void onConnected();
	void onTextMessageReceived(const QString &message);
	void onDisconnected();
	void onError(QAbstractSocket::SocketError err);
	void attemptReconnect();

private:
	explicit TwitchChat(QObject *parent = nullptr);

	void subscribe(const QString &sessionId);
	void handleNotification(const QJsonObject &payload);

	QWebSocket *_socket = nullptr;
	QTimer *_reconnectTimer = nullptr;
	QTimer *_keepaliveTimer = nullptr; // watchdog — reconnect if no msgs

	QString _broadcasterId;
	QString _accessToken;
	bool _intentionalDisconnect = false;

	// EventSub sends a keepalive every 10 s by default; we use 20 s timeout
	static constexpr int RECONNECT_DELAY_MS = 5'000;
	static constexpr int KEEPALIVE_TIMEOUT_MS = 20'000;
};
