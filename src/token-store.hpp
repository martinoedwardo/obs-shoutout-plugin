#pragma once

#include <QString>

// Persists Twitch OAuth tokens to the OBS plugin config directory.
// Singleton — access via TokenStore::instance().
class TokenStore {
public:
	static TokenStore &instance();

	// Returns true if both access and refresh tokens are present.
	bool hasTokens() const;

	QString accessToken() const;
	QString refreshToken() const;

	// Overwrites stored tokens and flushes to disk.
	void setTokens(const QString &access, const QString &refresh);

	// Overwrites the access token only (after a refresh).
	void setAccessToken(const QString &access);

	// Clears all tokens from memory and disk.
	void clear();

private:
	TokenStore();
	void load();
	void save() const;

	QString _accessToken;
	QString _refreshToken;
	QString _filePath;
};
