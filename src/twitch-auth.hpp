#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>

// Handles the Twitch OAuth 2.0 Authorization Code + PKCE flow.
//
// Flow:
//   1. startOAuthFlow() opens the system browser to Twitch's auth page.
//   2. A temporary local HTTP server catches the redirect callback.
//   3. The auth code is exchanged for access + refresh tokens via POST.
//   4. Tokens are persisted via TokenStore.
//   5. authSuccess or authFailed is emitted.
class TwitchAuth : public QObject {
	Q_OBJECT
public:
	explicit TwitchAuth(QObject *parent = nullptr);

	// Kicks off the full OAuth flow. Opens the user's browser.
	void startOAuthFlow();

signals:
	void authSuccess(const QString &username, const QString &userId);
	void authFailed(const QString &error);

private slots:
	void onNewConnection();

private:
	QTcpServer *_server = nullptr;
	QString _codeVerifier;
	quint16 _port = 0;

	static QString generateCodeVerifier();
	static QString generateCodeChallenge(const QString &verifier);
	void exchangeCode(const QString &code);
	void fetchUserInfo(const QString &accessToken, const QString &refreshToken);
};
