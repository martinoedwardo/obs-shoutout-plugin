#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <functional>

// Thin wrapper around the Twitch Helix API.
// All calls are asynchronous — results delivered via callbacks.
// Singleton — access via TwitchApi::instance().
class TwitchApi : public QObject {
	Q_OBJECT
public:
	static TwitchApi &instance();

	// Exchange refresh token for a new access token.
	// Callback receives success/failure.
	void refreshToken(std::function<void(bool ok)> cb);

	// Resolve a Twitch login name to a numeric user ID.
	// Callback receives the ID string, or empty string on failure.
	void getUserId(const QString &username,
		       std::function<void(QString id)> cb);

	// Send a /shoutout from fromId to toId (moderatorId is usually fromId).
	// Callback receives (success, errorMessage).
	void sendShoutout(const QString &fromId, const QString &toId,
			  const QString &moderatorId,
			  std::function<void(bool ok, QString err)> cb);

	// Check if a channel is currently live.
	// Callback receives true if live.
	void isLive(const QString &broadcasterId,
		    std::function<void(bool live)> cb);

	// Send a chat message as the authenticated user to their own channel.
	void sendChatMessage(const QString &broadcasterId,
			     const QString &senderId, const QString &message);

private:
	explicit TwitchApi(QObject *parent = nullptr);

	// Returns a pre-populated request (Auth + Client-Id headers).
	// Calls refreshToken() internally if the stored token looks stale.
	QNetworkRequest makeRequest(const QString &url) const;

	QNetworkAccessManager *_nam;
};
